#pragma once

#ifdef CYD_SIMULATION
#include "SimCamperNetwork.h"
using CamperNetworkRuntime = SimCamperNetwork;
#else
#include "CamperNetwork.h"
using CamperNetworkRuntime = CamperNetwork;
#endif
