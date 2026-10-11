#include "posture.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#ifdef M_PI
#undef M_PI
#endif
#include "qmi8658.h"

static void posture_task(void *arg)
{
    QueueHandle_t samples = arg;
    qmi8658_dev_t dev = {0};
    esp_err_t err = qmi8658_init(&dev, bsp_i2c_get_handle(), QMI8658_ADDRESS_HIGH);
    if (err == ESP_OK) err = qmi8658_set_accel_range(&dev, QMI8658_ACCEL_RANGE_2G);
    if (err == ESP_OK) err = qmi8658_set_accel_odr(&dev, QMI8658_ACCEL_ODR_125HZ);
    qmi8658_set_accel_unit_mps2(&dev, false);
    qmi8658_set_accel_unit_mg(&dev, true);
    if (err != ESP_OK) {
        ESP_LOGW("posture", "IMU unavailable: %s; touch controls remain available", esp_err_to_name(err));
        posture_sample_t sample = {0};
        xQueueOverwrite(samples, &sample);
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        qmi8658_data_t data;
        posture_sample_t sample = {0};
        if (qmi8658_read_sensor_data(&dev, &data) == ESP_OK) {
            sample = (posture_sample_t){ true, data.accelX/1000.0f, data.accelY/1000.0f, data.accelZ/1000.0f };
        }
        xQueueOverwrite(samples, &sample);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void posture_start(QueueHandle_t samples)
{
    if (xTaskCreate(posture_task, "dashboard_pose", 4096, samples, 4, NULL) != pdPASS)
        ESP_LOGW("posture", "Could not start IMU task; touch controls remain available");
}
