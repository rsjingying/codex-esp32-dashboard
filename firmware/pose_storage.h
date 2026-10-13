#pragma once
#include "esp_err.h"
#include "pose_settings.h"

// Call after nvs_flash_init(). Missing records are reported as ESP_ERR_NVS_NOT_FOUND.
esp_err_t pose_storage_load(pose_tracker_t *tracker, bool *enabled);
esp_err_t pose_storage_save(const pose_tracker_t *tracker, bool enabled);
esp_err_t pose_storage_clear(void);
