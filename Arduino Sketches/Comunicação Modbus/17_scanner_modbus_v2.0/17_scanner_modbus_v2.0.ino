// =======================
// Scanner Modbus - v1 (Parte 1: Procurar Slaves + UI + FSM)
// ESP8266 + ModbusRTU + WebSocket
// =======================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "RTClib.h"
#include <ModbusRTU.h>

// Declarado em WiFi_Manager.ino
void wifiManagerBegin(ESP8266WebServer& server, const char* apSsid, const char* apPass, uint32_t staTimeoutMs = 12000);

// ================= OBJETOS =================
RTC_DS1307 rtc;
ModbusRTU rtu;
ESP8266WebServer server(80);
WebSocketsServer ws(81);

// JSON doc (payload vindo do browser)
StaticJsonDocument<768> doc;

// ================= WIFI AP =================
const char* AP_SSID = "Scanner Modbus";
const char* AP_PASS = "1234567890";

// ================= PINOS ===================
#define LED_PIN           LED_BUILTIN
#define DE_RE             12
#define BTN_PIN           0
#define HALF_OR_FULL_PIN  13

// ================= MODBUS ==================
#define RS485_BAUD 9600
static const uint16_t MODBUS_TIMEOUT_MS = 350;

// ================= WS ======================
static uint8_t wsClient = 255;

// ================= UTIL (timestamp) ========
bool rtcOk = false;

String tsNow() {
  if (rtcOk) {
    DateTime n = rtc.now();
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d:%02d", n.hour(), n.minute(), n.second());
    return String(b);
  }
  uint32_t s = millis() / 1000;
  uint8_t hh = (s / 3600) % 24;
  uint8_t mm = (s / 60) % 60;
  uint8_t ss = s % 60;
  char b[16];
  snprintf(b, sizeof(b), "%02u:%02u:%02u", hh, mm, ss);
  return String(b);
}

void wsLog(const String& msg) {
  String m;
  if (wsClient == 255) return;
  m = "LOG: [" + tsNow() + "] " + msg;
  ws.sendTXT(wsClient, m);
}
void wsErr(const String& msg) {
  String m;
  if (wsClient == 255) return;
  m = "ERR: [" + tsNow() + "] " + msg;
  ws.sendTXT(wsClient, m);
}

