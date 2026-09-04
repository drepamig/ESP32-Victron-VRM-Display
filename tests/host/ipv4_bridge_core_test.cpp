#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "Ipv4BridgeCore.h"

namespace {
using Frame = std::vector<uint8_t>;
const uint8_t sta[6]{2,0,0,0,0,1}, ap[6]{2,0,0,0,0,2};
const uint8_t a[6]{2,0,0,0,0,3}, b[6]{2,0,0,0,0,4};
const uint8_t router[6]{2,0,0,0,0,5}, broadcast[6]{255,255,255,255,255,255};
void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
void w16(uint8_t* p, uint16_t n) { p[0]=n>>8; p[1]=n; }
void w32(uint8_t* p, uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
Frame ipv4(const uint8_t* src, const uint8_t* dst, uint32_t sip, uint32_t dip, size_t payload=20) {
  Frame f(34+payload, 0);
  std::memcpy(f.data(),dst,6); std::memcpy(f.data()+6,src,6); w16(f.data()+12,0x0800);
  f[14]=0x45; w16(f.data()+16,20+payload); f[22]=64; f[23]=6;
  w32(f.data()+26,sip); w32(f.data()+30,dip);
  return f;
}
Frame arp(const uint8_t* src, const uint8_t* dst, uint32_t sip, uint32_t tip, uint16_t op=1) {
  Frame f(42,0); std::memcpy(f.data(),dst,6); std::memcpy(f.data()+6,src,6);
  w16(f.data()+12,0x0806); w16(f.data()+14,1); w16(f.data()+16,0x0800);
  f[18]=6; f[19]=4; w16(f.data()+20,op); std::memcpy(f.data()+22,src,6);
  w32(f.data()+28,sip); if(op==2) std::memcpy(f.data()+32,dst,6); w32(f.data()+38,tip);
  return f;
}
Frame dhcp(const uint8_t* client, bool reply, uint8_t type, uint32_t xid, uint32_t offer=0, uint32_t lease=120) {
  auto f=ipv4(reply?router:client,reply?sta:broadcast,reply?0xc0a80101:0,reply?0xffffffff:0xffffffff,8+250);
  f[23]=17; auto* udp=f.data()+34; w16(udp,reply?67:68); w16(udp+2,reply?68:67);
  w16(udp+4,258); w16(udp+6,0x1234);
  auto* bootp=udp+8; bootp[0]=reply?2:1; bootp[1]=1; bootp[2]=6;
  w32(bootp+4,xid); w32(bootp+16,offer); std::memcpy(bootp+28,client,6);
  w32(bootp+236,0x63825363); bootp[240]=53; bootp[241]=1; bootp[242]=type;
  bootp[243]=51; bootp[244]=4; w32(bootp+245,lease); bootp[249]=255;
  return f;
}
struct Fixture {
  Ipv4BridgeCore core;
  Fixture() { core.configure(sta,ap,0xc0a8010a,0xffffff00); core.setAssociated(a,true); core.setAssociated(b,true); }
  void learn(const uint8_t* mac, uint32_t ip) { auto f=arp(mac,broadcast,ip,0xc0a80101); core.process(BridgeSide::Ap,f.data(),f.size(),100); }
};

// Missing MAC translation would make clients invisible to upstream initiators.
void testBidirectionalIpv4AndOwnTargetLookup() {
  Fixture f; f.learn(a,0xc0a80149); f.learn(b,0xc0a8014a);
  auto outbound=ipv4(a,router,0xc0a80149,0x08080808); auto original=outbound;
  auto d=f.core.process(BridgeSide::Ap,outbound.data(),outbound.size(),101);
  check(d.sendSta && !d.sendAp && !d.local,"client IPv4 goes upstream");
  check(!std::memcmp(outbound.data()+6,sta,6) && !std::memcmp(outbound.data()+14,original.data()+14,40),"only Ethernet source translated outbound");
  auto inbound=ipv4(router,sta,0xc0a80155,0xc0a80149); original=inbound;
  d=f.core.process(BridgeSide::Sta,inbound.data(),inbound.size(),102);
  check(d.sendAp && !d.local && !std::memcmp(inbound.data(),a,6),"upstream can initiate IPv4 to first client");
  check(!std::memcmp(inbound.data()+14,original.data()+14,40),"IP and transport bytes survive reverse forwarding");
  inbound=ipv4(router,sta,0xc0a80155,0xc0a8014a);
  d=f.core.process(BridgeSide::Sta,inbound.data(),inbound.size(),103);
  check(d.sendAp && !std::memcmp(inbound.data(),b,6),"second client keeps a distinct IP and MAC mapping");
  uint8_t found[6]; check(f.core.macForAddress(0xc0a80149,found,103) && !std::memcmp(found,a,6),"ESP Modbus output can resolve local AP destination");
  inbound=ipv4(router,sta,0xc0a80155,0xc0a8010a);
  d=f.core.process(BridgeSide::Sta,inbound.data(),inbound.size(),104);
  check(d.local && !d.sendAp,"ESP's upstream address goes to local stack once");
  inbound=ipv4(a,ap,0xc0a80149,0xc0a8010a);
  d=f.core.process(BridgeSide::Ap,inbound.data(),inbound.size(),105);
  check(d.local && !d.sendSta,"AP clients can reach ESP management address");
}
void testArpProxyAndNoPhantomClients() {
  Fixture f; f.learn(a,0xc0a80149);
  auto frame=arp(router,broadcast,0xc0a80101,0xc0a80149);
  auto d=f.core.process(BridgeSide::Sta,frame.data(),frame.size(),101);
  check(d.sendSta && frame[21]==2 && !std::memcmp(frame.data(),router,6),"proxy ARP answers upstream for known client");
  check(!std::memcmp(frame.data()+22,sta,6) && frame[31]==73,"upstream ARP associates Venus IP with STA MAC");
  frame=arp(a,broadcast,0xc0a80149,0xc0a8010a);
  d=f.core.process(BridgeSide::Ap,frame.data(),frame.size(),102);
  check(d.sendAp && frame[21]==2 && !std::memcmp(frame.data()+22,ap,6),"management proxy ARP returns on AP");
  frame=arp(router,sta,0xc0a80101,0xc0a80149,2);
  d=f.core.process(BridgeSide::Sta,frame.data(),frame.size(),102);
  check(d.sendAp && !std::memcmp(frame.data()+22,router,6),"upstream gateway ARP identity remains its real destination MAC");
  frame=ipv4(a,frame.data()+22,0xc0a80149,0x08080808);
  d=f.core.process(BridgeSide::Ap,frame.data(),frame.size(),102);
  check(d.sendSta && !std::memcmp(frame.data(),router,6),"client internet packets target real upstream gateway after ARP");
  const uint8_t unknown[6]{2,3,4,5,6,7}; frame=arp(unknown,broadcast,0xc0a80166,0xc0a80101);
  d=f.core.process(BridgeSide::Ap,frame.data(),frame.size(),103);
  uint8_t found[6]; check(!d.sendSta && !f.core.macForAddress(0xc0a80166,found,103),"unassociated source never becomes an AP client");
}
void testDhcpIdentityAndLeaseLifecycle() {
  Fixture f; auto req=dhcp(a,false,1,0x01020304);
  auto d=f.core.process(BridgeSide::Ap,req.data(),req.size(),100);
  check(d.sendSta && req[52]==0x80 && req[40]==0 && req[41]==0,"DHCP forces broadcast reply and repairs UDP checksum");
  check(!std::memcmp(req.data()+70,a,6),"DHCP hardware identity stays the actual client");
  auto reply=dhcp(a,true,5,0x01020304,0xc0a80149,120);
  d=f.core.process(BridgeSide::Sta,reply.data(),reply.size(),200);
  uint8_t found[6]; check(d.sendAp && !std::memcmp(reply.data(),a,6) && f.core.macForAddress(0xc0a80149,found,201),"ACK is delivered and learned before client sends IP traffic");
  auto probe=arp(a,broadcast,0,0xc0a80149);
  d=f.core.process(BridgeSide::Ap,probe.data(),probe.size(),202);
  check(d.sendSta && !d.sendAp && probe[21]==1,"DHCP client probes its own offered address without a false conflict reply");
  auto conflict=arp(router,sta,0xc0a80149,0,2);
  d=f.core.process(BridgeSide::Sta,conflict.data(),conflict.size(),202);
  check(d.sendAp && !std::memcmp(conflict.data(),a,6) &&
        !std::memcmp(conflict.data()+22,router,6) && !std::memcmp(conflict.data()+32,a,6),
        "a real upstream conflict reply to an unspecified-IP probe reaches the DHCP client");
  auto announce=arp(a,broadcast,0xc0a80149,0xc0a80149);
  d=f.core.process(BridgeSide::Ap,announce.data(),announce.size(),203);
  check(d.sendSta && !d.sendAp && announce[21]==1,"client announces its own address upstream without a false conflict reply");
  probe=arp(b,broadcast,0,0xc0a80149);
  d=f.core.process(BridgeSide::Ap,probe.data(),probe.size(),204);
  check(d.sendAp && !d.sendSta && probe[21]==2,"a different client probing an occupied address still gets a conflict reply");
  auto duplicate=arp(b,broadcast,0xc0a80149,0xc0a80101);
  f.core.process(BridgeSide::Ap,duplicate.data(),duplicate.size(),205);
  check(f.core.macForAddress(0xc0a80149,found,205) && !std::memcmp(found,a,6),
        "another client's observed address cannot replace a current DHCP lease");
  duplicate=ipv4(b,router,0xc0a80149,0x08080808);
  f.core.process(BridgeSide::Ap,duplicate.data(),duplicate.size(),206);
  check(f.core.macForAddress(0xc0a80149,found,206) && !std::memcmp(found,a,6),
        "IPv4 source observations cannot replace another client's DHCP lease");
  check(!f.core.macForAddress(0xc0a80149,found,120200),"expired lease is not current");
  req=dhcp(a,false,3,5); f.core.process(BridgeSide::Ap,req.data(),req.size(),120300);
  reply=dhcp(a,true,5,5,0xc0a80150);
  w32(reply.data()+30,0xc0a80150);  // A server may still unicast its renewal ACK.
  d=f.core.process(BridgeSide::Sta,reply.data(),reply.size(),120400);
  check(d.sendAp && !std::memcmp(reply.data(),a,6),"unicast DHCP renewal reaches the actual client");
  check(f.core.macForAddress(0xc0a80150,found,120500) && !f.core.macForAddress(0xc0a80149,found,120500),"renewal replaces prior address for same MAC");
  req=dhcp(a,false,3,6); f.core.process(BridgeSide::Ap,req.data(),req.size(),120600);
  reply=dhcp(a,true,6,6); f.core.process(BridgeSide::Sta,reply.data(),reply.size(),120700);
  check(!f.core.macForAddress(0xc0a80150,found,120701),"DHCP NAK invalidates address");
  reply=dhcp(b,true,5,99,0xc0a80151); f.core.process(BridgeSide::Sta,reply.data(),reply.size(),120800);
  check(!f.core.macForAddress(0xc0a80151,found,120800),"unsolicited ACK cannot assign a candidate");
}
void testMalformedFramesAndReset() {
  Fixture f; f.learn(a,0xc0a80149); uint8_t found[6];
  auto frame=ipv4(a,router,0xc0a80149,0x08080808);
  for(size_t n=0;n<frame.size();++n) {
    auto copy=frame; auto d=f.core.process(BridgeSide::Ap,copy.data(),n,101);
    check(!d.sendSta && !d.sendAp,"truncated IPv4 never forwarded");
  }
  frame[14]=0x4f; auto d=f.core.process(BridgeSide::Ap,frame.data(),frame.size(),102);
  check(!d.sendSta,"invalid IPv4 header length rejected");
  frame=dhcp(a,false,1,1); frame[283]=250;
  d=f.core.process(BridgeSide::Ap,frame.data(),frame.size(),103);
  check(!d.sendSta,"overrun DHCP option rejected");
  frame=ipv4(a,router,0xc0a80149,0x08080808); frame[20]=0x20;
  d=f.core.process(BridgeSide::Ap,frame.data(),frame.size(),104);
  check(d.sendSta,"ordinary IPv4 fragments pass without transport parsing");
  f.core.setAssociated(a,false); check(!f.core.macForAddress(0xc0a80149,found,105),"disconnect removes mapping");
  f.core.setAssociated(a,true); f.learn(a,0xc0a80149);
  f.core.configure(sta,ap,0xac10000a,0xffffff00);
  check(!f.core.macForAddress(0xc0a80149,found,106),"domain change clears old mapping");
}
void testDhcpCanReassignAnAddress() {
  Fixture f;
  auto request=dhcp(a,false,3,1);
  f.core.process(BridgeSide::Ap,request.data(),request.size(),100);
  auto ack=dhcp(a,true,5,1,0xc0a80149);
  f.core.process(BridgeSide::Sta,ack.data(),ack.size(),101);
  request=dhcp(b,false,3,2);
  f.core.process(BridgeSide::Ap,request.data(),request.size(),200);
  ack=dhcp(b,true,5,2,0xc0a80149);
  f.core.process(BridgeSide::Sta,ack.data(),ack.size(),201);
  uint8_t found[6];
  check(f.core.macForAddress(0xc0a80149,found,202) && !std::memcmp(found,b,6),
        "a validated DHCP ACK can authoritatively reassign a previously leased address");
}
void testClientCapacityAndSlotReuse() {
  Fixture f;
  const uint8_t c[6]{2,0,0,0,0,6}, d[6]{2,0,0,0,0,7}, e[6]{2,0,0,0,0,8};
  check(f.core.setAssociated(c,true) && f.core.setAssociated(d,true),"four clients fit the bounded table");
  check(!f.core.setAssociated(e,true),"a fifth client cannot exceed table capacity");
  f.learn(a,0xc0a80149); f.learn(b,0xc0a8014a); f.learn(c,0xc0a8014b); f.learn(d,0xc0a8014c);
  BridgeClientAddress clients[4];
  check(f.core.clients(clients,4,101)==4,"snapshot retains all four distinct clients");
  check(f.core.setAssociated(c,false) && f.core.setAssociated(e,true),"departed client's slot is reusable");
  f.learn(e,0xc0a8014d);
  uint8_t found[6];
  check(f.core.clients(clients,4,102)==4 && !f.core.macForAddress(0xc0a8014b,found,102) &&
        f.core.macForAddress(0xc0a8014d,found,102) && !std::memcmp(found,e,6),
        "slot reuse removes old identity and retains new address");
}
}
int main() { testBidirectionalIpv4AndOwnTargetLookup(); testArpProxyAndNoPhantomClients(); testDhcpIdentityAndLeaseLifecycle(); testMalformedFramesAndReset(); testDhcpCanReassignAnAddress(); testClientCapacityAndSlotReuse(); std::puts("ipv4_bridge_core_test: passed"); }
