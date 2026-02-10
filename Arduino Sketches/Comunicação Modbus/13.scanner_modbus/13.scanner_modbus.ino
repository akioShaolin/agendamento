// Scanner de Registrador

// Declaração de Bibliotecas
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include <ModbusRTU.h>
#include "ModbusAPI.h"
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ================= OBJETOS =================
// Inicialização do RTC, timer por hardware, modbus e WifiServer
RTC_DS1307 rtc;
ModbusRTU rtu;
ESP8266WebServer server(80);
WebSocketsServer ws = WebSocketsServer(81);
StaticJsonDocument<200> doc;

// ================= WIFI AP =================
const char* ssid = "Scanner Modbus";
const char* password = "1234567890";

// ================= HTML =================
const char HTML_PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<title>Scanner Modbus</title>
    <style>
            :root {
                --bg: #f2f2f2;
                --card: #ffffff;
                --primary: #1976d2;
                --text: #333;
                --border: #ddd;
                --console-bg: #111;
                --console-text: #0f0;
            }

            body {
                font-family: Arial, Helvetica, sans-serif;
                background: var(--bg);
                padding: 20px;
                color: var(--text);
            }

            .card {
                background: var(--card);
                padding: 20px;
                border-radius: 10px;
                max-width: 420px;
                margin: auto;
                box-shadow: 0 4px 10px rgba(0,0,0,0.1);
            }

            h2 {
                margin-top: 0;
                text-align: center;
                color: var(--primary);
            }

            h3 {
                margin-bottom: 10px;
            }

            .label {
                font-weight: bold;
                margin-bottom: 5px;
            }

            .value {
                font-size: 1.2em;
                margin-bottom: 10px;
            }

            .inputs input {
                width: 200px;
                padding: 5px;
                margin: 3px;
                text-align: left;
            }

            button {
                background: var(--primary);
                color: #fff;
                border: none;
                padding: 10px 15px;
                border-radius: 6px;
                cursor: pointer;
                font-size: 1em;
            }

            button:hover {
                opacity: 0.9;
            }

            hr {
                border: none;
                border-top: 1px solid var(--border);
                margin: 20px 0;
            }

            /* ===== CONSOLE ===== */
            .console-container {
                max-width: 420px;
                margin: 15px auto 0 auto;
            }

            .console-title {
                font-weight: bold;
                margin-bottom: 5px;
            }

            #console {
                background: var(--console-bg);
                color: var(--console-text);
                font-family: Consolas, monospace;
                font-size: 0.8em;
                padding: 10px;
                border-radius: 8px;
                height: 200px;
                overflow-y: auto;
                white-space: pre-wrap;
                box-shadow: inset 0 0 5px rgba(0,0,0,0.6);
            }
    </style>
