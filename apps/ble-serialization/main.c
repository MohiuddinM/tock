#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <nordic_common.h>
#include <nrf_error.h>
#include <ble_advdata.h>

#include <simple_ble.h>
#include <simple_adv.h>
#include <eddystone.h>

#include <firestorm.h>
#include <tmp006.h>
#include <isl29035.h>

#include "nrf.h"

void ble_address_set () {
  // nop
}


/*******************************************************************************
 * BLE
 ******************************************************************************/

// Intervals for advertising and connections
//char device_name[] = "FSTORM";
simple_ble_config_t ble_config = {
    .platform_id       = 0x00,              // used as 4th octect in device BLE address
    .device_id         = DEVICE_ID_DEFAULT,
    .adv_name          = "FSTORM",
    .adv_interval      = MSEC_TO_UNITS(500, UNIT_0_625_MS),
    .min_conn_interval = MSEC_TO_UNITS(10, UNIT_1_25_MS),
    .max_conn_interval = MSEC_TO_UNITS(1000, UNIT_1_25_MS)
};

// URL to advertise
char eddystone_url[] = "goo.gl/8685Uw";

// Manufacturer specific data setup
#define UMICH_COMPANY_IDENTIFIER 0x02E0
#define BLE_APP_ID  0x15
#define BLE_APP_VERSION_NUM 0x00
uint8_t mdata[4] = {BLE_APP_ID, BLE_APP_VERSION_NUM, 0xFF, 0xFF};
ble_advdata_manuf_data_t mandata = {
    .company_identifier = UMICH_COMPANY_IDENTIFIER,
    .data.p_data = mdata,
    .data.size   = sizeof(mdata)
};

// Sensor data service
static simple_ble_service_t sensor_service = {
    .uuid128 = {{0x1b, 0x98, 0x8e, 0xc4, 0xd0, 0xc4, 0x4a, 0x85,
                 0x91, 0x96, 0x95, 0x57, 0xf8, 0x02, 0xa0, 0x54}}
};

// characteristic to display temperature values
static simple_ble_char_t temperature_char = {.uuid16 = 0xf803};
static int16_t temp_reading = 0xFFFF;

// Light intensity characteristic
static simple_ble_char_t light_intensity_char = {.uuid16 = 0xf804};
static uint32_t light_intensity = 0;

void ble_evt_user_handler (ble_evt_t* p_ble_evt) {
    ble_gap_conn_params_t conn_params;
    memset(&conn_params, 0, sizeof(conn_params));
    conn_params.min_conn_interval = ble_config.min_conn_interval;
    conn_params.max_conn_interval = ble_config.max_conn_interval;
    conn_params.slave_latency     = SLAVE_LATENCY;
    conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONN_PARAM_UPDATE:
            // just update them right now
            sd_ble_gap_conn_param_update(0, &conn_params);
            break;
    }
}

void ble_evt_connected(ble_evt_t* p_ble_evt) {
    UNUSED_PARAMETER(p_ble_evt);
    timer_start_repeating(5000);
    printf("Connected to central\n");
}

void ble_evt_disconnected(ble_evt_t* p_ble_evt) {
    UNUSED_PARAMETER(p_ble_evt);
    timer_stop();
    printf("Disconnected from central\n");
}

void ble_error (uint32_t error_code) {
    printf("BLE ERROR: Code = %d\n", (int)error_code);
}

void services_init (void) {
    // add sensor data service
    simple_ble_add_service(&sensor_service);

    // add characteristic for temperature
    simple_ble_add_stack_characteristic(1, 0, 1, 0, // read, write, notify, vlen
                2, (uint8_t*)&temp_reading,
                &sensor_service, &temperature_char);

    // add light intensity service
    simple_ble_add_stack_characteristic(1, 0, 1, 0, // read, write, notify, vlen
                4, (uint8_t*)&light_intensity,
                &sensor_service, &light_intensity_char);
}

/*******************************************************************************
 * TEMPERATURE
 ******************************************************************************/

// Temperature read callback
static CB_TYPE temp_callback (int temp_value, int error_code, int unused, void* ud) {
    UNUSED_PARAMETER(error_code);
    UNUSED_PARAMETER(unused);
    UNUSED_PARAMETER(ud);

    temp_reading = (int16_t)temp_value;
    printf("Temp reading = %d\n", (int)temp_reading);

    simple_ble_stack_char_set(&temperature_char, 2, (uint8_t*)&temp_reading);
    simple_ble_notify_char(&temperature_char);

    // Update manufacturer specific data with new temp reading
    mdata[2] = temp_reading & 0xff;
    mdata[3] = (temp_reading >> 8) & 0xff;

    // And update advertising data
    eddystone_with_manuf_adv(eddystone_url, &mandata);
    return ASYNC;
}

static CB_TYPE timer_fired(int interval, int unused1, int unused2, void* ud) {
    UNUSED_PARAMETER(interval);
    UNUSED_PARAMETER(unused1);
    UNUSED_PARAMETER(unused2);
    UNUSED_PARAMETER(ud);

    light_intensity = isl29035_read_light_intensity();
    printf("Light (lux) reading = %d\n", (int)light_intensity);

    simple_ble_stack_char_set(&light_intensity_char, 4,
        (uint8_t*)&light_intensity);
    simple_ble_notify_char(&light_intensity_char);

    return ASYNC;
}

/*******************************************************************************
 * MAIN
 ******************************************************************************/

int main () {
    printf("Starting BLE serialization example\n");

    // Setup BLE
    simple_ble_init(&ble_config);

    // Setup reading from the temperature sensor
    tmp006_start_sampling(0x2, temp_callback, NULL);

    timer_subscribe(timer_fired, NULL);
}
