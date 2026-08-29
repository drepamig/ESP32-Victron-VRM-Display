#pragma once

#include <cstring>
#include <vector>

#include "FreeRTOS.h"

struct FakeQueue {
  UBaseType_t length;
  UBaseType_t itemSize;
  bool occupied = false;
  std::vector<unsigned char> item;
};

using QueueHandle_t = FakeQueue*;

namespace FakeRtos {
inline UBaseType_t lastQueueLength = 0;
inline UBaseType_t lastQueueItemSize = 0;
inline bool queueCreationSucceeds = true;
inline int queueCreateCalls = 0;
}  // namespace FakeRtos

inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
  ++FakeRtos::queueCreateCalls;
  FakeRtos::lastQueueLength = length;
  FakeRtos::lastQueueItemSize = itemSize;
  if (!FakeRtos::queueCreationSucceeds) return nullptr;
  return new FakeQueue{length, itemSize, false, std::vector<unsigned char>(itemSize)};
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t) {
  if (queue == nullptr || queue->occupied || queue->length == 0) return pdFAIL;
  std::memcpy(queue->item.data(), item, queue->itemSize);
  queue->occupied = true;
  return pdPASS;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void* output, TickType_t) {
  if (queue == nullptr || !queue->occupied) return pdFAIL;
  std::memcpy(output, queue->item.data(), queue->itemSize);
  queue->occupied = false;
  return pdPASS;
}