</head>
<body>
    <div class="card">
        <h2>Scanner Modbus</h2>
        <h3>Configurações do Escravo e Registro:</h3>
        <div class="inputs">
            <div class="label">ID do Escravo:</div>
            <input id="slaveId" type="number" value="1" min="1" max="247" />
            <div class="label">Registrador Inicial:</div>
            <input id="startRegister" type="number" value="1" min="0" max="65535" />
            <div class="label">Registrador Final:</div>
            <input id="endRegister" type="number" value="10" min="1" max="65535" />
        </div>
        <hr />
        <h3>Salvar Arquivo:</h3>
        <div class="inputs">
            <input id="fileName" type="text" value="scan_modbus.csv" />
            <button onclick="saveFile()">Salvar</button>
        </div>
        <hr />
        <h3>Configurações de Scanner</h3>
        <div class="inputs">
            <button onclick="beginScan()">Iniciar Scan</button>
            <button onclick="stopScan()">Parar Scan</button>
        </div>

        <div class="console-container">
            <div class="console-title">Console:</div>
            <div id="console">Registrador,Valor<br></div>
        </div>
    </div>
    <script>

        function beginScan() {}
        function stopScan() {}
        function saveFile() {}
        function adicionarLog(msg) {}

        const ws = new WebSocket("ws://192.168.4.1:81/");

        ws.onopen = () => console.log("Conectado ao WebSocket");
        ws.onerror = (e) => console.log("Erro no WebSocket: ", e);
        ws.onclose = () => console.log("WebSocket desconectado");

        ws.onmessage = (e) => {

            if (e.data.startsWith("LOG: ")) {
                console.log(e.data.substring(5));
                return;
            }

            if (e.data.startsWith("ERR: ")) {
                console.error(e.data.substring(5));
                return;
            }

            if (e.data === "busy") {
                busy = true;
                window.console.warn("Dispositivo ocupado. Tente novamente mais tarde.");
                return;
            }

            if (e.data.startsWith("DATA: ")) {
                const content = e.data.substring(6);
                
                let d;
                try {
                    d = JSON.parse(content);
                } catch (error) {
                    console.error("Erro ao analisar JSON:", error);
                    window.error("Erro ao analisar dados recebidos.");
                    return;
                }

                adicionarLog(`${d.register},${d.value}<br>`);
                return;
            }

            if (e.data === "end-scan") {
                busy = false;
                alert("Scan concluído.");
                return;
            }

        }

        let busy = false;

        function beginScan() {
            const slaveId = document.getElementById('slaveId');
            const startRegister = document.getElementById('startRegister');
            const endRegister = document.getElementById('endRegister');
            const consoleDiv = document.getElementById('console');

            slaveId.disabled = true;
            startRegister.disabled = true;
            endRegister.disabled = true;
            consoleDiv.innerHTML = "Registrador,Valor<br>";

            ws.send(JSON.stringify({
                action: 'begin-scan',
                slaveId: parseInt(slaveId.value),
                startRegister: parseInt(startRegister.value),
                endRegister: parseInt(endRegister.value)
            }));
        }

        function stopScan() {
            const slaveId = document.getElementById('slaveId');
            const startRegister = document.getElementById('startRegister');
            const endRegister = document.getElementById('endRegister');

            slaveId.disabled = false;
            startRegister.disabled = false;
            endRegister.disabled = false;
            ws.send(JSON.stringify({ action: 'stop-scan' }));
        }

        function saveFile() {
            let fileName = document.getElementById('fileName').value;
            /* Código para salvar o arquivo no dispositivo do usuário sem utilizar o console */
            let content = document.getElementById('console').innerText;
            let blob = new Blob([content], { type: 'text/csv' });
            let link = document.createElement('a');
            link.href = URL.createObjectURL(blob);
            link.download = fileName;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
        }

        function adicionarLog(msg) {
            const consoleDiv = document.getElementById('console');

            consoleDiv.innerHTML += `${msg}\n`;
            consoleDiv.scrollTop = consoleDiv.scrollHeight;
        }
    </script>
</body>
)HTMLPAGE";

// Definição dos pinos
#define LED_PIN           LED_BUILTIN   //  GPIO2 - Led Built in
#define DE_RE             12            // Pino ligado ao SN75176B (RS485 transceiver)
#define BTN_PIN           0             // Botão da placa com pull up
#define HALF_OR_FULL_PIN  13            // Half-Duplex:LOW  Full-Duplex-HIGH

// --- Configurações Modbus ---
#define RS485_BAUD 9600

// --- Variáveis de Controle ---

# define MAX_REG_COUNT 125

enum ScanState {
  SCAN_IDLE,
  SCAN_SEND,
  SCAN_WAIT
};

ScanState scanState = SCAN_IDLE;

uint16_t scanStart;
uint16_t scanEnd;
uint16_t scanCurrent;

uint16_t batchSizes[] = {125, 64, 32, 16, 1};
uint8_t batchIndex = 0;
uint16_t currentBatch = 1;

uint32_t scanTimer;
const uint16_t MODBUS_TIMEOUT = 300;

uint16_t lastBatch = 0;
bool scanRunning = false;

uint8_t wsClient = 255;

uint8_t slaveID = 1;
uint16_t startRegister = 1;
uint16_t endRegister = 10;

uint16_t val [MAX_REG_COUNT];

// ===== Prototipos =====

void beginScan(uint16_t, uint16_t);
void finishScan();
void sendScanRequest();
void processScanData();
void handleScanError();

// =========== WebSocketsServer ============

void wsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    
    case WStype_CONNECTED:
      wsClient = num;
      break;
    
    case WStype_DISCONNECTED:
      if (wsClient == num) wsClient = 255;
      break;

    case WStype_TEXT:

      DeserializationError error = deserializeJson(doc, (char*)payload);

      if (error) {
        ws.sendTXT(num, "ERR: Erro ao deserializar json.");
        return;
      }

      const char* action = doc["action"];

      if (strcmp(action, "begin-scan") == 0) {
        if (!scanRunning) {
          ws.sendTXT(num, "LOG: OK");
          // Recebe o json enviado pelo WS da pagina e separa os parametros
          slaveID = doc["slaveId"];
          startRegister = doc["startRegister"];
          endRegister = doc["endRegister"];
                   
          beginScan(startRegister, endRegister);

        } else {
          ws.sendTXT(num, "busy");
        }
      }

      if (strcmp(action, "end-scan") == 0) {
        if(scanRunning) {
          ws.sendTXT(num, "LOG: OK");
          finishScan();
        }
      }
      break;
  }
}

// ================= ROTAS =================
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

// ================================= MODBUS ====================================

// Callback do modbus
bool cb(Modbus::ResultCode event, uint16_t, void*) { // Callback to monitor errors

  if (event != Modbus::EX_SUCCESS) {
    char buffer[30];

    sprintf(buffer, "ERR: Modbus error 0x%02X.", event);
    ws.sendTXT(wsClient, buffer);
  }
    return true;
}

void beginScan(uint16_t start, uint16_t end) {
  scanStart = start;
  scanEnd = end;
  scanCurrent = scanStart;

  batchIndex = 0;
  currentBatch = batchSizes[batchIndex];

  scanRunning = true;
  scanState = SCAN_SEND;

  ws.sendTXT(wsClient, "LOG: Scan iniciado");
}

void sendScanRequest() {
  if(scanCurrent > scanEnd){
    finishScan();
    return;
  }

  uint16_t remaining = scanEnd - scanCurrent + 1;
  lastBatch = min (currentBatch, remaining);

  if (!rtu.readHreg(slaveID, scanCurrent, val, lastBatch, cb)) {
    ws.sendTXT(wsClient, "ERR: Modbus ocupado");
    finishScan();
    return;
  }

  scanTimer = millis();
  scanState = SCAN_WAIT;
}

void handleScanError() {
  if(currentBatch > 1) {
    batchIndex++;
    currentBatch = batchSizes[batchIndex];
    scanState = SCAN_SEND;
  } else {
    // batch == 1 -> pula registrador
    scanCurrent++;
    batchIndex = 0;
    currentBatch = batchSizes[0];
    scanState = SCAN_SEND;
  }
}

void processScanData() {
  for (uint16_t i = 0; i < lastBatch; i++) {
    char msg[64];
    sprintf(msg, "DATA: {\"register\": \"%u\", \"value\": \"0x%04X\"}", scanCurrent + i, val[i]);
    ws.sendTXT(wsClient, msg);
  }
}

void finishScan() {
  scanRunning = false;
  scanState = SCAN_IDLE;
  ws.sendTXT(wsClient, "end-scan");
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
  server.begin();

  ws.begin();
  ws.onEvent(wsEvent);

  // Início do barramento I2C
  Wire.begin(4, 5); // SDA = GPIO4, SCL = GPIO5

  // O pino HALF_OR_FULL_PIN se refere a Half ou Full Duplex, devido a um HC4066 selecionando entre 2 transceivers de RS485.
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP); 

  //igitalWrite(DE_RE, LOW); // Inicializa em modo de recepção (RX).
  digitalWrite(HALF_OR_FULL_PIN, LOW);  //Define a Rede RS485 como Half Duplex
  digitalWrite(LED_PIN, LOW); // Inicia o led como ligado

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
}

// ================================== LOOP ====================================

void loop() {

  ws.loop();
  server.handleClient();
  rtu.task();
  yield();

  if (!scanRunning) return;
  
  switch (scanState) {

    case SCAN_SEND:
      sendScanRequest();
      break;

    case SCAN_WAIT:

      if (!rtu.slave()) {
      processScanData();
      scanCurrent += lastBatch;

      // Reset para batch máximo
      batchIndex = 0;
      currentBatch = batchSizes[batchIndex];

      scanState = SCAN_SEND;
    } else if (millis() - scanTimer > MODBUS_TIMEOUT) {
      handleScanError();
    }
      break;

    default:
      break;
  }
}