#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"
#include "hal/gpio_ll.h"
#include "wifi.h"

#include "gdo.h"

#include "tasks.h"
#include "homekit_decl.h"
#include "homekit.h"

static const char* TAG = "test_main";

static void gdo_event_handler(const gdo_status_t* status, gdo_cb_event_t event, void *arg)
{
    switch (event) {
    case GDO_CB_EVENT_SYNCED:
        ESP_LOGI(TAG, "Synced: %s, protocol: %s", status->synced ? "true" : "false", gdo_protocol_type_to_string(status->protocol));
        if (status->protocol == GDO_PROTOCOL_SEC_PLUS_V2) {
            ESP_LOGI(TAG, "Client ID: %" PRIu32 ", Rolling code: %" PRIu32, status->client_id, status->rolling_code);
        }

        if (!status->synced) {
            if (gdo_set_rolling_code(status->rolling_code + 100) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set rolling code");
            } else {
                ESP_LOGI(TAG, "Rolling code set to %" PRIu32 ", retryng sync", status->rolling_code);
                gdo_sync();
            }
        }
        break;
    case GDO_CB_EVENT_LIGHT:
        ESP_LOGI(TAG, "Light: %s", gdo_light_state_to_string(status->light));
        notify_homekit_light(status->light);
        break;
    case GDO_CB_EVENT_LOCK:
        ESP_LOGI(TAG, "Lock: %s", gdo_lock_state_to_string(status->lock));
        notify_homekit_current_lock(status->lock);
        break;
    case GDO_CB_EVENT_DOOR_POSITION:
        ESP_LOGI(TAG, "Door: %s, %.2f%%, target: %.2f%%", gdo_door_state_to_string(status->door),
                 (float)status->door_position, (float)status->door_target);
        notify_homekit_current_door_state_change(status->door);
        break;
    case GDO_CB_EVENT_LEARN:
        ESP_LOGI(TAG, "Learn: %s", gdo_learn_state_to_string(status->learn));
        break;
    case GDO_CB_EVENT_OBSTRUCTION:
        ESP_LOGI(TAG, "Obstruction: %s", gdo_obstruction_state_to_string(status->obstruction));
        notify_homekit_obstruction(status->obstruction);
        break;
    case GDO_CB_EVENT_MOTION:
        ESP_LOGI(TAG, "Motion: %s", gdo_motion_state_to_string(status->motion));
        notify_homekit_motion(status->motion);
        break;
    case GDO_CB_EVENT_BATTERY:
        ESP_LOGI(TAG, "Battery: %s", gdo_battery_state_to_string(status->battery));
        break;
    case GDO_CB_EVENT_BUTTON:
        ESP_LOGI(TAG, "Button: %s", gdo_button_state_to_string(status->button));
        break;
    case GDO_CB_EVENT_MOTOR:
        ESP_LOGI(TAG, "Motor: %s", gdo_motor_state_to_string(status->motor));
        break;
    case GDO_CB_EVENT_OPENINGS:
        ESP_LOGI(TAG, "Openings: %d", status->openings);
        break;
    case GDO_CB_EVENT_TTC:
        ESP_LOGI(TAG, "Time to close: %d", status->ttc_seconds);
        break;
    case GDO_CB_EVENT_PAIRED_DEVICES:
        ESP_LOGI(TAG, "Paired devices: %d remotes, %d keypads, %d wall controls, %d accessories, %d total",
                 status->paired_devices.total_remotes, status->paired_devices.total_keypads,
                 status->paired_devices.total_wall_controls, status->paired_devices.total_accessories,
                 status->paired_devices.total_all);
        break;
    default:
        ESP_LOGI(TAG, "Unknown event: %d", event);
        break;
    }
}

// Board UART pins are shared by initialization and the restart protection.
#define GDO_UART_TX_PIN GPIO_NUM_1
#define GDO_UART_RX_PIN GPIO_NUM_2

// Idle the opener line: disconnect TX from the UART signal route so losing
// UART inversion cannot drive it, stop driving the pin, and pull it down.
// Shared by the restart and panic paths. A panic can happen with the flash
// cache unusable, so this uses only ROM and register-level calls and stays in
// IRAM.
static void IRAM_ATTR gdo_uart_release_tx(void)
{
    esp_rom_gpio_connect_out_signal(GDO_UART_TX_PIN, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_output_disable(GPIO_HAL_GET_HW(GPIO_PORT_0), GDO_UART_TX_PIN);
    gpio_ll_pulldown_en(GPIO_HAL_GET_HW(GPIO_PORT_0), GDO_UART_TX_PIN);
}

// esp_restart() invokes this before resetting the UART peripheral. Release TX,
// then release both pins as inputs.
// No queued door commands, delays, allocation, or driver teardown here.
// This covers orderly software restarts. A panic goes through
// __wrap_esp_panic_handler below; power loss or a hard reset are not covered.
static void gdo_uart_shutdown(void)
{
    gdo_uart_release_tx();
    gpio_set_direction(GDO_UART_TX_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(GDO_UART_RX_PIN, GPIO_MODE_INPUT);
}

// A panic resets through esp_restart_noos(), which skips shutdown handlers.
// main/CMakeLists.txt links with --wrap=esp_panic_handler, so the panic path
// calls this first, releases TX, then continues into the real handler.
extern "C" void __real_esp_panic_handler(void *info);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(void *info)
{
    gdo_uart_release_tx();
    __real_esp_panic_handler(info);
}

extern "C" void app_main(void)
{
    gdo_config_t gdo_conf;
    gdo_conf.invert_uart = true;
    gdo_conf.obst_from_status = true;
    gdo_conf.uart_num = UART_NUM_1;
    gdo_conf.uart_tx_pin = GDO_UART_TX_PIN;
    gdo_conf.uart_rx_pin = GDO_UART_RX_PIN;
    gdo_conf.obst_in_pin = GPIO_NUM_5;

    // Do not start the opener interface if restart protection cannot register.
    ESP_ERROR_CHECK(esp_register_shutdown_handler(gdo_uart_shutdown));
    ESP_ERROR_CHECK(gdo_init(&gdo_conf));
    ESP_ERROR_CHECK(gdo_start(gdo_event_handler, NULL));

    xTaskCreate(homekit_task_entry, HOMEKIT_TASK_NAME, HOMEKIT_TASK_STK_SZ, NULL, HOMEKIT_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "GDO started!");
}
