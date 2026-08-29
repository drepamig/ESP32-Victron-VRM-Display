#pragma once

#include <cstdint>

inline uint32_t fakeEspRandomValue = 0;
inline uint32_t esp_random() { return fakeEspRandomValue; }
