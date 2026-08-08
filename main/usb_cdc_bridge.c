#include "usb_cdc_bridge.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "usb_cdc.h"

#define TAG "usb_cdc_bridge"

#define USB_CDC_BRIDGE_TASK_STACK_SIZE 3072
#define USB_CDC_BRIDGE_TASK_PRIORITY 4
#define USB_CDC_TX_TIMEOUT_MS 200

typedef struct {
    uint16_t len;
    uint8_t data[USB_CDC_BRIDGE_FRAME_SIZE];
} usb_cdc_bridge_frame_t;

static QueueHandle_t s_rx_queue;
static QueueHandle_t s_tx_queue;
static TaskHandle_t s_rx_task;
static TaskHandle_t s_tx_task;
static volatile bool s_initialized;

static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static usb_cdc_bridge_stats_t s_stats;

static void stats_add(uint64_t *counter, size_t value)
{
    portENTER_CRITICAL(&s_stats_lock);
    *counter += value;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void bridge_rx_callback(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || s_rx_queue == NULL) {
        return;
    }

    size_t offset = 0;
    while (offset < len) {
        usb_cdc_bridge_frame_t frame = {0};
        size_t chunk = len - offset;
        if (chunk > sizeof(frame.data)) {
            chunk = sizeof(frame.data);
        }

        frame.len = (uint16_t)chunk;
        memcpy(frame.data, data + offset, chunk);
        if (xQueueSend(s_rx_queue, &frame, 0) != pdTRUE) {
            stats_add(&s_stats.rx_dropped_bytes, len - offset);
            return;
        }

        stats_add(&s_stats.rx_bytes, chunk);
        offset += chunk;
    }
}

static void cdc_rx_task(void *arg)
{
    (void)arg;
    usb_cdc_bridge_frame_t frame;

    while (1) {
        if (xQueueReceive(s_rx_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGD(TAG, "RX task received %u bytes", (unsigned)frame.len);

        /* Future formal command parser entry point. */
#if USB_CDC_DEBUG_ECHO
        esp_err_t err = usb_cdc_bridge_send(frame.data, frame.len);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "echo enqueue failed: %s", esp_err_to_name(err));
        }
#endif
    }
}

static void cdc_tx_task(void *arg)
{
    (void)arg;
    usb_cdc_bridge_frame_t frame;

    while (1) {
        if (xQueueReceive(s_tx_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        size_t offset = 0;
        TickType_t start = xTaskGetTickCount();
        TickType_t timeout = pdMS_TO_TICKS(USB_CDC_TX_TIMEOUT_MS);
        if (timeout == 0) {
            timeout = 1;
        }

        while (offset < frame.len && usb_cdc_is_connected()) {
            int queued = usb_cdc_write(frame.data + offset, frame.len - offset);
            if (queued > 0) {
                offset += (size_t)queued;
            }

            if (offset >= frame.len ||
                (xTaskGetTickCount() - start) >= timeout) {
                break;
            }
            if (queued <= 0) {
                vTaskDelay(1);
            }
        }

        stats_add(&s_stats.tx_bytes, offset);
        if (offset < frame.len) {
            size_t dropped = frame.len - offset;
            stats_add(&s_stats.tx_dropped_bytes, dropped);
            ESP_LOGW(TAG, "TX dropped %u bytes (%s)", (unsigned)dropped,
                     usb_cdc_is_connected() ? "busy" : "disconnected");
        } else {
            ESP_LOGD(TAG, "TX task queued %u bytes", (unsigned)frame.len);
        }
    }
}

static void bridge_cleanup(void)
{
    if (s_rx_task != NULL) {
        vTaskDelete(s_rx_task);
        s_rx_task = NULL;
    }
    if (s_tx_task != NULL) {
        vTaskDelete(s_tx_task);
        s_tx_task = NULL;
    }
    if (s_rx_queue != NULL) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }
    if (s_tx_queue != NULL) {
        vQueueDelete(s_tx_queue);
        s_tx_queue = NULL;
    }
    s_initialized = false;
}

esp_err_t usb_cdc_bridge_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_stats, 0, sizeof(s_stats));
    s_rx_queue = xQueueCreate(USB_CDC_BRIDGE_RX_QUEUE_DEPTH,
                              sizeof(usb_cdc_bridge_frame_t));
    s_tx_queue = xQueueCreate(USB_CDC_BRIDGE_TX_QUEUE_DEPTH,
                              sizeof(usb_cdc_bridge_frame_t));
    if (s_rx_queue == NULL || s_tx_queue == NULL) {
        ESP_LOGE(TAG, "queue allocation failed");
        bridge_cleanup();
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(cdc_rx_task, "cdc_rx", USB_CDC_BRIDGE_TASK_STACK_SIZE,
                    NULL, USB_CDC_BRIDGE_TASK_PRIORITY, &s_rx_task) != pdPASS) {
        ESP_LOGE(TAG, "RX task creation failed");
        bridge_cleanup();
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(cdc_tx_task, "cdc_tx", USB_CDC_BRIDGE_TASK_STACK_SIZE,
                    NULL, USB_CDC_BRIDGE_TASK_PRIORITY, &s_tx_task) != pdPASS) {
        ESP_LOGE(TAG, "TX task creation failed");
        bridge_cleanup();
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    esp_err_t err = usb_cdc_init(bridge_rx_callback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CDC initialization failed: %s", esp_err_to_name(err));
        bridge_cleanup();
        return err;
    }

    ESP_LOGI(TAG, "bridge ready: RX=%ux%u TX=%ux%u echo=%s",
             USB_CDC_BRIDGE_RX_QUEUE_DEPTH, USB_CDC_BRIDGE_FRAME_SIZE,
             USB_CDC_BRIDGE_TX_QUEUE_DEPTH, USB_CDC_BRIDGE_FRAME_SIZE,
             USB_CDC_DEBUG_ECHO ? "on" : "off");
    return ESP_OK;
}

esp_err_t usb_cdc_bridge_send(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0 || len > USB_CDC_BRIDGE_FRAME_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_initialized || s_tx_queue == NULL || !usb_cdc_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    usb_cdc_bridge_frame_t frame = {
        .len = (uint16_t)len,
    };
    memcpy(frame.data, data, len);
    if (xQueueSend(s_tx_queue, &frame, 0) != pdTRUE) {
        stats_add(&s_stats.tx_dropped_bytes, len);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t usb_cdc_bridge_send_string(const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return usb_cdc_bridge_send((const uint8_t *)text, strlen(text));
}

bool usb_cdc_bridge_is_connected(void)
{
    return s_initialized && usb_cdc_is_connected();
}

void usb_cdc_bridge_get_stats(usb_cdc_bridge_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_stats_lock);
    *stats = s_stats;
    portEXIT_CRITICAL(&s_stats_lock);
}

uint64_t usb_cdc_bridge_get_rx_bytes(void)
{
    usb_cdc_bridge_stats_t stats;
    usb_cdc_bridge_get_stats(&stats);
    return stats.rx_bytes;
}

uint64_t usb_cdc_bridge_get_tx_bytes(void)
{
    usb_cdc_bridge_stats_t stats;
    usb_cdc_bridge_get_stats(&stats);
    return stats.tx_bytes;
}
