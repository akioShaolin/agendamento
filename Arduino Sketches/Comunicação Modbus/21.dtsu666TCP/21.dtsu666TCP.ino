/*
  ESP8266 - DTSU666 RTU -> espelho em Holding Registers -> Modbus TCP server

  Bibliotecas (Alexander Emelianov):
    - ModbusRTU.h
    - ModbusIP_ESP8266.h

  Estratégia:
    - RTU Master lê o medidor (slave 1) e grava em espelho
    - TCP Server expõe Holding Registers com os MESMOS endereços
*/

#include <ESP8266WiFi.h>
#include <ModbusRTU.h>
#include <ModbusIP_ESP8266.h>

// Definição dos pinos
#define LED_PIN           LED_BUILTIN   //  GPIO2 - Led Built in
#define DE_RE             12            // Pino ligado ao SN75176B (RS485 transceiver)
#define BTN_PIN           0             // Botão da placa com pull up
#define HALF_OR_FULL_PIN  13            // Half-Duplex:LOW  Full-Duplex-HIGH

// -------------------- WiFi --------------------
static const char* WIFI_SSID = "VISITANTES";
static const char* WIFI_PASS = "connection";

IPAddress local_IP(172, 16, 99, 100);
IPAddress gateway (172, 16, 99, 1);
IPAddress subnet  (255, 255, 255, 0);

// -------------------- Modbus --------------------
static const uint8_t  SLAVE_ID = 0x01;

// Registradores / valores
static const uint16_t REG_ID        = 0x002B;
static const uint16_t ID_EXPECTED   = 0x5348;

static const uint16_t REG_CT        = 0x0006;

static const uint16_t REG_A_START   = 0x150A;
static const uint16_t REG_A_LEN     = 26;

static const uint16_t REG_B_START   = 0x181E;
static const uint16_t REG_B_LEN     = 16;

static const uint16_t REG_C_START   = 0x150A;
static const uint16_t REG_C_LEN     = 26;

static const uint16_t REG_D_START   = 0x2044;
static const uint16_t REG_D_LEN     = 2;

// Timings
static const uint32_t WAIT_ID_PERIOD_MS      = 2000;
static const uint32_t VALIDATE_CT_GAP_MS     = 500;
static const uint32_t CYCLIC_PERIOD_MS       = 1000;
static const uint8_t  CYCLIC_MAX_RETRIES     = 2;

// RS485 DE/RE (ajuste conforme seu hardware)
static const uint8_t PIN_RS485_DE_RE = 12; // GPIO5 (D1) EXEMPLO

static const uint32_t RTU_TXN_TIMEOUT_MS = 500; // ajuste: 300~800ms
uint32_t rtuTxnStart = 0;

static const uint16_t REG_STATUS = 0x3000;
static const uint16_t REG_SEQ    = 0x3001;
static const uint16_t REG_AGE_S  = 0x3002;   // idade em segundos (simples)


// -------------------- Objetos --------------------
ModbusRTU mbRTU;
ModbusIP  mbTCP;

// -------------------- Espelho de Holding Regs --------------------
// Em vez de um array gigante, vamos armazenar via mbTCP.Hreg(addr, val).
// (O ModbusIP do Emelianov mantém os Hregs internamente.)

// -------------------- Máquina de estados --------------------
enum SystemState : uint8_t {
  S_WAIT_ID = 0,
  S_VALIDATE_CT,
  S_CYCLIC
};

SystemState state = S_WAIT_ID;

enum LedState {
  LED_OFF = 0,
  LED_WIFI_OK,
  LED_RTU_ERROR,
  LED_ALL_OK
};

LedState ledState = LED_OFF;

// Controle geral de request RTU assíncrona
struct RtuReq {
  bool busy = false;
  uint16_t addr = 0;
  uint16_t len  = 0;
  uint16_t* buf = nullptr;
  bool ok = false;      // resultado da última transação
} rtu;

// Buffers temporários
uint16_t buf1[32];      // suficiente para 26 regs
uint16_t buf2[32];

// Controle S_WAIT_ID
uint32_t tNextWaitId = 0;
uint16_t idStored = 0;

// Controle S_VALIDATE_CT
enum CtPhase : uint8_t { CT_READ1=0, CT_WAIT_GAP, CT_READ2 };
CtPhase ctPhase = CT_READ1;
uint32_t tCtGap = 0;
uint16_t ctVal1 = 0;
uint16_t ctVal2 = 0;

