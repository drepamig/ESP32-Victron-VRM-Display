#pragma once

#include <vector>

#include "queue.h"

using TaskFunction_t = void (*)(void*);

namespace FakeRtos {
struct PendingTask { TaskFunction_t function; void* argument; BaseType_t core; };
inline std::vector<PendingTask> pendingTasks;
inline bool taskCreationSucceeds = true;
inline int createCalls = 0;
inline int deleteCalls = 0;
inline void reset() {
  pendingTasks.clear();
  taskCreationSucceeds = true;
  createCalls = 0;
  deleteCalls = 0;
  lastQueueLength = 0;
  lastQueueItemSize = 0;
  queueCreationSucceeds = true;
  queueCreateCalls = 0;
}
inline void runNextTask() { const PendingTask task = pendingTasks.front(); pendingTasks.erase(pendingTasks.begin()); task.function(task.argument); }
}  // namespace FakeRtos

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t function, const char*, uint32_t, void* argument,
                                         UBaseType_t, TaskHandle_t*, BaseType_t core) {
  ++FakeRtos::createCalls;
  if (!FakeRtos::taskCreationSucceeds) return pdFAIL;
  FakeRtos::pendingTasks.push_back({function, argument, core});
  return pdPASS;
}

inline void vTaskDelete(TaskHandle_t) { ++FakeRtos::deleteCalls; }
