#include <Arduino.h>
#include <ModbusRTU.h>

extern ModbusRTU rtu;
extern const uint16_t MODBUS_TIMEOUT_MS;

void wsLog(const String& msg);
void wsErr(const String& msg);
void wsEvt(const String& json);

// ================== FSM Scan Slaves ==================
enum ScanTaskState : uint8_t {
  SCAN_IDLE = 0,
  SCAN_SEND,
  SCAN_WAIT
};

static ScanTaskState scanState = SCAN_IDLE;
static bool scanRunning = false;

static uint8_t scanIdStart = 1;
static uint8_t scanIdEnd   = 247;
static uint8_t scanIdCur   = 1;

static uint16_t scanTestReg = 0;
static uint8_t  scanTestFn  = 3;

static uint16_t scanRegBuf[1] = {0};
static bool     scanBitBuf[1] = {false};

static uint32_t scanTimer = 0;

static volatile bool cbGot = false;
static volatile uint8_t cbLastRC = 0;

static bool cbScan(Modbus::ResultCode rc, uint16_t, void*) {
  cbGot = true;
  cbLastRC = (uint8_t)rc;
  return true;
}

void scanSlavesStart(uint8_t idS, uint8_t idE, uint16_t tReg, uint8_t tFn) {
  scanIdStart = idS;
  scanIdEnd   = idE;
  scanIdCur   = scanIdStart;
  scanTestReg = tReg;
  scanTestFn  = tFn;

  cbGot = false;
  scanRunning = true;
  scanState = SCAN_SEND;

  wsLog("Busca de slaves iniciada.");
  wsEvt("{\"type\":\"scan-status\",\"level\":\"ok\",\"text\":\"Executando...\",\"hint\":\"Varredura em andamento.\",\"lastOutput\":\"Buscando slaves...\"}");
}

void scanSlavesStop() {
  if (!scanRunning) return;
  scanRunning = false;
  scanState = SCAN_IDLE;

  wsLog("Busca de slaves interrompida.");
  wsEvt("{\"type\":\"scan-status\",\"level\":\"bad\",\"text\":\"Interrompido\",\"hint\":\"Busca cancelada.\",\"lastOutput\":\"Busca interrompida.\"}");
  wsEvt("{\"type\":\"scan-done\"}");
}

static void scanSend() {
  if (scanIdCur > scanIdEnd) {
    scanRunning = false;
    scanState = SCAN_IDLE;
    wsLog("Busca de slaves finalizada.");
    wsEvt("{\"type\":\"scan-status\",\"level\":\"ok\",\"text\":\"Concluído\",\"hint\":\"Busca finalizada.\",\"lastOutput\":\"Busca concluída.\"}");
    wsEvt("{\"type\":\"scan-done\"}");
    return;
  }

  if (rtu.slave()) return;
  cbGot = false;

  bool ok = false;
  switch (scanTestFn) {
    case 1: ok = rtu.readCoil(scanIdCur, scanTestReg, scanBitBuf, 1, cbScan); break;
    case 2: ok = rtu.readIsts(scanIdCur, scanTestReg, scanBitBuf, 1, cbScan); break;
    case 3: ok = rtu.readHreg(scanIdCur, scanTestReg, scanRegBuf, 1, cbScan); break;
    case 4: ok = rtu.readIreg(scanIdCur, scanTestReg, scanRegBuf, 1, cbScan); break;
    default: ok = rtu.readHreg(scanIdCur, scanTestReg, scanRegBuf, 1, cbScan); break;
  }

  if (!ok) {
    wsErr("Modbus ocupado ao enviar (scan).");
    return;
  }

  scanTimer = millis();
  scanState = SCAN_WAIT;

  char st[64];
  snprintf(st, sizeof(st), "Testando ID %u ...", scanIdCur);
  wsEvt(String("{\"type\":\"scan-status\",\"level\":\"ok\",\"text\":\"") + st + String("\",\"hint\":\"Se a rede estiver ruidosa, aumente timeout.\",\"lastOutput\":\"Varredura...\"}"));
}

static void scanWait() {
  if (!rtu.slave()) {
    if (cbGot) {
      const bool isSuccess = (cbLastRC == (uint8_t)Modbus::EX_SUCCESS);
      String status = isSuccess ? "RESP" : "EXC";

      char det[64];
      if (isSuccess) snprintf(det, sizeof(det), "OK fn=%u reg=%u", scanTestFn, scanTestReg);
      else snprintf(det, sizeof(det), "RC=0x%02X fn=%u reg=%u", cbLastRC, scanTestFn, scanTestReg);

      wsEvt(String("{\"type\":\"slave-found\",\"id\":") + scanIdCur +
            String(",\"status\":\"") + status +
            String("\",\"detail\":\"") + det + String("\"}"));
    }

    scanIdCur++;
    scanState = SCAN_SEND;
    return;
  }

  if (millis() - scanTimer > MODBUS_TIMEOUT_MS) {
    cbGot = false;
    wsLog(String("ID ") + scanIdCur + " sem resposta (timeout).");
    scanIdCur++;
    scanState = SCAN_SEND;
  }
}

void scanTaskLoop() {
  if (!scanRunning) return;
  if (scanState == SCAN_SEND) scanSend();
  else if (scanState == SCAN_WAIT) scanWait();
}
