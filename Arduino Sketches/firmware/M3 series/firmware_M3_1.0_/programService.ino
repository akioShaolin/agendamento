// -----------------------------------------------------------------------------
// ------------------------- CHECAGEM DE PROGRAMAS -----------------------------
// -----------------------------------------------------------------------------

// Callback específico do ProgramService (usa ponteiro 'data')
bool programCb(Modbus::ResultCode event, uint16_t /*transactionId*/, void* data) {
  auto* svc = static_cast<ProgramService*>(data);
  if (!svc) return true;

  svc->modbusDone = true;
  svc->modbusOk = (event == Modbus::EX_SUCCESS);

  // Log opcional
  if (wsClient != 255) {
    char buffer[128];
    if (svc->modbusOk) {
      strcpy(buffer, "LOG: Modbus OK (program). ");
    } else {
      snprintf(buffer, sizeof(buffer), "ERR: Modbus(program): 0x%02X. %s", event, resultCode(event));
    }
    ws.sendTXT(wsClient, buffer);
  }
  return true;
}

bool modbusReadPwrLim(ProgramService& svc) {
  // Não dispara se já tem transação em andamento
  if (rtu.slave()) return false;

  svc.modbusDone = false;
  svc.modbusOk = false;

  // Dispara a leitura de 1 registrador (ACTIVE POWER LIMIT VALUE: 16 bits)
  if (!rtu.readHreg(SLAVE_ID, HR_ACT_PWR_LIM_VL, &svc.pwr_lim_reg, 1, programCb)){
    // Não conseguiu enfileirar (ocupado)
    return false;
  }
  svc.stepStartMs = millis();
  return true;
}

bool modbusWritePwrLim(ProgramService& svc) {
  // Não dispara se já tem transação em andamento
  if (rtu.slave()) return false;

  svc.modbusDone = false;
  svc.modbusOk = false;
  // Dispara a escrita de 1 registrador (ACTIVE POWER LIMIT VALUE: 16 bits)
  if (!rtu.writeHreg(SLAVE_ID, HR_ACT_PWR_LIM_VL, svc.desired_reg, programCb)){
    // Não conseguiu enfileirar (ocupado)
    return false;
  }
  svc.stepStartMs = millis();
  return true;
}

uint16_t powerW_to_limitReg(uint32_t powerW) {
  // Scale 10 [kW] => unidade = 0,1 kW = 100 W
  uint32_t reg = (powerW + 50) / 100;   // arredonda para o inteiro mais próximo (opcional)
  if (reg > 65535) reg = 65535;         // só por segurança
  return (uint16_t)reg;
}

bool needAdjust(uint16_t currentReg, uint16_t desiredReg, uint16_t tol = 0) {
  // tol = 0 -> precisa ser igual; pode usar a tolerância se fizer sentido
  int32_t d = (int32_t)currentReg - (int32_t)desiredReg;
  if (d < 0) d = -d;
  return(uint16_t)d > tol;
}

