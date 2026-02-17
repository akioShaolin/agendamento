/*
  ESP8266 - Leitor Modbus configurável via página Web

  Você escolhe na página:
    - Endereço inicial do registrador (hex "0xA710" ou decimal)
    - Quantidade de registradores (count)

  Ao clicar "Confirmar":
    - A página envia para o ESP (HTTP /cfg)
    - O ESP atualiza o endereço/quantidade
    - Dispara uma leitura imediata
    - Mantém leitura periódica a cada 5 segundos

  Dados são enviados "crus" por WebSocket para a página (sem formatação).
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ModbusRTU.h>
#include <Ticker.h>
#include <Arduino.h>

// ------------------- Wi-Fi AP -------------------
const char* ssid     = "relogio inversor";
const char* password = "1234567890";

// ------------------- RS485 / Modbus -------------------
#define DE_RE      12
#define RS485_BAUD 9600
#define SLAVE_ID   2

// Limite de leitura (pra não estourar RAM / buffer)
#define MAX_REGS   64

// ------------------- Objetos -------------------
ModbusRTU rtu;
ESP8266WebServer server(80);
WebSocketsServer ws(81);
Ticker tRead;

volatile bool flagRead = false;
volatile bool flagForceRead = false;

uint8_t wsClient = 255;

// Config atual (editável via web)
uint16_t cfgAddr  = 0xA712;
uint16_t cfgCount = 6;

// Buffer de leitura
uint16_t regs[MAX_REGS];
bool lastOk = false;

// ------------------- HTML simples -------------------
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>Leitor Modbus</title>
</head>
<body>
<pre>
Configurar leitura:
- Endereço inicial (hex 0x.... ou decimal):
</pre>

<input id="addr" placeholder="0xA710" value="0xA712" />
<pre>
- Quantidade de registradores:
</pre>
<input id="count" placeholder="6" value="6" />
<br><br>
<button id="btn">Confirmar</button>

<pre id="status">---</pre>
<hr>
<pre id="out">conectando...</pre>

<script>
  const out = document.getElementById("out");
  const statusEl = document.getElementById("status");
  const btn = document.getElementById("btn");
  const WS_PORT = 81;

  function wsUrl(){
    const host = location.hostname || "192.168.4.1";
    return `ws://${host}:${WS_PORT}/`;
  }

  function connectWS(){
    out.textContent = "conectando...";
    const w = new WebSocket(wsUrl());

    w.onopen  = () => { out.textContent = "ws conectado. aguardando dados..."; };
    w.onclose = () => { out.textContent = "ws desconectado. reconectando..."; setTimeout(connectWS, 1200); };
    w.onmessage = (e) => { out.textContent = String(e.data ?? ""); };
  }

  async function aplicarCfg(){
    const addr = document.getElementById("addr").value.trim();
    const count = document.getElementById("count").value.trim();

    const url = `/cfg?addr=${encodeURIComponent(addr)}&count=${encodeURIComponent(count)}`;

    statusEl.textContent = "enviando...";
    try{
      const res = await fetch(url, { cache: "no-store" });
      const txt = await res.text();
      statusEl.textContent = txt;
    }catch(e){
      statusEl.textContent = "erro ao enviar cfg";
    }
  }

  btn.addEventListener("click", aplicarCfg);
  connectWS();
</script>
</body>
</html>
)rawliteral";

// ------------------- Util parse: aceita "0xA712" ou "42770" -------------------
static bool parseU16(const String& s, uint16_t &out){
  String t = s;
  t.trim();
  if (t.length() == 0) return false;

  const char* c = t.c_str();
  char* endp = nullptr;

  unsigned long v = 0;
  if (t.startsWith("0x") || t.startsWith("0X")) {
    v = strtoul(c, &endp, 16);
  } else {
    v = strtoul(c, &endp, 10);
  }

  if (endp == c) return false;         // não converteu nada
  if (v > 0xFFFFUL) return false;      // fora de u16

  out = (uint16_t)v;
  return true;
}

// ------------------- WebSocket events -------------------
void wsEvent(uint8_t num, WStype_t type, uint8_t*, size_t){
  if (type == WStype_CONNECTED) wsClient = num;
  if (type == WStype_DISCONNECTED && wsClient == num) wsClient = 255;
}

// ------------------- Rotas HTTP -------------------
void handleRoot(){
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleCfg(){
  // espera: /cfg?addr=0xA710&count=6
  if (!server.hasArg("addr") || !server.hasArg("count")){
    server.send(400, "text/plain", "ERR: faltou addr/count");
    return;
  }

  uint16_t a=0, c=0;
  if (!parseU16(server.arg("addr"), a)){
    server.send(400, "text/plain", "ERR: addr invalido");
    return;
  }
  if (!parseU16(server.arg("count"), c)){
    server.send(400, "text/plain", "ERR: count invalido");
    return;
  }
  if (c < 1 || c > MAX_REGS){
    server.send(400, "text/plain", "ERR: count fora (1..64)");
    return;
  }

  cfgAddr = a;
  cfgCount = c;

  // força leitura imediata quando houver alteração (após botão Confirmar)
  flagForceRead = true;

  char b[64];
  snprintf(b, sizeof(b), "OK: addr=0x%04X count=%u", cfgAddr, cfgCount);
  server.send(200, "text/plain", b);
}

// ------------------- Modbus callback -------------------
bool cb(Modbus::ResultCode event, uint16_t, void*){
  lastOk = (event == Modbus::EX_SUCCESS);

  if (wsClient != 255 && event != Modbus::EX_SUCCESS){
    char b[64];
    snprintf(b, sizeof(b), "MODBUS_ERR:0x%02X", (unsigned)event);
    ws.sendTXT(wsClient, b);
  }
  return true;
}

// ------------------- Ticker -------------------
void onReadTick(){ flagRead = true; }

// ------------------- Envio cru -------------------
void sendRegs(){
  if (wsClient == 255) return;

  // Monta um texto cru, com dec e hex
  // (evita JSON pra ficar “sem formatação”)
  String s;
  s.reserve(1200);

  char head[80];
  snprintf(head, sizeof(head), "ADDR 0x%04X COUNT %u\n", cfgAddr, cfgCount);
  s += head;

  // linha decimal
  for (uint16_t i=0; i<cfgCount; i++){
    s += String(regs[i]);
    if (i+1 < cfgCount) s += ' ';
  }
  s += "\n";

  // linha hex
  for (uint16_t i=0; i<cfgCount; i++){
    char hx[8];
    snprintf(hx, sizeof(hx), "0x%04X", regs[i]);
    s += hx;
    if (i+1 < cfgCount) s += ' ';
  }

  ws.sendTXT(wsClient, s);
}

// ------------------- Setup -------------------
void setup(){
  // Modbus master
  rtu.master();
  Serial.begin(RS485_BAUD);
  rtu.begin(&Serial, DE_RE);
  //rtu.setTimeout(600);

  // Wi-Fi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // HTTP
  server.on("/", handleRoot);
  server.on("/cfg", handleCfg);
  server.begin();

  // WS
  ws.begin();
  ws.onEvent(wsEvent);

  // leitura a cada 5s
  tRead.attach(5.0, onReadTick);

  // primeira leitura logo ao iniciar
  flagForceRead = true;
}

// ------------------- Loop -------------------
void loop(){
  server.handleClient();
  ws.loop();
  rtu.task();
  yield();

  // dispara leitura periódica ou leitura imediata após confirmar
  if ((flagRead || flagForceRead) && !rtu.slave()){
    flagRead = false;
    flagForceRead = false;

    // limpa buffer local (opcional)
    for (uint16_t i=0; i<cfgCount; i++) regs[i] = 0;

    rtu.readHreg(SLAVE_ID, cfgAddr, regs, cfgCount, cb);
  }

  // quando terminar a transação, envia
  static bool wasBusy = false;
  bool busy = rtu.slave();

  if (wasBusy && !busy){
    // terminou agora → manda o que leu
    sendRegs();
  }
  wasBusy = busy;
}
