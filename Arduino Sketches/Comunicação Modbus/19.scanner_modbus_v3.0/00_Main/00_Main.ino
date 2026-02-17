// =======================
// Scanner Modbus - v2
// Parte 1: Scan Slaves (já funcionando)
// Parte 2: Ler 1 registrador/bit (read-once)
// =======================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "RTClib.h"
#include <ModbusRTU.h>

// --- WiFi manager (já existente no seu WiFi_Manager.ino)
void wifiManagerBegin(ESP8266WebServer& server, const char* apSsid, const char* apPass, uint32_t staTimeoutMs = 12000);

// --- HTML
extern const char HTML_PAGE[] PROGMEM;

// --- Objetos globais
RTC_DS1307 rtc;
ModbusRTU rtu;
ESP8266WebServer server(80);
WebSocketsServer ws(81);

// --- Config AP
const char* AP_SSID = "Scanner Modbus";
const char* AP_PASS = "1234567890";

// --- Pinos
#define LED_PIN           LED_BUILTIN
#define DE_RE             12
#define BTN_PIN           0
#define HALF_OR_FULL_PIN  13

// --- Modbus
#define RS485_BAUD 9600
static const uint16_t MODBUS_TIMEOUT_MS = 350;

// --- WS client
uint8_t wsClient = 255;

// --- JSON doc
StaticJsonDocument<768> doc;

// --- RTC ok?
bool rtcOk = false;

// ======= Forward decls (WS utils / scan / read / router) =======
String tsNow();
void wsLog(const String& msg);
void wsErr(const String& msg);
void wsEvt(const String& json);

void scanTaskLoop();
void readTaskLoop();

void scanSlavesStop(); // usado ao desconectar WS (opcional)
void readStop();       // usado ao desconectar WS (opcional)

void wsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

// --- Rotas
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void setup() {
  Serial.begin(115200);

  // Modbus master
  rtu.master();
  Serial.begin(RS485_BAUD);
  rtu.begin(&Serial, DE_RE);

  // WiFi: AP sempre + STA opcional + /wifi
  wifiManagerBegin(server, AP_SSID, AP_PASS, 12000);

  // HTTP
  server.on("/", handleRoot);
  server.begin();

  // WS
  ws.begin();
  ws.onEvent(wsEvent);

  // I2C + RTC
  Wire.begin(4, 5);

  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);

  digitalWrite(HALF_OR_FULL_PIN, LOW);  // Half duplex
  digitalWrite(LED_PIN, LOW);

  delay(300);

  rtcOk = false;
  for (int i = 0; i < 5; i++) {
    if (rtc.begin()) { rtcOk = true; break; }
    delay(150);
  }
}

void loop() {
  ws.loop();
  server.handleClient();
  rtu.task();
  yield();

  // FSMs
  scanTaskLoop();
  readTaskLoop();
}