bool ProgramTick(ProgramService &svc, DateTime dt) {

  const uint32_t nowMs = millis();

  switch (svc.state) {

    case ProgramState::IDLE:
      if (svc.pending) {
        svc.pending = false;
        svc.state = ProgramState::EVALUATE_PROGRAMS;
        svc.retries = 0;
      }
      return false;

    case ProgramState::EVALUATE_PROGRAMS: {
      
      Program p;
      bool found = false;

      uint8_t today = dt.dayOfTheWeek(); // 0 - Domingo ~ 6 - Sábado
      uint16_t nowMins = dt.hour() * 60 + dt.minute();

      // Percorre todos os programas da PROGMEM
      for (int i = 0; i < NUM_PROGRAMS; i++) {
        memcpy_P(&p, &programs[i], sizeof(Program));    

        // Filtra por programa ativo
        if (!p.enabled) continue;

        // Filtra por mês (se configurado)
        if (p.month != 0 && dt.month() != p.month) continue;

        // Filtra por dia da semana (se configurado)
        bool dayMatch = false;
        if(p.dayOfWeek <= 6) {
          dayMatch = (today == p.dayOfWeek);
        }
        else if (p.dayOfWeek == 10) {
          // Dias úteis (Segunda a Sexta)
          dayMatch = (today >= 1 && today <= 5);
        }
        else if (p.dayOfWeek == 11) {
          dayMatch = (today == 0 || today == 6);
        }

        if (!dayMatch) continue;

        // Contas feitas em minutos do dia
        uint16_t startMins = p.startHour * 60 + p.startMinute;
        uint16_t endMins = p.endHour * 60 + p.endMinute;

        if (nowMins >= startMins && nowMins < endMins) {
          found = true;
          break; // Apenas um programa ativo por vez
        }
      }

      if (!found) {
        memcpy_P(&p, &defaultProgram, sizeof(Program));
      }

      svc.currentProgram = p;
      svc.desired_reg = powerW_to_limitReg(svc.currentProgram.power);

      svc.op = ProgramOp::READ_LIMIT;
      svc.retries = 0;
      svc.state = ProgramState::REQUEST_MODBUS;
      return false;
    }

    case ProgramState::REQUEST_MODBUS: {
      bool started = false;
      // Em seguida, essa rotina é executada para gerar a requisição de leitura ao inversor
      
      if (svc.op == ProgramOp::READ_LIMIT || svc.op == ProgramOp::VERIFY_READ) {
        started = modbusReadPwrLim(svc);
      } else if (svc.op == ProgramOp::WRITE_LIMIT) {
        started = modbusWritePwrLim(svc);
      }

      // Em caso de erro na leitura ou escrita, a função de requisição irá repetir até a leitura das certo ou chegar ao timeout
      if (started) {
        svc.state = ProgramState::WAIT_MODBUS;
      } else {
        // não conseguiu nem enfileirar (ocupado): não conta como falha; tenta de novo no próximo loop
        // opcional: se ficar ocupado tempo demais, trate como timeout também
      }
      return false;
    }

    case ProgramState::WAIT_MODBUS:

      if(svc.modbusDone) {
        if(!svc.modbusOk){
          // falhou -> retry/erro
          if(++svc.retries > svc.maxRetries) {
            svc.state = ProgramState::ERROR;
            return true;
          }
          svc.state = ProgramState::REQUEST_MODBUS; // Tenta de novo a mesma operação
          return false;
        }

        //sucesso: decide próximo passo
        if (svc.op == ProgramOp::READ_LIMIT) {
          if (!needAdjust(svc.pwr_lim_reg, svc.desired_reg, 1)) {
            svc.state = ProgramState::DONE; // "Need Adj? = N"
            return true;
          }
          svc.op = ProgramOp::WRITE_LIMIT; // "Need Adj? = Y" -> Write
          svc.retries = 0;
          svc.state = ProgramState::REQUEST_MODBUS;
          return false;        
        }

        if (svc.op == ProgramOp::WRITE_LIMIT) {
          svc.op = ProgramOp::VERIFY_READ;
          svc.retries = 0;
          svc.state = ProgramState::REQUEST_MODBUS;
          return false;
        }

        if (svc.op == ProgramOp::VERIFY_READ) {
          if(!needAdjust(svc.pwr_lim_reg, svc.desired_reg, 1)) {
            svc.state = ProgramState::DONE;
            return true;
          }
          // escreveu, mas não confirmou -> retry controlado
          if (++svc.retries > svc.maxRetries) {
            svc.state = ProgramState::ERROR;
            return true;
          }
          svc.op = ProgramOp::WRITE_LIMIT;
          svc.state = ProgramState::REQUEST_MODBUS;
          return false;
        }
      }

      // ainda não terminou: checa timeout da etapa

      if (stepTimedOut(nowMs, svc.stepStartMs, svc.stepTimeoutMs)) {
        if (++svc.retries > svc.maxRetries) {
          svc.state = ProgramState::ERROR;
          return true;
        }
        svc.state = ProgramState::REQUEST_MODBUS; //repete a mesma op
      }
      return false;

    case ProgramState::DONE:
      return true;

    case ProgramState::ERROR:
      return true;
  }

  return false;
}