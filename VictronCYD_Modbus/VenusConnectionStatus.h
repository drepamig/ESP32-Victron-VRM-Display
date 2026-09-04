#pragma once

struct VenusConnectionStatus {
  char address[16]{};
  bool current = false;
  bool reachable = false;
};
