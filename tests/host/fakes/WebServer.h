#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Arduino.h"
#include "IPAddress.h"

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE };

class FakeWebServerClient {
 public:
  IPAddress remoteIP() const { return remoteAddress_; }
  void setRemoteIP(const IPAddress& address) { remoteAddress_ = address; }

 private:
  IPAddress remoteAddress_;
};

class WebServer {
 public:
  using THandlerFunction = std::function<void()>;

  explicit WebServer(int port = 80) : port_(port) { lastInstance_ = this; }

  void on(const char* uri, HTTPMethod method, THandlerFunction handler) {
    routes_.push_back({uri == nullptr ? "" : uri, method, std::move(handler)});
  }

  void onNotFound(THandlerFunction handler) { notFoundHandler_ = std::move(handler); }

  void begin() {
    running_ = true;
    ++beginCalls_;
  }

  void stop() {
    running_ = false;
    ++stopCalls_;
  }

  void handleClient() { ++handleClientCalls_; }

  FakeWebServerClient& client() { return client_; }

  bool hasArg(const String& name) const {
    const std::string key(name.c_str());
    for (const auto& entry : arguments_) {
      if (entry.first == key) return true;
    }
    return false;
  }

  String arg(const String& name) const {
    const std::string key(name.c_str());
    for (const auto& entry : arguments_) {
      if (entry.first == key) return String(entry.second);
    }
    return String();
  }

  void send(int code, const char* contentType, const String& content) {
    responseCode_ = code;
    responseContentType_ = contentType == nullptr ? "" : contentType;
    responseBody_ = content.c_str();
  }

  bool simulateRequest(HTTPMethod method, const char* uri, const IPAddress& remoteAddress,
                       const std::vector<std::pair<std::string, std::string>>& arguments = {}) {
    if (!running_) return false;
    responseCode_ = 0;
    responseContentType_.clear();
    responseBody_.clear();
    arguments_ = arguments;
    client_.setRemoteIP(remoteAddress);
    const std::string requestedUri = uri == nullptr ? "" : uri;
    for (const Route& route : routes_) {
      if (route.uri == requestedUri && route.method == method) {
        route.handler();
        return true;
      }
    }
    if (notFoundHandler_) {
      notFoundHandler_();
      return true;
    }
    return false;
  }

  static WebServer* lastInstance() { return lastInstance_; }
  bool running() const { return running_; }
  int port() const { return port_; }
  int beginCalls() const { return beginCalls_; }
  int stopCalls() const { return stopCalls_; }
  int handleClientCalls() const { return handleClientCalls_; }
  int responseCode() const { return responseCode_; }
  const std::string& responseContentType() const { return responseContentType_; }
  const std::string& responseBody() const { return responseBody_; }

 private:
  struct Route {
    std::string uri;
    HTTPMethod method;
    THandlerFunction handler;
  };

  inline static WebServer* lastInstance_ = nullptr;
  int port_ = 80;
  bool running_ = false;
  int beginCalls_ = 0;
  int stopCalls_ = 0;
  int handleClientCalls_ = 0;
  int responseCode_ = 0;
  std::string responseContentType_;
  std::string responseBody_;
  std::vector<Route> routes_;
  std::vector<std::pair<std::string, std::string>> arguments_;
  THandlerFunction notFoundHandler_;
  FakeWebServerClient client_;
};
