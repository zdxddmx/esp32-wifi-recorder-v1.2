#ifndef USB_CDC_BRIDGE_H
#define USB_CDC_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define USB_CDC_BRIDGE_FRAME_SIZE 512U
#define USB_CDC_BRIDGE_RX_QUEUE_DEPTH 8U
#define USB_CDC_BRIDGE_TX_QUEUE_DEPTH 8U

/* Development-only raw byte echo. Set to 0 when a formal parser is connected. */
#ifndef USB_CDC_DEBUG_ECHO
#define USB_CDC_DEBUG_ECHO 1
#endif

typedef struct {
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_dropped_bytes;
    uint64_t tx_dropped_bytes;
} usb_cdc_bridge_stats_t;

esp_err_t usb_cdc_bridge_init(void);

/* Binary-safe, non-blocking enqueue. Length must be explicit and <= frame size. */
esp_err_t usb_cdc_bridge_send(const uint8_t *data, size_t len);
esp_err_t usb_cdc_bridge_send_string(const char *text);

bool usb_cdc_bridge_is_connected(void);
uint64_t usb_cdc_bridge_get_rx_bytes(void);
uint64_t usb_cdc_bridge_get_tx_bytes(void);
void usb_cdc_bridge_get_stats(usb_cdc_bridge_stats_t *stats);

#endif
