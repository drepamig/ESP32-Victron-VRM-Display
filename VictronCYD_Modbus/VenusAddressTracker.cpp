#include "VenusAddressTracker.h"
#include <Preferences.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
bool same(const uint8_t* a, const uint8_t* b) { return std::memcmp(a,b,6)==0; }
bool validMac(const uint8_t* m) { const uint8_t zero[6]{}; return !(m[0]&1) && !same(m,zero); }
int hex(char c) { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; return -1; }
void addressText(uint32_t ip, char (&out)[16]) {
  std::snprintf(out,sizeof(out),"%u.%u.%u.%u",unsigned(ip>>24),unsigned((ip>>16)&255),unsigned((ip>>8)&255),unsigned(ip&255));
}
bool loadIdentity(uint8_t* mac) {
  Preferences prefs;
  if(!prefs.begin("venusidentity",true)) return false;
  const String record=prefs.getString("record",String()); prefs.end();
  if(record.length()!=14 || std::strlen(record.c_str())!=14 || std::strncmp(record.c_str(),"1|",2)) return false;
  uint8_t parsed[6];
  for(size_t i=0;i<6;++i) {
    int high=hex(record.c_str()[2+i*2]), low=hex(record.c_str()[3+i*2]);
    if(high<0||low<0)return false;
    parsed[i]=static_cast<uint8_t>(high*16+low);
  }
  if(!validMac(parsed))return false;
  std::memcpy(mac,parsed,6); return true;
}
}

void VenusAddressTracker::begin() {
  bound_=loadIdentity(boundMac_);
}
bool VenusAddressTracker::isBoundMac(const uint8_t mac[6]) const { return bound_&&same(boundMac_,mac); }
void VenusAddressTracker::select(size_t index) {
  const uint32_t address=index<count_?clients_[index].address:0;
  const uint8_t empty[6]{};
  const uint8_t* mac=index<count_?clients_[index].mac:empty;
  if(address==target_.address&&same(mac,target_.mac))return;
  target_.address=address; std::memcpy(target_.mac,mac,6); ++target_.token;
  lastReadValid_=false;
  if(address&&isBoundMac(mac))addressText(address,lastAddress_);
}
void VenusAddressTracker::update(const BridgeClientAddress* clients,size_t count,
                                 uint32_t generation,uint32_t) {
  if(generation!=generation_) {
    generation_=generation; target_.address=0; std::memset(target_.mac,0,6);
    ++target_.token; lastReadValid_=false;
  }
  count_=0;
  for(size_t i=0;clients&&i<std::min(count,Ipv4BridgeCore::kMaxClients);++i) {
    if(!validMac(clients[i].mac)||!clients[i].address)continue;
    clients_[count_++]=clients[i];
  }
  if(bound_) {
    size_t index=0; while(index<count_&&!isBoundMac(clients_[index].mac))++index;
    select(index); return;
  }
  // Keep an in-flight unbound candidate stable across refreshes. Failed probes
  // rotate only among observed AP clients, never across the upstream subnet.
  for(size_t i=0;i<count_;++i) if(same(clients_[i].mac,target_.mac)) {select(i);return;}
  select(0);
}
VenusProbe VenusAddressTracker::request() const { return target_; }
bool VenusAddressTracker::recordResult(const VenusProbe& probe,bool valid,uint32_t nowMs) {
  if(!target_.address||probe.token!=target_.token||probe.address!=target_.address||!same(probe.mac,target_.mac))return false;
  lastReadValid_=valid;
  if(valid) {
    if(!bound_) {std::memcpy(boundMac_,probe.mac,6);bound_=true;identityDirty_=true;}
    addressText(target_.address,lastAddress_);lastSuccessMs_=nowMs;return true;
  }
  if(!bound_&&count_>1) {
    for(size_t i=0;i<count_;++i) if(same(clients_[i].mac,target_.mac)) {select((i+1)%count_);break;}
  }
  return false;
}
VenusConnectionStatus VenusAddressTracker::status(uint32_t nowMs) const {
  VenusConnectionStatus s{};
  std::memcpy(s.address,lastAddress_,sizeof(s.address));
  s.current=bound_&&target_.address!=0&&isBoundMac(target_.mac);
  s.reachable=s.current&&lastReadValid_&&(nowMs-lastSuccessMs_<10000);
  return s;
}
bool VenusAddressTracker::persistIdentity(uint32_t nowMs) {
  if(!identityDirty_)return true;
  if(saveAttempted_&&nowMs-lastSaveAttemptMs_<30000)return false;
  saveAttempted_=true;lastSaveAttemptMs_=nowMs;
  char record[15]; std::snprintf(record,sizeof(record),"1|%02x%02x%02x%02x%02x%02x",boundMac_[0],boundMac_[1],boundMac_[2],boundMac_[3],boundMac_[4],boundMac_[5]);
  Preferences prefs;if(!prefs.begin("venusidentity",false))return false;
  const bool saved=prefs.putString("record",record)==14;prefs.end();
  if(saved)identityDirty_=false;
  return saved;
}
