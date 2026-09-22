#pragma once
inline constexpr int ESP_RST_SW = 3;
inline int test_reset_reason = 1;
inline int esp_reset_reason() { return test_reset_reason; }
