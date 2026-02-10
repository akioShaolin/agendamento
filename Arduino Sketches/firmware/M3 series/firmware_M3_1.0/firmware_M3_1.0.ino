// Feito sob medida para a cliente Vanessa Hespanha
// Concessionária: Neoenergia

// ModbusRTU não bloqueante
// WebServer
// WebSockets
// Configuração e vizualizações via WiFi
// RTC integrado
// Máquina de estados


// Atualiza o RTC a cada 600 segundos conforme o horario do inversor hospedeiro
// A partir do tempo, executa os programas agendados, verificando a cada minuto
// Pode ser acessado pela pagina web, onde poderá ver os logs, horário e qual programa ativo.
// Verifica a Potência do inversor a cada segundo

// -----------------------------------------------------------------------------
// ---------------------- Declaração de Bibliotecas ----------------------------
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
//#include "ModbusAPI.h"
#include <ModbusRTU.h>
#include <Wire.h>
#include "RTClib.h"
#include <Ticker.h>
#include <WebSocketsServer.h>

#include "firmware_config.h"

// -----------------------------------------------------------------------------
// ------------------- Declaração de Objetos externos --------------------------
// -----------------------------------------------------------------------------

RTC_DS1307 rtc;
Ticker ticker;
Ticker updateRTC;
ModbusRTU rtu;
ESP8266WebServer server(80);
WebSocketsServer ws(81);

// -----------------------------------------------------------------------------
// -------------------- Declaração de Objetos locais ---------------------------
// -----------------------------------------------------------------------------

RTUState rtuState = RTUState::RTU_IDLE;
RtcAdjustService rtcSvc;
ProgramService ProgSvc;
ReadPowerService readPowerSvc;

// -----------------------------------------------------------------------------
// --------------------------------- TICKER ------------------------------------
// -----------------------------------------------------------------------------

// --- Função de interrupção (Timer1 hardware) ---
void onTick() {
  tickFlag = true;  // Sinaliza que passou 1 segundo  
}

// Acerta o RTC com base no horário do inversor a cada 600 segundos
void update_RTC(){
  updRTC = true; // Sinaliza que é hora de ajustar o RTC
}

// -----------------------------------------------------------------------------
// --------------------------------- ROTAS -------------------------------------
// -----------------------------------------------------------------------------

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleData(DateTime now) {
  
  char buffer [128];

  sprintf(buffer, "{\"day\": \"%d\", \"month\": \"%d\", \"year\": \"%d\", \"hour\": \"%d\", \"minute\": \"%d\", \"second\": \"%d\", \"dayOfTheWeek\":  \"%d\"}",
          now.day(), now.month(), now.year(),
          now.hour(), now.minute(), now.second(), now.dayOfTheWeek());

  ws.sendTXT(wsClient, buffer);
}

// -----------------------------------------------------------------------------
// -------------------------- WEB SOCKETS SERVER -------------------------------
// -----------------------------------------------------------------------------

void wsEvent(uint8_t num, WStype_t type, uint8_t payload, size_t lenght){
  
  switch (type){
    case WStype_CONNECTED:
      wsClient = num;
      break;

    case WStype_DISCONNECTED:
      if (wsClient == num) wsClient = 255;
      break;

    case WStype_TEXT:

      break;
  }
}

// -----------------------------------------------------------------------------
// --------------------------------- MODBUS ------------------------------------
// -----------------------------------------------------------------------------

// ------------------------- Tradução do result code ---------------------------

