#pragma once
// Capture the actual helper's output without an ESPHome device.
#define ESP_LOGI(tag, ...) capture_log(tag, __VA_ARGS__)
