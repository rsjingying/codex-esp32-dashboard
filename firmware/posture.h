#pragma once
#include "posture_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
typedef struct { bool valid; float x, y, z; } posture_sample_t;
void posture_start(QueueHandle_t samples);