// Controle S_CYCLIC
enum CyStep : uint8_t { CY_A=0, CY_B, CY_C, CY_D, CY_IDCHECK };
CyStep cyStep = CY_A;
uint8_t cyRetries = 0;
uint32_t tNextCycleStart = 0;

// -------------------- Helpers: RS485 direction --------------------
static inline void rs485TxEnable(bool en) {
  digitalWrite(PIN_RS485_DE_RE, en ? HIGH : LOW);
}

// -------------------- Helpers: registrar ranges no TCP --------------------
// O ModbusIP permite reservar ranges: addHreg(start, initVal, count)
void tcpReserveRegs() {
  // ID e CT
  mbTCP.addHreg(REG_ID, 0);
  mbTCP.addHreg(REG_CT, 0);

  // Blocos cíclicos
  mbTCP.addHreg(REG_A_START, 0, REG_A_LEN);
  mbTCP.addHreg(REG_B_START, 0, REG_B_LEN);
  mbTCP.addHreg(REG_D_START, 0, REG_D_LEN);

  mbTCP.addHreg(REG_STATUS, 0);
  mbTCP.addHreg(REG_SEQ, 0);
  mbTCP.addHreg(REG_AGE_S, 0);

  // REG_C_START == REG_A_START, então já está coberto
}

// Escreve no espelho TCP
static inline void mirrorWrite(uint16_t reg, uint16_t val) {
  mbTCP.Hreg(reg, val);
}

// Escreve bloco no espelho TCP
static inline void mirrorWriteBlock(uint16_t start, const uint16_t* data, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    mbTCP.Hreg(start + i, data[i]);
  }
}

// -------------------- RTU request assíncrona --------------------
bool rtuReadHreg(uint16_t addr, uint16_t* dst, uint16_t len) {
  if (rtu.busy) return false;

  rtu.busy = true;
  rtu.addr = addr;
  rtu.len  = len;
  rtu.buf  = dst;
  rtu.ok   = false;

  rtuTxnStart = millis();

  // callback: resultado da transação
  auto cb = [](Modbus::ResultCode event, uint16_t /*transactionId*/, void* data) -> bool {
    (void)data;
    // Nota: aqui acessamos o "rtu" global (simples e direto pro esqueleto)
    rtu.ok = (event == Modbus::EX_SUCCESS);
    rtu.busy = false;
    return true;
  };

  // direção TX é gerenciada pelo callback de preTransmission/postTransmission (abaixo)
  // Aqui só dispara a leitura
  bool started = mbRTU.readHreg(SLAVE_ID, addr, dst, len, cb);

  if (!started) {
    rtu.busy = false;
    rtu.ok = false;
    return false;
  }

  return true;
}

// -------------------- Mudança de estado (reset de variáveis) --------------------
void gotoWaitId() {
  state = S_WAIT_ID;
  tNextWaitId = 0;
  // reset dos controles
  ctPhase = CT_READ1;
  cyStep = CY_A;
  cyRetries = 0;
  tNextCycleStart = 0;
}

// -------------------- Estado 1: WAIT_ID --------------------
void handleWaitId(uint32_t now) {
  if (now < tNextWaitId) return;
  tNextWaitId = now + WAIT_ID_PERIOD_MS;

  // dispara leitura se não houver req pendente
  if (!rtu.busy) {
    if (!rtuReadHreg(REG_ID, buf1, 1)) {
      // não conseguiu iniciar; tenta de novo no próximo tick
      return;
    }
  } else {
    // ainda ocupado
    return;
  }

  // Resultado virá por callback. Precisamos checar no loop quando estiver livre.
  // Então, vamos "sondar" no próximo loop: quando rtu.busy virar false.
}

// -------------------- Estado 2: VALIDATE_CT --------------------
void handleValidateCt(uint32_t now) {
  if (ctPhase == CT_READ1) {
    if (!rtu.busy) {
      if (rtuReadHreg(REG_CT, buf1, 1)) {
        // aguardar callback
        ctPhase = CT_WAIT_GAP; // mas só avança de fato quando rtu acabar ok
      }
    }
    return;
  }

  // Aqui, CT_WAIT_GAP é usado em duas etapas: (1) esperar finalizar read1, (2) esperar gap, (3) iniciar read2
  if (ctPhase == CT_WAIT_GAP) {
    // se ainda está lendo, aguarda
    if (rtu.busy) return;

    // leitura terminou: verifica sucesso
    if (!rtu.ok) {
      ledState = LED_RTU_ERROR;
      mbTCP.Hreg(REG_STATUS, 0x0000);  // inválido
      gotoWaitId();

      return;
    }

    // armazenar primeira leitura e esperar gap
    ctVal1 = buf1[0];
    tCtGap = now + VALIDATE_CT_GAP_MS;
    ctPhase = CT_READ2;
    return;
  }

  if (ctPhase == CT_READ2) {
    if (now < tCtGap) return;

    if (!rtu.busy) {
      if (!rtuReadHreg(REG_CT, buf2, 1)) return;
      // depois que terminar, checa e compara abaixo
    } else {
      return;
    }

    // aguardar término via loop (rtu.busy false)
    // como acabamos de iniciar, sai agora
  }
}

