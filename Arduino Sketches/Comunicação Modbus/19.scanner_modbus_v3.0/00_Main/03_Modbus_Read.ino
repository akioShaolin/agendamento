#include <Arduino.h>
#include <ModbusRTU.h>

extern ModbusRTU rtu;
extern const uint16_t MODBUS_TIMEOUT_MS;

void wsLog(const String& msg);
void wsErr(const String& msg);
void wsEvt(const String& json);

// ================== FSM Read Once ==================
enum ReadTaskState : uint8_t {
  READ_IDLE = 0,
  READ_SEND,
  READ_WAIT
};

static ReadTaskState readState = READ_IDLE;
static bool readRunning = false;

static uint8_t  readSlave = 1;
static uint8_t  readFn    = 3;    // 1..4
static uint16_t readAddr  = 0;

static uint16_t readRegBuf[1] = {0};
static bool     readBitBuf[1] = {false};

static uint32_t readTimer = 0;

static volatile bool cbReadGot = false;
static volatile uint8_t cbReadRC = 0;

static bool cbRead(Modbus::ResultCode rc, uint16_t, void*) {
  cbReadGot = true;
  cbReadRC = (uint8_t)rc;
  return true;
}

void readStartOnce(uint8_t slave, uint8_t fn, uint16_t addr) {
  if (readRunning) return;
  readSlave = slave;
  readFn    = fn;
  readAddr  = addr;

  cbReadGot = false;
  readRunning = true;
  readState = READ_SEND;

  wsLog(String("Read once: id=") + readSlave + " fn=" + readFn + " addr=" + readAddr);
  wsEvt("{\"type\":\"read-status\",\"level\":\"ok\",\"text\":\"Enviando...\",\"lastOutput\":\"Leitura iniciada.\"}");
}

void readStop() {
  if (!readRunning) return;
  readRunning = false;
  readState = READ_IDLE;
  wsLog("Read interrompido.");
  wsEvt("{\"type\":\"read-status\",\"level\":\"bad\",\"text\":\"Interrompido\",\"lastOutput\":\"Leitura interrompida.\"}");
  wsEvt("{\"type\":\"read-done\"}");
}

static void readSend() {
  if (rtu.slave()) return; // aguarda lib liberar
  cbReadGot = false;

  bool ok = false;
  switch (readFn) {
    case 1: ok = rtu.readCoil(readSlave, readAddr, readBitBuf, 1, cbRead); break;
    case 2: ok = rtu.readIsts(readSlave, readAddr, readBitBuf, 1, cbRead); break;
    case 3: ok = rtu.readHreg(readSlave, readAddr, readRegBuf, 1, cbRead); break;
    case 4: ok = rtu.readIreg(readSlave, readAddr, readRegBuf, 1, cbRead); break;
    default: ok = rtu.readHreg(readSlave, readAddr, readRegBuf, 1, cbRead); break;
  }

  if (!ok) {
    wsErr("Modbus ocupado ao enviar (read).");
    return;
  }

  readTimer = millis();
  readState = READ_WAIT;

  wsEvt(String("{\"type\":\"read-status\",\"level\":\"ok\",\"text\":\"Aguardando resposta...\",\"lastOutput\":\"id=") +
        readSlave + " fn=" + readFn + " addr=" + readAddr + "\"}");
}

static void readWait() {
  if (!rtu.slave()) {
    // terminou transação
    if (!cbReadGot) {
      wsErr("Read terminou sem callback (tratado como timeout).");
      wsEvt("{\"type\":\"read-status\",\"level\":\"bad\",\"text\":\"Sem callback\",\"lastOutput\":\"Falha.\"}");
      readRunning = false;
      readState = READ_IDLE;
      wsEvt("{\"type\":\"read-done\"}");
      return;
    }

    if (cbReadRC != (uint8_t)Modbus::EX_SUCCESS) {
      char det[48];
      snprintf(det, sizeof(det), "RC=0x%02X", cbReadRC);
      wsErr(String("Read erro: ") + det);
      wsEvt(String("{\"type\":\"read-status\",\"level\":\"bad\",\"text\":\"Erro Modbus\",\"lastOutput\":\"") + det + "\"}");
      readRunning = false;
      readState = READ_IDLE;
      wsEvt("{\"type\":\"read-done\"}");
      return;
    }

    // sucesso
    if (readFn == 1 || readFn == 2) {
      int v = readBitBuf[0] ? 1 : 0;
      wsEvt(String("{\"type\":\"read-result\",\"fn\":") + readFn +
            ",\"id\":" + readSlave +
            ",\"addr\":" + readAddr +
            ",\"kind\":\"bit\",\"value\":" + v + "}");
      wsLog(String("Read OK bit=") + v);
    } else {
      uint16_t v = readRegBuf[0];
      char hexv[8];
      snprintf(hexv, sizeof(hexv), "0x%04X", v);
      wsEvt(String("{\"type\":\"read-result\",\"fn\":") + readFn +
            ",\"id\":" + readSlave +
            ",\"addr\":" + readAddr +
            ",\"kind\":\"reg\",\"value\":" + String(v) +
            ",\"hex\":\"" + hexv + "\"}");
      wsLog(String("Read OK reg=") + String(v) + " (" + hexv + ")");
    }

    wsEvt("{\"type\":\"read-status\",\"level\":\"ok\",\"text\":\"Concluído\",\"lastOutput\":\"Leitura concluída.\"}");
    readRunning = false;
    readState = READ_IDLE;
    wsEvt("{\"type\":\"read-done\"}");
    return;
  }

  if (millis() - readTimer > MODBUS_TIMEOUT_MS) {
    wsErr("Timeout na leitura.");
    wsEvt("{\"type\":\"read-status\",\"level\":\"bad\",\"text\":\"Timeout\",\"lastOutput\":\"Timeout.\"}");
    readRunning = false;
    readState = READ_IDLE;
    wsEvt("{\"type\":\"read-done\"}");
  }
}

void readTaskLoop() {
  if (!readRunning) return;
  if (readState == READ_SEND) readSend();
  else if (readState == READ_WAIT) readWait();
}
