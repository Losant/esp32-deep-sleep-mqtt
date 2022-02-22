#include "freertos/FreeRTOS.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_adc_cal.h"
#include "soc/adc_channel.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "esp32-bat-mon";

/**
 * Losant Credentials
 * put your device ID, access key, and access
 * secret here to be used throughout the application. 
*/
#define LOSANT_DEVICE_ID ""
#define LOSANT_ACCESS_KEY ""
#define LOSANT_ACCESS_SECRET ""

#define EXAMPLE_ESP_WIFI_SSID ""
#define EXAMPLE_ESP_WIFI_PASS ""

#define EXAMPLE_ESP_MAXIMUM_RETRY 5
#define DEFAULT_VREF 1100

#define BROKER_URL "broker.losant.com"

/* FreeRTOS event group to signal when we are connected to WiFi */
static EventGroupHandle_t s_wifi_event_group;

// FreeRTOS even group to signal when we are connected to MQTT
static EventGroupHandle_t s_mqtt_event_group;

static esp_adc_cal_characteristics_t *adc_chars;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_FAIL_BIT BIT1


static int s_retry_num = 0;

/**
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 * 
 * @param event The data for the event
 * @return esp_err_t 
 */
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        xEventGroupSetBits(s_mqtt_event_group, MQTT_FAIL_BIT);
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

/**
 * @brief MQTT connection handler.
 * Uses `mqtt_cfg` as MQTT connection configuration.
 * Passes data to the MQTT Event handler `mqtt_event_handler` and sets a bit 
 * if connection is successful.
 *
 * 
 * @return esp_mqtt_client_handle_t 
 */
static esp_mqtt_client_handle_t mqtt_app_start(void)
{
    s_mqtt_event_group = xEventGroupCreate();

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://broker.losant.com",
        .username = LOSANT_ACCESS_KEY,
        .password = LOSANT_ACCESS_SECRET,
        .client_id = LOSANT_DEVICE_ID

    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
                                        MQTT_CONNECTED_BIT | MQTT_FAIL_BIT,
                                        pdFALSE,
                                        pdFALSE,
                                        portMAX_DELAY);

    if (bits & MQTT_CONNECTED_BIT)
    {
        return client;
    }

    return NULL;
}

/**
 * @brief WiFi & IP event handler. 
 * Handles WiFi reconnect logic based on EXAMPLE_ESP_MAXIMUM_RETRY and outputs 
 * information. 
 * 
 * @param arg 
 * @param event_base Base of the event (e.g. WIFI_EVENT)
 * @param event_id ID of the event 
 * @param event_data Data associated with the event
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Connect to WiFi using EXAMPLE_ESP_WIFI_SSID and EXAMPLE_ESP_WIFI_PASS with error handling.
 * see https://github.com/espressif/esp-idf/tree/master/examples/wifi for more information.
 * 
 */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGI(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

/**
 * @brief Reads battery voltage and publishes the value to an MQTT topic.
 * On the Adafruit Huzzah32 1/2 of the battery voltage can be read on pin 35.
 * Currently, the topic is the Losant Device State topic, but can be updated to any valid topic
 * for the connected broker.
 * 
 * @param client MQTT client used for publishing to a topic.
 */
void read_bat_and_publish(void *client)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_GPIO35_CHANNEL, ADC_ATTEN_11db);

    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));

    //build state topic
    char state_topic[128];
    sprintf(state_topic, "losant/%s/state", LOSANT_DEVICE_ID);

    int adc_reading = adc1_get_raw(ADC1_GPIO35_CHANNEL);
    ESP_LOGI(TAG, "Raw: %d", adc_reading);

    /* 
    * This board has voltage divider, so need to multiply by 2.
    * See this link for more info: https://learn.adafruit.com/adafruit-huzzah32-esp32-feather/power-management#measuring-battery-2385442-8
    */
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars) * 2;
    ESP_LOGI(TAG, "Voltage: %d", voltage);

    cJSON *payload = cJSON_CreateObject();
    cJSON *data = cJSON_AddObjectToObject(payload, "data");
    cJSON_AddNumberToObject(data, "battery_voltage", voltage);

    esp_mqtt_client_publish(client, state_topic, cJSON_Print(payload), 0, 0, 0);

}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());

    // start wifi
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    esp_mqtt_client_handle_t client = mqtt_app_start();

    if (client) {
        read_bat_and_publish(client);   

        // disconnect from MQTT client
        esp_mqtt_client_disconnect(client);
    }


    const int wakeup_time_sec = 10; // 900 seconds = 15 minutes
    ESP_LOGI(TAG, "Enabling timer wakeup, %ds\n", wakeup_time_sec);
    esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
    ESP_LOGI(TAG, "Entering deep sleep\n");
    esp_deep_sleep_start();
}
