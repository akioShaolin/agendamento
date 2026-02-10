// Declaração de Bibliotecas
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include <Ticker.h>
#include <ModbusRTU.h>
#include "ModbusAPI.h"

// ================= OBJETOS =================
// Inicialização do RTC, timer por hardware, modbus e WifiServer
RTC_DS1307 rtc;
Ticker ticker;
ModbusRTU rtu;
ESP8266WebServer server(80);

// ================= WIFI AP =================
const char* ssid = "InjecaoProg";
const char* password = "1234567890";

// ================= HTML =================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>ESP07 RTC</title>
<style>
body { font-family: Arial; background:#f2f2f2; padding:20px; }
.card { background:#fff; padding:20px; border-radius:8px; max-width:400px; }
h2 { margin-top:0; }
</style>
</head>
<body>
<div class="card">
  <h2>RTC ESP07</h2>

  <p><b>Data/Hora:</b></p>
  <p id="hora">--</p>

  <p><b>Potência:</b></p>
  <p id="valor">--</p>

  <hr>

  <h3>Ajustar RTC</h3>
  <input id="d" placeholder="DD" size="2">
  <input id="m" placeholder="MM" size="2">
  <input id="y" placeholder="AAAA" size="4"><br><br>
  <input id="hh" placeholder="HH" size="2">
  <input id="mm" placeholder="MM" size="2">
  <input id="ss" placeholder="SS" size="2"><br><br>

  <button onclick="ajustar()">Ajustar</button>
</div>

<script>
function atualizar() {
  fetch('/data')
    .then(r => r.json())
    .then(d => {
      document.getElementById('hora').innerHTML = d.datetime;
      document.getElementById('valor').innerHTML = d.valor;
    });
}

function ajustar() {
  let url = `/set?d=${d.value}&m=${m.value}&y=${y.value}&hh=${hh.value}&mm=${mm.value}&ss=${ss.value}`;
  fetch(url);
}

setInterval(atualizar, 1000);
atualizar();
</script>

</body>
</html>
)rawliteral";

// Definição dos pinos
#define LED_PIN           LED_BUILTIN   //  GPIO2 - Led Built in
#define DE_RE             12            // Pino ligado ao SN75176B (RS485 transceiver)
#define BTN_PIN           0             // Botão da placa com pull up
#define HALF_OR_FULL_PIN  13            // Half-Duplex:LOW  Full-Duplex-HIGH

// --- Configurações Modbus ---
// IDs do dispositivo escravo Modbus.
#define SLAVE_1_ID 2
#define RS485_BAUD 9600

// --- Configuração dos Registradores pertinentes ---

#define HR_ACT_PWR_OUT      0x9C8F   //Holding Register Active Power Output. Scale: 100 [W]
#define HR_EN_ACT_PWR_LIM   0x9D6B   //Holding Register Enable Active Power Limit. [0 - Off, 1 - On]
#define HR_ACT_PWR_LIM_VL   0x9D6C   //Holding Register Active Power Limit Value. Scale: 0.1 [%]  (0~1100)

// --- Variáveis de Controle ---
// Flag vinculada ao Timer1 para atualização do RTC
volatile bool tickFlag = false;

# define REG_COUNT 1

uint16_t reg_act_power = 0;
float power = 0;

// --- Programação dos eventos ---
// ID do programa
// Dia da Semana
// Hora de início
// Minuto de início
// Hora de fim
// Minuto de fim
// Potência
// Ativado

#define POT_INV 100000 //Potência do inversor especificada em 100kW
#define NUM_PROGRAMS 3

struct Program {
  uint8_t id;                   // ID do programa (1 a 4)
  uint8_t dayOfWeek;            // Dia da semana (0 - Domingo, 1 - Segunda)
  uint8_t startHour;                 // Hora do inicio evento
  uint8_t startMinute;               // Minuto do inicio evento
  uint8_t endHour;                 // Hora do inicio evento
  uint8_t endMinute;               // Minuto do inicio evento
  uint32_t power;               // Potência (em W)
  bool enabled;                 // 1 - ativo, 0 - inativo
};

Program programs[NUM_PROGRAMS] PROGMEM = {
  {0x01, 0,  8,  0,  9,  0,  23700, true},
  {0x02, 0,  9,  0, 15,  0,      0, true},
  {0x03, 0, 15,  0, 16,  0,  27000, true},
};

const Program defaultProgram PROGMEM  = {0x00, 0,  0,  0,  0,  0, 75000, true};

// ================= ROTAS =================
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleData() {
  DateTime now = rtc.now();

  char buffer[60];
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d",
          now.day(), now.month(), now.year(),
          now.hour(), now.minute(), now.second());

  String json = "{";
  json += "\"datetime\":\"" + String(buffer) + "\",";
  json += "\"valor\":\"" + String(power, 1) + " kW\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetRTC() {
  if (server.hasArg("d") && server.hasArg("m") && server.hasArg("y") &&
      server.hasArg("hh") && server.hasArg("mm") && server.hasArg("ss")) {

    rtc.adjust(DateTime(
      server.arg("y").toInt(),
      server.arg("m").toInt(),
      server.arg("d").toInt(),
      server.arg("hh").toInt(),
      server.arg("mm").toInt(),
      server.arg("ss").toInt()
    ));
  }

  server.send(200, "text/plain", "OK");
}

// --- Função de interrupção (Timer1 hardware) ---
void onTick() {
  tickFlag = true;  // Sinaliza que passou 1 segundo  
}

// Callback do modbus