// ================= HTML ====================
const char HTML_PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Scanner Modbus</title>
<style>
  :root{
    --bg:#0b1220; --panel:#0f1a30; --card:#111f3a;
    --text:#e9eefc; --muted:#a9b6d3; --primary:#4ea1ff; --primary2:#2b74ff;
    --border:rgba(255,255,255,.10); --ok:#00e676; --warn:#ffca28; --bad:#ff5252;
    --console:#05070d; --shadow: 0 14px 30px rgba(0,0,0,.35); --radius:16px;
  }
  *{ box-sizing:border-box; }
  body{
    margin:0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
    color:var(--text);
    background: radial-gradient(1200px 700px at 20% 0%, #152a55 0%, var(--bg) 50%, #070b14 100%);
    padding: 18px;
  }
  .wrap{ max-width: 980px; margin: 0 auto; display: grid; grid-template-columns: 1.15fr .85fr; gap: 14px; }
  @media (max-width: 900px){ .wrap{ grid-template-columns: 1fr; } }
  .topbar{
    grid-column: 1 / -1; display:flex; align-items:center; justify-content:space-between; gap:12px;
    padding:14px 16px; background: rgba(255,255,255,.05); border:1px solid var(--border);
    border-radius: var(--radius); box-shadow: var(--shadow); backdrop-filter: blur(8px);
  }
  .title{ display:flex; flex-direction:column; gap:2px; }
  .title h1{ font-size: 18px; margin:0; letter-spacing:.2px; }
  .title p{ margin:0; font-size: 12px; color: var(--muted); }
  .pill{ padding:6px 10px; border-radius:999px; border:1px solid var(--border); color: var(--muted); font-size:12px; }
  .card{ background: rgba(255,255,255,.04); border: 1px solid var(--border); border-radius: var(--radius); box-shadow: var(--shadow); overflow:hidden; }
  .cardHead{ padding:12px 14px; border-bottom:1px solid var(--border); display:flex; align-items:center; justify-content:space-between; gap:10px; }
  .cardHead h2{ margin:0; font-size:14px; letter-spacing:.2px; }
  .cardBody{ padding:14px; }
  .tabs{ display:flex; gap:8px; flex-wrap:wrap; }
  .tabBtn{
    border:1px solid var(--border); background: rgba(255,255,255,.03); color: var(--muted);
    padding:9px 12px; border-radius: 12px; cursor:pointer; font-size: 13px; transition: .15s ease;
  }
  .tabBtn.active{ background: linear-gradient(180deg, rgba(78,161,255,.25), rgba(43,116,255,.15)); color: var(--text); border-color: rgba(78,161,255,.35); }
  .tabBtn:hover{ transform: translateY(-1px); }
  .grid2{ display:grid; grid-template-columns: 1fr 1fr; gap:10px; }
  @media (max-width: 520px){ .grid2{ grid-template-columns: 1fr; } }
  label{ font-size: 12px; color: var(--muted); display:block; margin-bottom: 6px; }
  input, select{
    width:100%; padding: 10px 10px; border-radius: 12px; border:1px solid var(--border);
    background: rgba(0,0,0,.25); color: var(--text); outline:none;
  }
  .btnRow{ display:flex; gap:10px; flex-wrap:wrap; margin-top:12px; }
  .btn{
    border:none; cursor:pointer; padding: 10px 14px; border-radius: 12px; font-weight:600; font-size: 13px;
    color: #08101f; background: linear-gradient(180deg, var(--primary), var(--primary2));
    box-shadow: 0 10px 20px rgba(43,116,255,.25);
  }
  .btn:disabled{ opacity:.55; cursor:not-allowed; }
  .btn.secondary{ color: var(--text); background: rgba(255,255,255,.06); border:1px solid var(--border); box-shadow:none; }
  .hint{ font-size: 12px; color: var(--muted); margin-top:10px; line-height:1.35; }
  .statusBox{ display:flex; align-items:center; gap:10px; padding:10px 12px; border-radius: 12px; border:1px solid var(--border); background: rgba(0,0,0,.22); margin-top: 12px; }
  .dot{ width: 10px; height: 10px; border-radius: 999px; background: var(--warn); box-shadow: 0 0 0 6px rgba(255,202,40,.12); }
  .dot.ok{ background: var(--ok); box-shadow: 0 0 0 6px rgba(0,230,118,.12); }
  .dot.bad{ background: var(--bad); box-shadow: 0 0 0 6px rgba(255,82,82,.12); }
  .mono{ font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
  table{ width:100%; border-collapse: collapse; overflow:hidden; border-radius: 12px; border:1px solid var(--border); background: rgba(0,0,0,.22); }
  th, td{ padding: 10px 10px; border-bottom: 1px solid rgba(255,255,255,.08); font-size: 13px; text-align:left; }
  th{ color: var(--muted); font-size:12px; }
  tr:last-child td{ border-bottom:none; }
  .badge{ display:inline-flex; align-items:center; gap:6px; padding:4px 8px; border-radius: 999px; font-size: 12px; border:1px solid var(--border); }
  .badge.ok{ border-color: rgba(0,230,118,.35); color: var(--ok); background: rgba(0,230,118,.08); }
  .badge.warn{ border-color: rgba(255,202,40,.35); color: var(--warn); background: rgba(255,202,40,.08); }
  .badge.bad{ border-color: rgba(255,82,82,.35); color: var(--bad); background: rgba(255,82,82,.08); }
  #console{
    height: 320px; overflow:auto; white-space: pre-wrap; background: var(--console);
    border:1px solid rgba(255,255,255,.10); border-radius: 12px; padding: 10px; font-size: 12px; color: #cfe3ff;
  }
  .logErr{ color: #ff7b7b; } .logOk{ color: #7bffb3; } .logWarn{ color: #ffe27b; }
  .panelTitle{ font-size: 12px; color: var(--muted); margin: 0 0 8px 0; }
  .lastOut{ padding: 10px 12px; border-radius: 12px; border:1px solid var(--border); background: rgba(0,0,0,.22); min-height: 130px; }
</style>
</head>
<body>
  <div class="wrap">
    <div class="topbar">
      <div class="title">
        <h1>Scanner Modbus</h1>
        <p>AP local • WebSocket • Diagnóstico RTU • <a href="/wifi" style="color:#a9b6d3">WiFi</a></p>
      </div>
      <div class="pill mono" id="wsStatus">WS: desconectado</div>
    </div>

    <div class="card">
      <div class="cardHead">
        <h2>Operações</h2>
        <div class="tabs">
          <button class="tabBtn active" data-tab="tabScan">Procurar Slaves</button>
          <button class="tabBtn" data-tab="tabRead">Ler Registradores</button>
          <button class="tabBtn" data-tab="tabWrite">Escrever Registradores</button>
        </div>
      </div>

      <div class="cardBody">
        <div id="tabScan" class="tab">
          <div class="grid2">
            <div><label>ID inicial</label><input id="scanIdStart" type="number" min="1" max="247" value="1"></div>
            <div><label>ID final</label><input id="scanIdEnd" type="number" min="1" max="247" value="247"></div>
            <div><label>Registrador teste (ping)</label><input id="scanTestReg" type="number" min="0" max="65535" value="0"></div>
            <div>
              <label>Função teste</label>
              <select id="scanTestFn">
                <option value="3" selected>03 - Holding Register</option>
                <option value="4">04 - Input Register</option>
                <option value="1">01 - Coils</option>
                <option value="2">02 - Discrete Inputs</option>
              </select>
            </div>
          </div>

          <div class="btnRow">
            <button class="btn" id="btnScanStart" onclick="scanStart()">Iniciar busca</button>
            <button class="btn secondary" id="btnScanStop" onclick="scanStop()" disabled>Parar</button>
            <button class="btn secondary" onclick="clearFound()">Limpar lista</button>
          </div>

          <div class="statusBox">
            <div class="dot" id="scanDot"></div>
            <div>
              <div class="mono" id="scanStatus">Aguardando...</div>
              <div class="hint" id="scanHint">Dica: comece com ID 1..20 para validar comunicação.</div>
            </div>
          </div>

          <div style="margin-top:14px;">
            <p class="panelTitle">Slaves encontrados</p>
            <table>
              <thead><tr><th style="width:80px;">ID</th><th>Status</th><th>Detalhe</th></tr></thead>
              <tbody id="foundTable">
                <tr><td class="mono">-</td><td><span class="badge warn">vazio</span></td><td class="mono">---</td></tr>
              </tbody>
            </table>
          </div>
        </div>

        <div id="tabRead" class="tab" style="display:none;">
          <div class="hint">Aba será implementada na Parte 2.</div>
          <div class="lastOut mono" style="margin-top:12px;">Em construção.</div>
        </div>

        <div id="tabWrite" class="tab" style="display:none;">
          <div class="hint">Aba será implementada na Parte 3.</div>
          <div class="lastOut mono" style="margin-top:12px;">Em construção.</div>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="cardHead">
        <h2>Saídas</h2>
        <button class="btn secondary" onclick="clearConsole()">Limpar console</button>
      </div>
      <div class="cardBody">
        <p class="panelTitle">Última saída</p>
        <div class="lastOut mono" id="lastOutput">Nenhuma operação executada.</div>

        <p class="panelTitle" style="margin-top:14px;">Console</p>
        <div id="console" class="mono"></div>
      </div>
    </div>
  </div>

<script>
  document.querySelectorAll('.tabBtn').forEach(btn=>{
    btn.addEventListener('click', ()=>{
      document.querySelectorAll('.tabBtn').forEach(b=>b.classList.remove('active'));
      btn.classList.add('active');
      const tabId = btn.dataset.tab;
      document.querySelectorAll('.tab').forEach(t=>t.style.display='none');
      document.getElementById(tabId).style.display='block';
    });
  });

  const consoleDiv = document.getElementById('console');
  function addLine(text, cls=""){
    const d = document.createElement('div');
    if(cls) d.className = cls;
    d.textContent = text;
    consoleDiv.appendChild(d);
    consoleDiv.scrollTop = consoleDiv.scrollHeight;
  }
  function clearConsole(){ consoleDiv.innerHTML=""; }

  const foundTable = document.getElementById('foundTable');
  let found = {};
  function renderFound(){
    foundTable.innerHTML = "";
    const ids = Object.keys(found).map(n=>parseInt(n)).sort((a,b)=>a-b);
    if(ids.length===0){
      foundTable.innerHTML = `<tr><td class="mono">-</td><td><span class="badge warn">vazio</span></td><td class="mono">---</td></tr>`;
      return;
    }
    for(const id of ids){
      const item = found[id];
      const badgeClass = item.status === "RESP" ? "ok" : (item.status === "EXC" ? "warn" : "bad");
      const badgeText  = item.status === "RESP" ? "respondeu" : (item.status === "EXC" ? "exceção" : "erro");
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td class="mono">${id}</td>
        <td><span class="badge ${badgeClass}">${badgeText}</span></td>
        <td class="mono">${item.detail || ""}</td>
      `;
      foundTable.appendChild(tr);
    }
  }
  function clearFound(){ found = {}; renderFound(); setLastOutput("Lista limpa."); }

  function setLastOutput(text){ document.getElementById('lastOutput').textContent = text; }

  // WS: usa o host atual (funciona em AP e em STA)
  const ws = new WebSocket(`ws://${location.hostname}:81/`);
  const wsStatus = document.getElementById('wsStatus');
  let busy = false;

  ws.onopen  = () => { wsStatus.textContent = "WS: conectado"; addLine("[browser] WS conectado", "logOk"); };
  ws.onerror = () => { wsStatus.textContent = "WS: erro"; addLine("[browser] WS erro", "logErr"); };
  ws.onclose = () => { wsStatus.textContent = "WS: desconectado"; addLine("[browser] WS desconectado", "logWarn"); };

  ws.onmessage = (e) => {
    const msg = String(e.data);
    if (msg === "busy") { busy = true; addLine("Dispositivo ocupado.", "logWarn"); return; }
    if (msg.startsWith("LOG: ")) { addLine(msg.substring(5)); return; }
    if (msg.startsWith("ERR: ")) { addLine(msg.substring(5), "logErr"); return; }

    if (msg.startsWith("EVT: ")) {
      const payload = msg.substring(5);
      let d=null;
      try { d = JSON.parse(payload); } catch(err){ addLine("Falha ao parsear EVT JSON", "logErr"); return; }

      if(d.type === "scan-status"){
        document.getElementById('scanStatus').textContent = d.text;
        document.getElementById('scanHint').textContent = d.hint || "";
        const dot = document.getElementById('scanDot');
        dot.classList.remove('ok','bad');
        if(d.level==="ok") dot.classList.add('ok');
        else if(d.level==="bad") dot.classList.add('bad');
        setLastOutput(d.lastOutput || "Busca...");
      }
      if(d.type === "slave-found"){ found[d.id] = { status: d.status, detail: d.detail }; renderFound(); }
      if(d.type === "scan-done"){ busy = false; setScanButtons(false); addLine("Busca finalizada.", "logOk"); }
      return;
    }
  };

  function setScanButtons(running){
    document.getElementById('btnScanStart').disabled = running;
    document.getElementById('btnScanStop').disabled = !running;
  }

  function clampInt(v, min, max){
    let n = parseInt(v);
    if(Number.isNaN(n)) n = min;
    if(n < min) n = min;
    if(n > max) n = max;
    return n;
  }

  function scanStart(){
    if(busy){ addLine("O dispositivo está ocupado.", "logWarn"); return; }
    const idStart = clampInt(document.getElementById('scanIdStart').value, 1, 247);
    const idEnd   = clampInt(document.getElementById('scanIdEnd').value, 1, 247);
    const testReg = clampInt(document.getElementById('scanTestReg').value, 0, 65535);
    const testFn  = clampInt(document.getElementById('scanTestFn').value, 1, 4);

    document.getElementById('scanIdStart').value = idStart;
    document.getElementById('scanIdEnd').value   = idEnd;

    busy = true;
    setScanButtons(true);
    setLastOutput(`Iniciando busca (${idStart}..${idEnd})...`);

    ws.send(JSON.stringify({ action:"scan-slaves-start", idStart, idEnd, testReg, testFn }));
  }

  function scanStop(){
    if(!busy) return;
    ws.send(JSON.stringify({ action:"scan-slaves-stop" }));
    addLine("Solicitado parar busca...", "logWarn");
  }
</script>
</body>
</html>
)HTMLPAGE";

// ================== FSM (tarefas) ==================
enum TaskState : uint8_t {
  TASK_IDLE = 0,
  TASK_SCAN_SLAVES_SEND,
  TASK_SCAN_SLAVES_WAIT
};

TaskState taskState = TASK_IDLE;
bool taskRunning = false;

uint8_t scanIdStart = 1;
uint8_t scanIdEnd   = 247;
uint8_t scanIdCur   = 1;

uint16_t scanTestReg = 0;
uint8_t  scanTestFn  = 3;
uint16_t scanRegBuf[2] = {0};
bool     scanBitBuf[2] = {false};

uint32_t taskTimer = 0;

volatile bool cbGot = false;
volatile uint8_t cbLastRC = 0;

bool cb(Modbus::ResultCode rc, uint16_t, void*) {
  cbGot = true;
  cbLastRC = (uint8_t)rc;
  return true;
}

void wsEvt(const String& json) {
  String m;
  if (wsClient == 255) return;
  m = "EVT: " + json;
  ws.sendTXT(wsClient, m);
}

void scanSlavesStart(uint8_t idS, uint8_t idE, uint16_t tReg, uint8_t tFn) {
  scanIdStart = idS;
  scanIdEnd   = idE;
  scanIdCur   = scanIdStart;
  scanTestReg = tReg;
  scanTestFn  = tFn;

  cbGot = false;
  taskRunning = true;
  taskState = TASK_SCAN_SLAVES_SEND;

  wsLog("Busca de slaves iniciada.");
  wsEvt("{\"type\":\"scan-status\",\"level\":\"ok\",\"text\":\"Executando...\",\"hint\":\"Varredura em andamento.\",\"lastOutput\":\"Buscando slaves...\"}");
}

void scanSlavesStop() {
  if (!taskRunning) return;
  taskRunning = false;
  taskState = TASK_IDLE;

  wsLog("Busca de slaves interrompida.");
  wsEvt("{\"type\":\"scan-status\",\"level\":\"bad\",\"text\":\"Interrompido\",\"hint\":\"Busca cancelada.\",\"lastOutput\":\"Busca interrompida.\"}");
  wsEvt("{\"type\":\"scan-done\"}");
}

void scanSend() {
  if (scanIdCur > scanIdEnd) {
    taskRunning = false;
    taskState = TASK_IDLE;
    wsLog("Busca de slaves finalizada.");
    wsEvt("{\"type\":\"scan-status\",\"level\":\"ok\",\"text\":\"Concluído\",\"hint\":\"Busca finalizada.\",\"lastOutput\":\"Busca concluída.\"}");
    wsEvt("{\"type\":\"scan-done\"}");
    return;
  }

  if (rtu.slave()) return;
  cbGot = false;

  bool ok = false;
  switch (scanTestFn) {
    case 1: ok = rtu.readCoil(scanIdCur, scanTestReg, scanBitBuf, 1, cb); break;
    case 2: ok = rtu.readIsts(scanIdCur, scanTestReg, scanBitBuf, 1, cb); break;
    case 3: ok = rtu.readHreg(scanIdCur, scanTestReg, scanRegBuf, 1, cb); break;
    case 4: ok = rtu.readIreg(scanIdCur, scanTestReg, scanRegBuf, 1, cb); break;
    default: ok = rtu.readHreg(scanIdCur, scanTestReg, scanRegBuf, 1, cb); break;
  }

  if (!ok) {
    wsErr("Modbus ocupado ao enviar (aguardando).");
    return;
  }

  taskTimer = millis();
  taskState = TASK_SCAN_SLAVES_WAIT;

  char st[64];
  snprintf(st, sizeof(st), "Testando ID %u ...", scanIdCur);
  wsEvt(String("{\"type\":\"scan-status\",\"level\":\"ok\",\"text\":\"") + st + String("\",\"hint\":\"Se a rede estiver ruidosa, aumente timeout.\",\"lastOutput\":\"Varredura...\"}"));
}

void scanWait() {
  if (!rtu.slave()) {
    if (cbGot) {
      const bool isSuccess = (cbLastRC == (uint8_t)Modbus::EX_SUCCESS);
      String status = isSuccess ? "RESP" : "EXC";

      char det[64];
      if (isSuccess) snprintf(det, sizeof(det), "OK fn=%u reg=%u", scanTestFn, scanTestReg);
      else snprintf(det, sizeof(det), "RC=0x%02X fn=%u reg=%u", cbLastRC, scanTestFn, scanTestReg);

      wsEvt(String("{\"type\":\"slave-found\",\"id\":") + scanIdCur + String(",\"status\":\"") + status + String("\",\"detail\":\"") + det + String("\"}"));
      wsLog(String("ID ") + scanIdCur + (isSuccess ? " respondeu." : " respondeu com exceção (ainda existe)."));
    }

    scanIdCur++;
    taskState = TASK_SCAN_SLAVES_SEND;
    return;
  }

  if (millis() - taskTimer > MODBUS_TIMEOUT_MS) {
    cbGot = false;
    wsLog(String("ID ") + scanIdCur + " sem resposta (timeout).");
    scanIdCur++;
    taskState = TASK_SCAN_SLAVES_SEND;
  }
}

void wsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsClient = num;
      wsLog("Cliente conectado.");
      break;

    case WStype_DISCONNECTED:
      if (wsClient == num) wsClient = 255;
      break;

    case WStype_TEXT: {
      DeserializationError err = deserializeJson(doc, (char*)payload, length);
      if (err) { ws.sendTXT(num, "ERR: JSON inválido."); return; }

      const char* action = doc["action"] | "";
      if (!action[0]) { ws.sendTXT(num, "ERR: action ausente."); return; }

      if (strcmp(action, "scan-slaves-start") == 0) {
        if (taskRunning) { ws.sendTXT(num, "busy"); return; }

        uint8_t idS = doc["idStart"] | 1;
        uint8_t idE = doc["idEnd"]   | 247;
        uint16_t tReg = doc["testReg"] | 0;
        uint8_t tFn = doc["testFn"] | 3;

        if (idS < 1) idS = 1;
        if (idE > 247) idE = 247;
        if (idE < idS) { uint8_t tmp = idS; idS = idE; idE = tmp; }
        if (tFn < 1 || tFn > 4) tFn = 3;

        scanSlavesStart(idS, idE, tReg, tFn);
        return;
      }

      if (strcmp(action, "scan-slaves-stop") == 0) {
        scanSlavesStop();
        return;
      }

      wsErr(String("Ação desconhecida: ") + action);
    } break;

    default: break;
  }
}

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void setup() {
  Serial.begin(115200);

  // Modbus master
  rtu.master();
  Serial.swap();               // opcional dependendo do seu hardware; remova se atrapalhar
  Serial.begin(RS485_BAUD);
  rtu.begin(&Serial, DE_RE);

  // WiFi: AP sempre + tentativa STA via EEPROM + rotas /wifi
  wifiManagerBegin(server, AP_SSID, AP_PASS, 12000);

  // HTTP
  server.on("/", handleRoot);
  server.begin();

  // WS
  ws.begin();
  ws.onEvent(wsEvent);

  // I2C + RTC
  Wire.begin(4, 5);

  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);

  digitalWrite(HALF_OR_FULL_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  delay(300);

  rtcOk = false;
  for (int i = 0; i < 5; i++) {
    if (rtc.begin()) { rtcOk = true; break; }
    delay(150);
  }
}

void loop() {
  ws.loop();
  server.handleClient();
  rtu.task();
  yield();

  if (!taskRunning) return;

  switch (taskState) {
    case TASK_SCAN_SLAVES_SEND: scanSend(); break;
    case TASK_SCAN_SLAVES_WAIT: scanWait(); break;
    default: break;
  }
}