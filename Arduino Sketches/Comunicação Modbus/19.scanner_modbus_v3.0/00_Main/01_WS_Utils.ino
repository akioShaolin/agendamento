#include <Arduino.h>
#include "RTClib.h"
#include <WebSocketsServer.h>

extern RTC_DS1307 rtc;
extern bool rtcOk;
extern WebSocketsServer ws;
extern uint8_t wsClient;

String tsNow() {
  if (rtcOk) {
    DateTime n = rtc.now();
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d:%02d", n.hour(), n.minute(), n.second());
    return String(b);
  }
  uint32_t s = millis() / 1000;
  uint8_t hh = (s / 3600) % 24;
  uint8_t mm = (s / 60) % 60;
  uint8_t ss = s % 60;
  char b[16];
  snprintf(b, sizeof(b), "%02u:%02u:%02u", hh, mm, ss);
  return String(b);
}

void wsLog(const String& msg) {
  if (wsClient == 255) return;
  String m = "LOG: [" + tsNow() + "] " + msg;
  ws.sendTXT(wsClient, m);
}

void wsErr(const String& msg) {
  if (wsClient == 255) return;
  String m = "ERR: [" + tsNow() + "] " + msg;
  ws.sendTXT(wsClient, m);
}

void wsEvt(const String& json) {
  if (wsClient == 255) return;
  String m = "EVT: " + json;   // cria um String "de verdade" (lvalue)
  ws.sendTXT(wsClient, m);     // agora passa String&
}
