#include "HalScreenCalibration.h"

#include <Logging.h>
#include <nvs.h>

namespace {
constexpr const char* NAMESPACE = "screen-cal";
constexpr const char* KEY = "edges";
}  // namespace

bool HalScreenCalibration::load(uint8_t (&edges)[4]) {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;  // never calibrated
  size_t size = sizeof(edges);
  const esp_err_t error = nvs_get_blob(handle, KEY, edges, &size);
  nvs_close(handle);
  return error == ESP_OK && size == sizeof(edges);
}

bool HalScreenCalibration::save(const uint8_t (&edges)[4]) {
  nvs_handle_t handle;
  esp_err_t error = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
  if (error == ESP_OK) {
    error = nvs_set_blob(handle, KEY, edges, sizeof(edges));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
  }
  if (error != ESP_OK) LOG_ERR("CAL", "Could not save calibration: %d", error);
  return error == ESP_OK;
}
