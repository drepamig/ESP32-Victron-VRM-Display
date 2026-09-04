#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <Preferences.h>
#include "VenusAddressTracker.h"
namespace {
void check(bool b,const char* text) { if(!b){ std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
const BridgeClientAddress clients[]{{{2,0,0,0,0,3},0xc0a80149},{{2,0,0,0,0,4},0xc0a8014a}};
void testBindOnlySuccessfulCandidate() {
  Preferences::reset(); VenusAddressTracker t; t.begin();
  check(t.request().address==0 && !t.status(0).current,"no candidate means no subnet scan or fixed target");
  t.update(clients,2,1,100); auto first=t.request();
  check(first.address==0xc0a80149 && !t.status(100).current,"unknown client is only a probe until verified");
  check(!t.recordResult(first,false,200),"unsuccessful Modbus does not bind");
  auto second=t.request(); check(second.address==0xc0a8014a,"failure rotates to next observed AP client");
  check(t.recordResult(second,true,300),"successful system reads identify Venus");
  auto s=t.status(300); check(s.current && s.reachable && !std::strcmp(s.address,"192.168.1.74"),"confirmed address and health reach the UI");
  check(t.persistIdentity(300),"verified MAC persists");
  VenusAddressTracker reboot; reboot.begin(); reboot.update(clients,2,1,400);
  check(reboot.request().address==0xc0a8014a,"reboot follows stored MAC instead of first client");
  check(!reboot.status(400).reachable,"reboot requires fresh Modbus confirmation");
}
void testAddressChangeStaleResultAndOffline() {
  Preferences::reset(); VenusAddressTracker t; t.begin(); t.update(clients,2,1,100);
  auto first=t.request(); t.recordResult(first,true,200);
  BridgeClientAddress changed=clients[0]; changed.address=0xc0a83264;
  t.update(&changed,1,2,300);
  auto next=t.request(); check(next.address==0xc0a83264 && next.token!=first.token,"domain/address change replaces target generation");
  check(!t.recordResult(first,true,310) && !t.status(310).reachable,"old worker result cannot mark new address healthy");
  check(!std::strcmp(t.status(310).address,"192.168.50.100") && t.status(310).current,"known new lease is visible before Modbus success");
  t.recordResult(next,true,400); t.update(nullptr,0,3,500);
  auto s=t.status(500); check(!s.current && !s.reachable && !std::strcmp(s.address,"192.168.50.100"),"disconnect keeps explicitly stale address");
  check(t.request().address==0,"disconnected Venus is not polled at stale IP");
  t.update(&clients[1],1,3,600); check(t.request().address==0,"another client never replaces bound Venus");
  t.update(&changed,1,4,700); next=t.request(); t.recordResult(next,true,800);
  check(!t.status(10800).reachable && t.status(10800).current,"aging health does not erase current lease");
  t.recordResult(next,false,10900); check(t.status(10900).current,"Modbus failure preserves assigned IP");
}
void testIdentityStorageFailureAndValidation() {
  Preferences::reset(); VenusAddressTracker t; t.begin(); t.update(clients,1,1,100);
  t.recordResult(t.request(),true,200); Preferences::failFromMutation(1);
  check(!t.persistIdentity(200) && t.status(200).reachable,"NVS failure retains live verified identity");
  Preferences::clearFaults(); check(t.persistIdentity(30200),"failed identity save can retry later");
  Preferences::putRawString("venusidentity","record","1|ffffffffffff");
  VenusAddressTracker corrupt; corrupt.begin(); corrupt.update(clients,1,1,40000);
  check(!corrupt.status(40000).current,"invalid persisted MAC is not accepted as identity");
  auto req=corrupt.request(); check(req.address==0xc0a80149,"invalid storage allows bounded rediscovery");
}
}
int main(){testBindOnlySuccessfulCandidate();testAddressChangeStaleResultAndOffline();testIdentityStorageFailureAndValidation();std::puts("venus_address_tracker_test: passed");}
