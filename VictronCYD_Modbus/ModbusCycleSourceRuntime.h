#pragma once

#ifdef CYD_SIMULATION
#include "SimModbusCycleSource.h"
using ModbusCycleSourceRuntime = SimModbusCycleSource;
#else
#include "TcpModbusCycleSource.h"
using ModbusCycleSourceRuntime = TcpModbusCycleSource;
#endif
