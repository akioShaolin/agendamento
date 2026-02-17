#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>

extern WebSocketsServer ws;
extern uint8_t wsClient;
extern StaticJsonDocument<768> doc;

void wsLog(const String& msg);
void wsErr(const String& msg);

void scanSlavesStart(uint8_t idS, uint8_t idE, uint16_t tReg, uint8_t tFn);
void scanSlavesStop();

void readStartOnce(uint8_t slave, uint8_t fn, uint16_t addr);
void readStop();

void wsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsClient = num;
      wsLog("Cliente conectado.");
      break;

    case WStype_DISCONNECTED:
      if (wsClient == num) wsClient = 255;
      // opcional: parar tarefas ao desconectar
      // scanSlavesStop();
      // readStop();
      break;

    case WStype_TEXT: {
      DeserializationError err = deserializeJson(doc, (char*)payload, length);
      if (err) { ws.sendTXT(num, "ERR: JSON inválido."); return; }

      const char* action = doc["action"] | "";
      if (!action[0]) { ws.sendTXT(num, "ERR: action ausente."); return; }

      // ===== Parte 1: scan slaves =====
      if (strcmp(action, "scan-slaves-start") == 0) {
        uint8_t idS = doc["idStart"] | 1;
        uint8_t idE = doc["idEnd"]   | 247;
        uint16_t tReg = doc["testReg"] | 0;
        uint8_t tFn = doc["testFn"] | 3;

        if (idS < 1) idS = 1;
        if (idE > 247) idE = 247;
        if (idE < idS) { uint8_t tmp = idS; idS = idE; idE = tmp; }
        if (tFn < 1 || tFn > 4) tFn = 3;

        scanSlavesStart(idS, idE, tReg, tFn);
        return;
      }

      if (strcmp(action, "scan-slaves-stop") == 0) {
        scanSlavesStop();
        return;
      }

      // ===== Parte 2: read once =====
      if (strcmp(action, "read-once") == 0) {
        uint8_t slave = doc["slaveId"] | 1;
        uint8_t fn    = doc["fn"] | 3;
        uint16_t addr = doc["addr"] | 0;

        if (slave < 1) slave = 1;
        if (slave > 247) slave = 247;
        if (fn < 1 || fn > 4) fn = 3;

        readStartOnce(slave, fn, addr);
        return;
      }

      if (strcmp(action, "read-stop") == 0) {
        readStop();
        return;
      }

      wsErr(String("Ação desconhecida: ") + action);
    } break;

    default:
      break;
  }
}