// -------------------- Estado 3: CYCLIC --------------------
bool cyclicStartStep(CyStep step) {
  switch(step) {
    case CY_A:      return rtuReadHreg(REG_A_START, buf1, REG_A_LEN);
    case CY_B:      return rtuReadHreg(REG_B_START, buf1, REG_B_LEN);
    case CY_C:      return rtuReadHreg(REG_C_START, buf1, REG_C_LEN);
    case CY_D:      return rtuReadHreg(REG_D_START, buf1, REG_D_LEN);
    case CY_IDCHECK:return rtuReadHreg(REG_ID,      buf1, 1);
    default:        return false;
  }
}

void cyclicCommitStep(CyStep step) {
  switch(step) {
    case CY_A:       mirrorWriteBlock(REG_A_START, buf1, REG_A_LEN); break;
    case CY_B:       mirrorWriteBlock(REG_B_START, buf1, REG_B_LEN); break;
    case CY_C:       mirrorWriteBlock(REG_C_START, buf1, REG_C_LEN); break;
    case CY_D:       mirrorWriteBlock(REG_D_START, buf1, REG_D_LEN); break;
    case CY_IDCHECK: /* commit do ID não é necessário aqui (já existe),
                        mas podemos atualizar o espelho */
                     mirrorWrite(REG_ID, buf1[0]);
                     break;
  }
}

void handleCyclic(uint32_t now) {
  // só inicia um novo ciclo completo a cada 250 ms
  if (cyStep == CY_A && now < tNextCycleStart) return;

  // se não há request em andamento, inicia a do step atual
  if (!rtu.busy) {
    bool started = cyclicStartStep(cyStep);
    if (!started) return;
    // aguardar callback
    return;
  }

  // se ainda está ocupado, aguarda
}

void handleLed() {
  static uint32_t tLed = 0;
  static bool ledOn = false;
  uint32_t now = millis();

  switch (ledState) {

    case LED_OFF:
      digitalWrite(LED_PIN, HIGH);  // LED off (ativo em LOW)
      break;

    case LED_WIFI_OK:  // 2 Hz
      if (now - tLed >= 250) {
        tLed = now;
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
      }
      break;

    case LED_RTU_ERROR:  // 1 Hz
      if (now - tLed >= 500) {
        tLed = now;
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
      }
      break;

    case LED_ALL_OK:
      digitalWrite(LED_PIN, LOW);  // ligado fixo
      break;
  }
}

// -------------------- Setup/Loop: integração com callbacks e transições --------------------
void setup() {
  pinMode(PIN_RS485_DE_RE, OUTPUT);
  rs485TxEnable(false);

  Serial.begin(9600, SERIAL_8N1); // ajuste se necessário

  // O pino HALF_OR_FULL_PIN se refere a Half ou Full Duplex, devido a um HC4066 selecionando entre 2 transceivers de RS485.
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP); 

  //igitalWrite(DE_RE, LOW); // Inicializa em modo de recepção (RX).
  digitalWrite(HALF_OR_FULL_PIN, LOW);  //Define a Rede RS485 como Half Duplex
  digitalWrite(LED_PIN, HIGH); // Inicia o led como desligado

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);   // <<< aumenta estabilidade
  WiFi.config(local_IP, gateway, subnet);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(200);
  }

  // TCP Modbus
  mbTCP.server(502);
  tcpReserveRegs();

  // RTU Master
  mbRTU.begin(&Serial, PIN_RS485_DE_RE);
  mbRTU.master();

  mbTCP.Hreg(REG_STATUS, 0x0000);  // inválido
  gotoWaitId();

}

