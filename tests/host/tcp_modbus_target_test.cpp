#include <cstdio>
#include <cstdlib>
#include "TcpModbusCycleSource.h"
void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
  TcpModbusCycleSource source; ModbusReadCycle cycle{};
  check(!source.fetch(cycle)&&WiFiClient::destinations.empty(),"unknown Venus address never initiates a connection");
  source.setAddress(0xc0a80149); check(source.fetch(cycle)&&cycle.requiredValid,"learned address supplies successful Modbus data");
  check(WiFiClient::destinations.size()==1&&WiFiClient::destinations.back()=="192.168.1.73"&&WiFiClient::ports.back()==502,"Modbus targets learned Venus address");
  source.setAddress(0xc0a80149);check(source.fetch(cycle)&&WiFiClient::destinations.size()==1,"unchanged address retains healthy connection");
  source.setAddress(0xc0a83264);check(source.fetch(cycle)&&WiFiClient::destinations.size()==2&&WiFiClient::destinations.back()=="192.168.50.100","new lease closes old socket and reconnects to new address");
  source.setAddress(0);check(!source.fetch(cycle)&&!cycle.requiredValid&&WiFiClient::destinations.size()==2,"removed lease stops polling stale endpoint");
  WiFiClient::connectSucceeds=false;source.setAddress(0xc0a80149);
  check(!source.fetch(cycle)&&WiFiClient::destinations.size()==3,"unreachable candidate has one bounded connection attempt per cycle");
  std::puts("tcp_modbus_target_test: passed");
}
