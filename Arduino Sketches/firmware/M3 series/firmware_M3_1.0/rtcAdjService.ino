// -----------------------------------------------------------------------------
// ----------------------------- AJUSTE DO RTC ---------------------------------
// -----------------------------------------------------------------------------

// NTP
bool ntpRequest() {
  // exemplo: configure uma vez; se já estiver configurado, pode retornar true
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.google.com");
  return true;
}

//Chegar se NTP já forneceu hora válida
bool ntpGet(uint32_t &unixOut) {
  time_t t = time(nullptr);

  // Critério: ainda não sincronizou se for muito pequeno
  if (t < rtcSvc.UNIX_MIN_2000 || rtcSvc.UNIX_MAX_2068) return false;

  unixOut = (uint32_t)t;
  return true;
}

bool rtcCb (Modbus::ResultCode event, uint16_t transactionId, void* data) {
// Essa função é essencial para o funcionamento e não só para comunicação,
  char buffer[128];

  if (event == Modbus::EX_SUCCESS) {
    rtcSvc.modbusOk = true; 
    strcpy(buffer, "LOG: Leitura do registrador SYS_TIME OK.");
  } else {
    rtcSvc.modbusOk = false;
    snprintf(buffer, sizeof(buffer), "ERR: Erro na leitura do registrador SYS_TIME: 0x%02X. %s", event, resultCode(event));
  }

  // Verifica primeiro se o client está online antes de enviar a mensagem
  if (wsClient != 255) {
    ws.sendTXT(wsClient, buffer);
  }

  return true;
}

// Dispara leitura Modbus (assíncrono)
bool modbusRequestInverterTime() {

  // Não dispara se já tem transação em andamento
  if (rtu.slave()) return false;

  // Dispara a leitura de 2 registradores (SYS TIME: 32 bits)
  if (!rtu.readHreg(SLAVE_ID, HR_SYS_TIME, sys_time_regs, 2, rtcCb)){
    // Não conseguiu enfileirar (ocupado)
    return false;
  }

  return true;
}

// Pega os dados e valida se veio OK
bool modbusTimeGet() {

  // Montagem do int32_t
  uint32_t s = ((uint32_t)sys_time_regs[1] << 16) | sys_time_regs[0];

  if (s < rtcSvc.UNIX_MIN_2000 || s > rtcSvc.UNIX_MAX_2068) return false;

  rtcSvc.invTime = s;
  return true; // Sucesso em obter os dados
}

bool rtcSet(uint32_t unixTime, uint32_t tol) {
  // Escreve no RTC
  rtc.adjust(DateTime(unixTime));

  // Realiza leitura para verificar se deu certo
  uint32_t r1 = rtc.now().unixtime();
  int32_t d = (int32_t)r1 - (int32_t)unixTime;
  d = (d < 0) ? -d : d;

  if(d <= tol) return true;

  // Tentativa 2, caso a primeira der errado
  uint32_t r2 = rtc.now().unixtime();
  d = (int32_t)r2 - (int32_t)unixTime;

  return (d <= tol);
}

bool rtcAdjustTick(RtcAdjustService &svc, uint32_t now) {
  // Retorna true quando finaliza (DONE ou ERROR), para agir no status principal

  switch (svc.state) {

    case RtcAdjState::IDLE:
      // Essa rotina prepara o RTC para ser ajustado
      if (svc.pending) {
        svc.pending = false;

        // zera os resultados
        svc.modbusOk = false;
        svc.dataOk = false;
        svc.rtcOk = false;

        svc.ntpOk = false;
        svc.ntpDone = false;
        svc.ntpRetries = 0;

        svc.state = RtcAdjState::REQUEST_NTP;
        svc.stepStartMs = now;
      }
      break;

// --------------------------------- NTP ---------------------------------------

    case RtcAdjState::REQUEST_NTP:
      if(ntpRequest()) {
        svc.state = RtcAdjState::WAIT_NTP;
        svc.stepStartMs = now;
      } else {
        // se nem conseguiu iniciar NTP, já faz o fallback
        svc.state = RtcAdjState::REQUEST_MODBUS;
        svc.stepStartMs = now;
      }
      break;

    case RtcAdjState::WAIT_NTP:
      // tenta obter um horário válido
      if (!svc.ntpDone) {
        uint32_t t;
        if(ntpGet(t)) {
          svc.ntpTime = t;
          svc.ntpOk = true;
          svc.ntpDone = true;
        }
      }

      if (svc.ntpDone && svc.ntpOk) {
        // NTP OK --> Ajuste RTC e encerra
        svc.invTime = svc.ntpTime;
        svc.state = RtcAdjState::SET_RTC;
        svc.stepStartMs = now;
        break;
      }

      // Timeout do NTP -> fallback para Modbus
      if (stepTimedOut(now, svc.stepStartMs, svc.ntpTimeoutMs)) {
        if(++svc.ntpRetries <= svc.ntpMaxRetries){
          //Tenta NTP mais uma vez
          svc.state = RtcAdjState::REQUEST_NTP;
          svc.stepStartMs = now;
        } else {
          //fallback
          svc.state = RtcAdjState::REQUEST_MODBUS;
          svc.stepStartMs = now;
        }
      }
      break;

    case RtcAdjState::REQUEST_MODBUS:
      // Essa rotina é executada para gerar a requisição de leitura ao inversor

      if(modbusRequestInverterTime()) {
        svc.state = RtcAdjState::WAIT_MODBUS;
        svc.stepStartMs = now;
      }
      // Em caso de erro na leitura, a função de requisição irá repetir até a leitura das certo ou chegar ao timeout
      else if (stepTimedOut(now, svc.stepStartMs, svc.stepTimeoutMs)) {
        svc.modbusOk = false;
        svc.state = RtcAdjState::ERROR;
        return true;
      }
      break;

    case RtcAdjState::WAIT_MODBUS:

      if(svc.modbusOk) {
        svc.dataOk = modbusTimeGet();
        if(!svc.dataOk) {
          svc.state = RtcAdjState::ERROR;
          return true;
        }
        svc.state = RtcAdjState::SET_RTC;
        svc.stepStartMs = now;
      }
      // Caso a leitura não tenha terminado, o timeout será monitorado. 
      else if (stepTimedOut(now, svc.stepStartMs, svc.stepTimeoutMs)) {
        svc.modbusOk = false;
        svc.dataOk = false;
        svc.state = RtcAdjState::ERROR;
        return true;
      }
      break;

// --------------- SET/VERIFY ----------------------
    case RtcAdjState::SET_RTC:
      svc.rtcOk = rtcSet(svc.invTime, svc.verifyToleranceSec); 
      if(!svc.rtcOk){
        svc.state = RtcAdjState::ERROR;
        return true;
      }

      svc.state = RtcAdjState::DONE;
      return true;

    case RtcAdjState::DONE:
      return true;

    case RtcAdjState::ERROR:
      return true;
  }
    return false; // Indica que ainda está em andamento
}