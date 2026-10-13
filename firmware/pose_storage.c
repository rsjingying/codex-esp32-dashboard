#include "pose_storage.h"
#include "nvs.h"

static const char *storage_namespace = "dashboard_pose";
static const char *storage_key = "settings";

esp_err_t pose_storage_load(pose_tracker_t *tracker, bool *enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(storage_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    pose_settings_t record = {0};
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, storage_key, &record, &size);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    if (size != sizeof(record) || !pose_settings_restore(&record, tracker, enabled))
        return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t pose_storage_save(const pose_tracker_t *tracker, bool enabled)
{
    pose_settings_t record = pose_settings_capture(tracker, enabled);
    pose_tracker_t checked;
    bool checked_enabled;
    if (!pose_settings_restore(&record, &checked, &checked_enabled)) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(storage_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, storage_key, &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t pose_storage_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(storage_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, storage_key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