bool cb(Modbus::ResultCode event, uint16_t transactionId, void* data) { // Callback to monitor errors
  if (event != Modbus::EX_SUCCESS) {
    //Serial.print("Request result: 0x");
    //Serial.print(event, HEX);
  }
  return true;
}

// --- Verificação de Programas e ativação ---

Program currentProgram;
void checkPrograms(DateTime now) {
  uint8_t activeProgram = 0; // 0 = default

  uint8_t rtcDow = now.dayOfTheWeek(); // 0 - Domingo, 6 - Sábado

  // Percorre todos os programas da PROGMEM
  for (int i = 0; i < NUM_PROGRAMS; i++) {
    Program p;
    memcpy_P(&p, &programs[i], sizeof(Program));
    
    if (!p.enabled || rtcDow != p.dayOfWeek) continue;

    uint16_t nowMins = now.hour() * 60 + now.minute();
    uint16_t startMins = p.startHour * 60 + p.startMinute;
    uint16_t endMins = p.endHour * 60 + p.endMinute;

    if (nowMins >= startMins && nowMins < endMins) {
      activeProgram = p.id;
      if (currentProgram.id != p.id) {
        activateProgram(p);
        currentProgram = p;
      }
      break; // Apenas um programa ativo por vez
    }
  }

  // Nenhum programa válido encontrado → volta para o Default
  if (activeProgram == 0 && currentProgram.id != 0) {
    Program p;
    memcpy_P(&p, &defaultProgram, sizeof(Program));
    activateProgram(p);
    currentProgram = p;
  }
}

// --- Rodando programa de alteração da potência ---
void activateProgram(Program p) {
  bool limit_enable = true;

  uint32_t powerLimit = p.power;
  uint32_t pwrPercent = powerLimit * 1000 / POT_INV; // Escala de 0,1%. É preciso multiplicar por 1000 para ajustar os valores

  // 1. Habilita a Limitação de Potência
  //Debug.printf("Escrevendo flag %d no registrador %d do Escravo ID %d...\n", limit_enable, HR_EN_ACT_PWR_LIM, SLAVE_1_ID);

  if (!rtu.slave()) { // Verifica se não há nenhuma transação em progresso
    rtu.writeHreg(SLAVE_1_ID, HR_EN_ACT_PWR_LIM, limit_enable, cb); 
    while(rtu.slave()) { // Checa se a transação está ativa
      rtu.task();
      delay(10);
    }
  }
  delay(100);

  // 2. Define o Limite de Potência
  //Debug.printf("Escrevendo valor principal %d no registrador %d do Escravo ID %d...\n", pwrPercent, HR_ACT_PWR_LIM_VL, SLAVE_1_ID);

  if (!rtu.slave()) { // Verifica se não há nenhuma transação em progresso
    rtu.writeHreg(SLAVE_1_ID, HR_ACT_PWR_LIM_VL, pwrPercent, cb); 
    while(rtu.slave()) { // Checa se a transação está ativa
      rtu.task();
      delay(10);
    }
  }
  delay(100);

  uint32_t change = millis();
  while (millis() - change < 3000) { //Aguarda 3 segundos até a potência estabilizar
    digitalWrite(LED_PIN, LOW);
    yield();
  }
}

// ================================== SETUP ====================================
void setup() {
  // Define o RTU como master
  rtu.master();

  // Inicia a comunicação serial para Modbus RTU (HardwareSerial).
  Serial.begin(RS485_BAUD);
  rtu.begin(&Serial, DE_RE); 
  
  // WIFI AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // SERVER
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/set", handleSetRTC);
  server.begin();

  // Início do barramento I2C
  Wire.begin(4, 5); // SDA = GPIO4, SCL = GPIO5

  // O pino HALF_OR_FULL_PIN se refere a Half ou Full Duplex, devido a um HC4066 selecionando entre 2 transceivers de RS485.
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP); 

  //igitalWrite(DE_RE, LOW); // Inicializa em modo de recepção (RX).
  digitalWrite(HALF_OR_FULL_PIN, LOW);  //Define a Rede RS485 como Half Duplex
  digitalWrite(LED_PIN, HIGH); // Inicia o led como desligado

  // =====================================================

  delay(2000); // Pequena pausa para estabilização.

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
    //Debug.println("RTC inciado.");
  } else {
    //Debug.println("RTC não encontrado!");
    digitalWrite(LED_PIN, HIGH);
    while(1) yield();
  }
  
  // Verifica se o RTC está funcionando
  if (!rtc.isrunning()) {
    //Debug.println("RTC não estava rodando, necessário ajuste de data e hora.");
    digitalWrite(LED_PIN, HIGH);
    //settingMode = true;
  }

  // --- Configuração do Timer1 hardware (ESP8266) ---
  ticker.attach(1.0, onTick); //Interrupção a cada 1 segundo
  //Debug.println("Iniciado.");
}

// ================================== LOOP ====================================

void loop() {

  server.handleClient();

  if (tickFlag) {

    tickFlag = false;
    digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);

    DateTime now = rtc.now();

    // Essa é a linha mais importante do código
    checkPrograms(now);

    // Leitura da potência do inversor
    if (!rtu.slave()) { // Verifica se não há nenhuma transação em progresso
      rtu.readHreg(SLAVE_1_ID, HR_ACT_PWR_OUT, &reg_act_power, REG_COUNT, cb);  
      power = (float)reg_act_power / 10; // Divide por 10 para ter a unidade como kW
      while(rtu.slave()) { // Checa se a transação está ativa
        rtu.task();
        delay(10);
      }
    } else {

    }
  }
  yield();
}