#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>

// Configuração de Hardware
#define RS485_BAUD 9600 // Velocidade do barramento RS485
#define DE_RE 12 // Controle do transceptor RS485 (DE/RE)
#define HALF_OR_FULL_PIN  13            // Half-Duplex:LOW  Full-Duplex-HIGH
#define BTN_PIN           0             // Botão da placa com pull up
// Tempo de silêncio Modbus RTU (3.5 caracteres @ 9600 bps ≈ 3.64 ms). Usamos 4ms para segurança.
#define MODBUS_TIMEOUT_MS 4 

// ========== WiFi ==========

const char* ssid = "Sniffer WiFi";
const char* password = "1234567890";

// ========== OBJETOS ==========
ESP8266WebServer server(80);
WebSocketsServer ws = WebSocketsServer(81);

// Buffer de frame
String frameBuffer;
bool inFrame = false;

// ========== HTML ==========
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<title>Modbus RTU Sniffer</title>
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
        <h2>Modbus RTU Sniffer</h2>
        <hr />
        <h3>Salvar Arquivo:</h3>
        <div class="inputs">
            <input id="fileName" type="text" value="sniffing_modbus.csv" />
            <button onclick="saveFile()">Salvar</button>
        </div>
        <hr />
        <div class="console-container">
            <div class="console-title">Console:</div>
            <div id="console">Sniffer<br></div>
        </div>        
    </div>
  <script>
    let ws = new WebSocket('ws://192.168.4.1:81/');

    ws.onopen = () => console.log("Conectado ao WebSocket");
    ws.onerror = (e) => console.log("Erro no WebSocket: ", e);
    ws.onclose = () => console.log("WebSocket desconectado");

    ws.onmessage = (e) => {
        let consoleDiv = document.getElementById('console');
        consoleDiv.innerHTML += e.data + '<br>';
        consoleDiv.scrollTop = consoleDiv.scrollHeight;
    };

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

  </script>
</body>
)rawliteral";

// ========== WEBSOCKET ==========

uint8_t wsClient = 255;

void wsEvent (uint8_t num, WStype_t type, uint8_t * payload, size_t len) {
  
  switch (type) {

    case WStype_CONNECTED:
      wsClient = num;
      break;

    case WStype_DISCONNECTED:
      if (wsClient == num)  { 
        wsClient = 255;
        frameBuffer = "";
        inFrame = false;
      }
      break; 
  }
}

// ========== ROTAS ==========

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

// ========== MODBUS ==========

void sniff() {
  static uint32_t lastByteTime = 0;
  static bool inFrame = false;
  float time = 0;
  char buffer [20];
  
  // 1. Processamento de Bytes (Não-Bloqueante)
  while (Serial.available()) {
    uint8_t b = Serial.read();
    
    // Se a trama anterior terminou, ou se estamos começando uma nova
    if (!inFrame) {
      time = (float)millis() / 1000;
      frameBuffer = "";
      sprintf(buffer, "%5.3f [RX] ", time);
      frameBuffer = buffer;
      inFrame = true;
    }
    
    char hex[4];
    sprintf(hex, "%02X ", b);
    frameBuffer += hex;

    lastByteTime = millis();
    
    // O yield() é importante para o ESP8266, mas deve ser usado com moderação
    // para não introduzir atrasos no loop de leitura.
    // yield(); 
  }

  // 2. Detecção de Fim de Trama (Timeout)
  // Verifica se a trama estava ativa e se o tempo de silêncio Modbus (4ms) foi excedido
  if (inFrame && (millis() - lastByteTime > MODBUS_TIMEOUT_MS)) {
    if (wsClient != 255) {
      ws.broadcastTXT(frameBuffer);
    }
    inFrame = false;
  }
}

void setup() {
  // Inicializa a Serial principal (UART0) para o RS485
  Serial.begin(RS485_BAUD);
  
  // WIFI AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // SERVER
  server.on("/", handleRoot);
  server.begin();

  ws.begin();
  ws.onEvent(wsEvent);

  // Configuração dos pinos
  // O pino HALF_OR_FULL_PIN se refere a Half ou Full Duplex, devido a um HC4066 selecionando entre 2 transceivers de RS485.
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(DE_RE, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT); // Pino 2 é o LED_BUILTIN em alguns ESPs, mas você o usou para RX do Debug. Vou usar o LED_BUILTIN para o pino 13.
  pinMode(BTN_PIN, INPUT_PULLUP); 
  
  // Configura o transceptor para modo de recepção (LOW)
  digitalWrite(DE_RE, LOW);
  digitalWrite(HALF_OR_FULL_PIN, LOW);  //Define a Rede RS485 como Half Duplex
  digitalWrite(LED_BUILTIN, LOW); // LED ligado
}

void loop() {
  sniff();
  // O loop roda o mais rápido possível, maximizando a chance de ler o buffer serial.
  ws.loop();
  server.handleClient();
}