const char* resultCode(Modbus::ResultCode event) {

  switch (event) {
    
    case Modbus::EX_SUCCESS: 
      return PSTR("EX_SUCCESS: No error");

    case Modbus::EX_ILLEGAL_FUNCTION: 
      return PSTR("EX_ILLEGAL_FUNCTION: Function Code not Supported");

    case Modbus::EX_ILLEGAL_ADDRESS: 
      return PSTR("EX_ILLEGAL_ADDRESS: Output Address not exists");

    case Modbus::EX_ILLEGAL_VALUE: 
      return PSTR("EX_ILLEGAL_VALUE: Output Value not in Range");

    case Modbus::EX_SLAVE_FAILURE: 
      return PSTR("EX_SLAVE_FAILURE: Slave or Master Device Fails to process request");

    case Modbus::EX_ACKNOWLEDGE:
      return PSTR("EX_ACKNOWLEDGE: Not used");

    case Modbus::EX_SLAVE_DEVICE_BUSY:
      return PSTR("EX_SLAVE_DEVICE_BUSY: Not used");

    case Modbus::EX_MEMORY_PARITY_ERROR:
      return PSTR("EX_MEMORY_PARITY_ERROR: Not used");

    case Modbus::EX_PATH_UNAVAILABLE:
      return PSTR("EX_PATH_UNAVAILABLE: Not used");

    case Modbus::EX_DEVICE_FAILED_TO_RESPOND:
      return PSTR("EX_DEVICE_FAILED_TO_RESPOND: Not used");

    case Modbus::EX_GENERAL_FAILURE: 
      return PSTR("EX_GENERAL_FAILURE: Unexpected master error");

    case Modbus::EX_DATA_MISMACH: 
      return PSTR("EX_DATA_MISMACH: Input data size mismach");

    case Modbus::EX_UNEXPECTED_RESPONSE: 
      return PSTR("EX_UNEXPECTED_RESPONSE: Returned result doesn't mach transaction");

    case Modbus::EX_TIMEOUT: 
      return PSTR("EX_TIMEOUT: Operation not finished within reasonable time");

    case Modbus::EX_CONNECTION_LOST: 
      return PSTR("EX_CONNECTION_LOST: Connection with device lost");

    case Modbus::EX_CANCEL:
      return PSTR("EX_CANCEL: Transaction/request canceled");

    case Modbus::EX_PASSTHROUGH: 
      return PSTR("EX_PASSTHROUGH: Raw callback");

    case Modbus::EX_FORCE_PROCESS: 
      return PSTR("EX_FORCE_PROCESS: Raw callback");

    default:
      return PSTR("UNKNOWN ERROR");
  }
}

bool stepTimedOut(uint32_t now, uint32_t stepStart, uint32_t timeoutMs) {
  return (uint32_t)(now - stepStart) > timeoutMs;
}

// -----------------------------------------------------------------------------
// --------------------------------- SETUP -------------------------------------
// -----------------------------------------------------------------------------

void setup() {

  // Define o RTU como master
  rtu.master();
  // Inicia a comunicação serial para o Modbus (HardwareSerial).
  Serial.begin(RS485_BAUD); 
  rtu.begin(&Serial, DE_RE);

  // WIFI AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // SERVER
  server.on("/", handleRoot);
  server.begin();

  // Início do barramento I2C
  Wire.begin(4, 5); // SDA = GPIO4, SCL = GPIO5

  // O pino HALF_OR_FULL_PIN se refere a Half ou Full Duplex, devido a um HC4066 selecionando entre 2 transceivers de RS485.
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP); 

  digitalWrite(HALF_OR_FULL_PIN, LOW);  // Define a Rede RS485 como Half Duplex
  digitalWrite(BTN_PIN, LOW); // Inicia o led como desligado

  // ===========================================================================
  // --- Iniciando RTC, com até 5 tentativas de conectar, caso não funcionar de primeira

  bool rtcFound = false;
  for (int i = 0; i < 5; i++) {
    if (rtc.begin()) {
      rtcFound = true;
      break;
    }
    delay(200);
  }
  // Caso o RTC não for encontrado ou não estiver funcionando corretamente, indica com o led ligado continuamente
  if (rtcFound) {
  } else {
    digitalWrite(LED_PIN, HIGH);
    while(1) yield();
  }
  
  // Verifica se o RTC está funcionando
  if (!rtc.isrunning()) {
    digitalWrite(LED_PIN, HIGH);
    while(1) yield();
  }

  // --- Configuração do Timer1 hardware (ESP8266) ---
  ticker.attach(1.0, onTick);
  updateRTC.attach(rtcSvc.period, update_RTC);
}

