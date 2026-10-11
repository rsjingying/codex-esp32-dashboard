#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
typedef struct {
    bool connected;
    bool has_status;
    bool app_source;
    unsigned int state;
    char project[17];
    uint16_t running, waiting, seen;
    uint32_t elapsed;
    dashboard_extra_t extra;
} dashboard_message_t;
void ble_dashboard_start(QueueHandle_t queue);
bool dashboard_send_command(const char *command);
bool dashboard_command_pending(void);
