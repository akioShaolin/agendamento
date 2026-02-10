// -----------------------------------------------------------------------------
// -------------------------- LEITOR DE POTÊNCIA -------------------------------
// -----------------------------------------------------------------------------

// Callback específico do ReadPowerService (usa ponteiro 'data')
bool readPowerCb(Modbus::ResultCode event, uint16_t /*transactionId*/, void* data) {
  auto* svc = static_cast<ReadPowerService*>(data);
  if (!svc) return true;

  svc->modbusDone = true;
  svc->modbusOk = (event == Modbus::EX_SUCCESS);

  // Log opcional
  if (wsClient != 255) {
    char buffer[128];
    if (svc->modbusOk) {
      strcpy(buffer, "LOG: Leitura do registrador ACT_PWR_OUT OK.");
    } else {
      snprintf(buffer, sizeof(buffer), "ERR: ACT_PWR_OUT: 0x%02X. %s", event, resultCode(event));
    }
    ws.sendTXT(wsClient, buffer);
  }
  return true;
}

// Dispara leitura modbus (assincrono)
bool modbusRequestPower(ReadPowerService &svc) {

  // Não dispara se já tem transação em andamento
  if (rtu.slave()) return false;

  svc.modbusDone = false;
  svc.modbusOk = false;

  // Dispara a leitura de 1 registrador (HR_ACT_PWR_OUT: 16 bits)
  if (!rtu.readHreg(SLAVE_ID, HR_ACT_PWR_OUT, &power_reg, 1, readPowerCb)){
    // Não conseguiu enfileirar (ocupado)
    return false;
  }

  return true;
}

bool modbusPowerGet(ReadPowerService &svc) {
  // Ajusta a escala e converte para kW
  svc.power = (float)power_reg / 10.0f;
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
        svc.modbusDone = false;
        svc.dataOk = false;
        svc.logOk = false;
        svc.stepStartMs = now;
      }
      break;

    case ReadPowerState::REQUEST_MODBUS:
      // Em seguida, essa rotina é executada para gerar a requisição de leitura ao inversor
      if(modbusRequestPower(svc)) {
        svc.state = ReadPowerState::WAIT_MODBUS;
        svc.stepStartMs = now;
      }
      // Em caso de erro na leitura, a função de requisição irá repetir até a leitura das certo ou chegar ao timeout
      else if (stepTimedOut(now, svc.stepStartMs, svc.stepTimeoutMs)) {
        svc.modbusOk = false;
        svc.state = ReadPowerState::ERROR;
        return true;
      }
      break;

    case ReadPowerState::WAIT_MODBUS:

      if(svc.modbusDone) {
        if(!svc.modbusOk) {
          svc.state = ReadPowerState::ERROR;
          return true;
        }
        svc.dataOk = modbusPowerGet(svc);
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