// -----------------------------------------------------------------------------
// ---------------------------------- LOOP -------------------------------------
// -----------------------------------------------------------------------------
void loop() {

  uint32_t now = millis();
  
  server.handleClient();
  rtu.task();
  ws.loop();
  yield();

  if (tickFlag) {
    tickFlag = false;
    digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);

    DateTime dt = rtc.now();
    // Verificação dos programas sempre que os minutos mudarem
    if (ProgSvc.lastMinute != dt.minute()){
      ProgSvc.pending = true;
      ProgSvc.lastMinute = dt.minute(); 
    }

    // Requisição de leitura de potência quando o WebSockets estiver ativo
    if (wsClient != 255) {
      readPowerSvc.pending = true;
    }
  }
  if(updRTC){
    updRTC = false;
    rtcSvc.pending = true;
  }

  // Organização em ordem de prioridade. É mais importante ter um relógio atualizado do que ativar um programa, pois se a hora estiver errada, o programa será ativado errôneamente. A leitura é só para mostrar ao usuário, portanto, menos importante.

  switch (rtuState) {

    case RTUState::RTC_ADJUSTING: {
      bool finished = rtcAdjustTick(rtcSvc, now);
      if (finished) {
        if (rtcSvc.state == RtcAdjState::DONE && rtcSvc.modbusOk && rtcSvc.dataOk && rtcSvc.rtcOk) {
          // Ok: volta ao normal
          // Envia mensagem ao console------------------------------------------
        } else {
          // Erro: loga, conta falhas, decide retry, etc
          // Envia mensagem ao console -----------------------------------------
        }
        rtcSvc.state = RtcAdjState::IDLE;
        rtuState = RTUState::RTU_IDLE;
      }
      break;
    }     

    case RTUState::PROGRAMS_CHECK: {
      DateTime dt = rtc.now();
      bool finished = ProgramTick(ProgSvc, dt);
      if (finished) {
        if(ProgSvc.state == ProgramState::DONE) {
          // Ok: volta ao normal
          // Envia mensagem ao console------------------------------------------
        } else {
          // Erro: loga, conta falhas, decide retry, etc
          // Envia mensagem ao console----------------------------------------- 
        }
        ProgSvc.state = ProgramState::IDLE;
        rtuState = RTUState::RTU_IDLE;
      }
      break;
    }

    case RTUState::POWER_READING: {
      bool finished = readPowerTick(readPowerSvc, now);
      if (finished) {
        if(readPowerSvc.state == ReadPowerState::DONE) {
          // Ok: volta ao normal
          // Envia mensagem ao console------------------------------------------
        } else {
          // Erro: loga, conta falhas, decide retry, etc
          // Envia mensagem ao console------------------------------------------
        }
        readPowerSvc.state = ReadPowerState::IDLE;
        rtuState = RTUState::RTU_IDLE;
      }
      break;
      }

    default:
      if (rtcSvc.pending) {
        rtuState = RTUState::RTC_ADJUSTING;
        // O RTC só será ajustado quando o modbus estiver desocupado e a flag do updRTC estiver em true
      }

      if (ProgSvc.pending) {
        rtuState = RTUState::PROGRAMS_CHECK;
        // Os Programas só serão verificados quando o modbus estiver desocupado e a tickFlag estiver em true
      }  

      if (readPowerSvc.pending) {
        rtuState = RTUState::POWER_READING;
        // A potência só será lida quando o modbus estiver desocupado e a tickFlag estiver em true
      }
      break;
  }
}
