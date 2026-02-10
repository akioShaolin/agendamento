// -----------------------------------------------------------------------------
// -------------------------- LEITOR DE POTÊNCIA -------------------------------
// -----------------------------------------------------------------------------

bool readPowerCb (Modbus::ResultCode event, uint16_t transactionId, void* data) {
// Essa função é essencial para o funcionamento e não só para comunicação,
  char buffer[128];

  // cb da leitura de potência
  if (event == Modbus::EX_SUCCESS) {
    readPowerSvc.modbusOk = true;
    strcpy(buffer, "LOG: Leitura do registrador ACT_PWR_OUT OK.");
  } else {
    readPowerSvc.modbusOk = false;
    snprintf(buffer, sizeof(buffer), "ERR: Erro na leitura do registrador ACT_PWR_OUT: 0x%02X. %s", event, resultCode(event));
  }

  // Verifica primeiro se o client está online antes de enviar a mensagem
  if (wsClient != 255) {
    ws.sendTXT(wsClient, buffer);
  }
  return true;
}

// Dispara leitura modbus (assincrono)
bool modbusRequestPower() {

  // Não dispara se já tem transação em andamento
  if (rtu.slave()) return false;

  // Dispara a leitura de 2 registradores (ACTIVE POWER OUT: 16 bits)
  if (!rtu.readHreg(SLAVE_ID, HR_ACT_PWR_OUT, &power_reg, 1, readPowerCb)){
    // Não conseguiu enfileirar (ocupado)
    return false;
  }

  return true;
}

bool modbusPowerGet() {

  // Ajusta a escala e converte para float
  float p = (float)power_reg / 10;

  readPowerSvc.power = p;
  return true;
}

bool logPower(float power) {
  if (wsClient == 255) return false;

  char buffer[128];

  sprintf(buffer, "{\"power\": \"%.1f\"}", power);
  ws.sendTXT(wsClient, buffer);
  return true;
}

bool readPowerTick(ReadPowerService &svc, uint32_t now) {

  switch (svc.state) {

    case ReadPowerState::IDLE:
      if (svc.pending) {
        svc.pending = false;
        svc.state = ReadPowerState::REQUEST_MODBUS;
        svc.modbusOk = false;
        svc.dataOk = false;
        svc.logOk = false;
        svc.stepStartMs = now;
      }
      break;

    case ReadPowerState::REQUEST_MODBUS:
      // Em seguida, essa rotina é executada para gerar a requisição de leitura ao inversor
      if(modbusRequestPower()) {
        svc.state = ReadPowerState::WAIT_MODBUS;
        svc.stepStartMs = now;
        return true;
      }
      // Em caso de erro na leitura, a função de requisição irá repetir até a leitura das certo ou chegar ao timeout
      else if (stepTimedOut(now, svc.stepStartMs, svc.stepTimeoutMs)) {
        svc.modbusOk = false;
        svc.state = ReadPowerState::ERROR;
        return true;
      }
      break;

    case ReadPowerState::WAIT_MODBUS:

      if(svc.modbusOk) {
        svc.dataOk = modbusPowerGet();
        if(!svc.dataOk) {
          svc.state = ReadPowerState::ERROR;
          return true;
        }
        svc.state = ReadPowerState::LOG;
        svc.stepStartMs = now;
      }
      // Caso a leitura não tenha terminado, o timeout será monitorado. 
      else if (stepTimedOut(now, svc.stepStartMs, svc.stepTimeoutMs)) {
        svc.modbusOk = false;
        svc.dataOk = false;
        svc.state = ReadPowerState::ERROR;
        return true;
      }
      break;

    case ReadPowerState::LOG:
      svc.logOk = logPower(svc.power);
      if(!svc.logOk){
        svc.state = ReadPowerState::ERROR;
        return true;
      }

      svc.state = ReadPowerState::DONE;
      return true;
      break;

    case ReadPowerState::DONE:
      return true;

    case ReadPowerState::ERROR:
      return true;
  }
  return false; // Indica que ainda está em andamento
}
