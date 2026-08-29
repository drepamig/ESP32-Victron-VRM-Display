#include <cassert>
#include <cstdint>
#include "../../VictronCYD_Modbus/GatewayPolicy.h"

int main() {
  RetryBackoff retry(5000, 60000);
  assert(retry.nextDelay() == 5000);
  assert(retry.nextDelay() == 10000);
  assert(retry.nextDelay() == 20000);
  assert(retry.nextDelay() == 40000);
  assert(retry.nextDelay() == 60000);
  assert(retry.nextDelay() == 60000);
  retry.reset();
  assert(retry.nextDelay() == 5000);

  assert(!shouldCommitPendingProfile(false, false));
  assert(!shouldCommitPendingProfile(true, false));
  assert(!shouldCommitPendingProfile(false, true));
  assert(shouldCommitPendingProfile(true, true));

  assert(!isDeadlineReached(90, 100));
  assert(isDeadlineReached(100, 100));
  assert(isDeadlineReached(5, UINT32_MAX - 5));
  return 0;
}
