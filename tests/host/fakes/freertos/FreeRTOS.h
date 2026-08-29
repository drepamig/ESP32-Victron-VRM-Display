#pragma once

#include <cstddef>
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using TaskHandle_t = void*;

constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFAIL = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