void loop() {
  
  static uint32_t tAge = 0;
  if (millis() - tAge >= 1000) {
    tAge += 1000;
    uint16_t age = mbTCP.Hreg(REG_AGE_S);
    if (age < 65535) mbTCP.Hreg(REG_AGE_S, age + 1);
  }

  // ---------------- WIFI WATCHDOG ----------------
  static uint32_t wifiRetryTimer = 0;

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiRetryTimer > 5000) {
      wifiRetryTimer = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    ledState = LED_OFF;
  }

  uint32_t now = millis();

  // tarefas contínuas
  mbTCP.task();
  mbRTU.task();
  yield();

  if (rtu.busy && (millis() - rtuTxnStart) > RTU_TXN_TIMEOUT_MS) {
    // força saída de busy para não travar o sistema
    rtu.busy = false;
    rtu.ok = false;
    // sinaliza falha e reinicia estados
    ledState = LED_RTU_ERROR;
    mbTCP.Hreg(REG_STATUS, 0x0000);  // inválido
    gotoWaitId();
  }

  // -------------------- Pós-callback: checagens e transições --------------------
  // A biblioteca chama callback e libera rtu.busy.
  // Então aqui fazemos as decisões quando uma transação termina.
  static bool lastBusy = false;
  if (lastBusy && !rtu.busy) {
    // uma transação acabou agora
    delay(0);
    if (state == S_WAIT_ID) {
      if (rtu.ok && buf1[0] == ID_EXPECTED) {
        idStored = buf1[0];

        mirrorWrite(REG_ID, idStored);
        // ID persistente validado e reservado (não sobrescrever com outra coisa)

        // vai para validação do CT
        state = S_VALIDATE_CT;
        ctPhase = CT_READ1;
      }
      // se falhou ou ID não bateu: continua tentando a cada 1s
    }

    else if (state == S_VALIDATE_CT) {
      // Pode ter finalizado a READ2 (ou a READ1, mas nós já tratamos a READ1 ao sair busy no CT_WAIT_GAP)
      if (!rtu.ok) {
        ledState = LED_RTU_ERROR;
        mbTCP.Hreg(REG_STATUS, 0x0000);  // inválido
        gotoWaitId();

      } else {
        // se está em CT_READ2, significa que essa conclusão foi do segundo read
        if (ctPhase == CT_READ2) {
          ctVal2 = buf2[0];

          if (ctVal2 == ctVal1) {
            mirrorWrite(REG_CT, ctVal2);
            // pronto para cíclico
            state = S_CYCLIC;
            cyStep = CY_A;
            cyRetries = 0;
            tNextCycleStart = now; // inicia já
          } else {
            mbTCP.Hreg(REG_STATUS, 0x0000);  // inválido
            gotoWaitId();
          }
        }
      }
    }

    else if (state == S_CYCLIC) {
      if (!rtu.ok) {
        if (cyRetries < CYCLIC_MAX_RETRIES) {
          cyRetries++;
          // reexecuta mesmo step
        } else {
          ledState = LED_RTU_ERROR;
          mbTCP.Hreg(REG_STATUS, 0x0000);  // inválido
          gotoWaitId();
        }
      } else {
        // sucesso: commit e avança step
        cyclicCommitStep(cyStep);
        cyRetries = 0;

        if (cyStep == CY_IDCHECK) {
          // validar ID
          uint16_t idNow = buf1[0];

          // ciclo válido
          mbTCP.Hreg(REG_STATUS, 0x0001);          // bit0 = DATA_VALID
          mbTCP.Hreg(REG_SEQ, (mbTCP.Hreg(REG_SEQ) + 1) & 0xFFFF);
          mbTCP.Hreg(REG_AGE_S, 0);

          if (idNow != idStored) {
            mbTCP.Hreg(REG_STATUS, 0x0000);  // inválido
            gotoWaitId();

          } else {
            // ciclo completo finalizado -> agenda próximo ciclo
            cyStep = CY_A;
            tNextCycleStart = now + CYCLIC_PERIOD_MS;
          }
        } else {
          // avança para próximo step
          cyStep = (CyStep)((uint8_t)cyStep + 1);
        }
      }
    }
  }
  lastBusy = rtu.busy;

  if (WiFi.status() == WL_CONNECTED && state != S_CYCLIC) {
    ledState = LED_WIFI_OK;
  }


  // -------------------- Executa estado atual --------------------
  switch(state) {
    case S_WAIT_ID:
      handleWaitId(now);
      break;

    case S_VALIDATE_CT:
      handleValidateCt(now);
      // Nota: a segunda leitura é iniciada dentro da função após o gap.
      // A validação/commit ocorre no bloco "pós-callback".
      break;

    case S_CYCLIC:
      ledState = LED_ALL_OK;
      handleCyclic(now);
      break;
  }

  handleLed();
}
