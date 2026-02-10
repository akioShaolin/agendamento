// -----------------------------------------------------------------------------
// ---------------------- Declaração de Bibliotecas ----------------------------
// -----------------------------------------------------------------------------

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include <Ticker.h>
#include <WebSocketsServer.h>
#include <ModbusRTU.h>

// -----------------------------------------------------------------------------
// ------------------- Declaração de Objetos externos --------------------------
// -----------------------------------------------------------------------------

RTC_DS1307 rtc;
Ticker ticker;
Ticker rtc_sync;
ModbusRTU rtu;
ESP8266WebServer server(80);
WebSocketsServer ws(81);

// -----------------------------------------------------------------------------
// -------------------------- CONFIGURAÇÕES DO WIFI ----------------------------
// -----------------------------------------------------------------------------

const char* ssid = "InjecaoProg";
const char* password = "1234567890";

// -----------------------------------------------------------------------------
// ------------------------------- PÁGINA HTML ---------------------------------
// -----------------------------------------------------------------------------

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
  <title>Injeção Programada</title>

  <style>
    :root{
      --bg:#f2f2f2;
      --card:#ffffff;
      --primary:#1976d2;
      --text:#1f2937;
      --muted:#6b7280;
      --border:#e5e7eb;

      --console-bg:#0b0f14;
      --console-text:#d1d5db;
      --console-log:#22c55e;
      --console-error:#ef4444;
      --console-warn:#f59e0b;
    }

    *{ box-sizing:border-box; }
    html, body{ height:100%; }
    body{
      margin:0;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, Helvetica, sans-serif;
      background: var(--bg);
      color: var(--text);
      padding: 18px;
      -webkit-text-size-adjust: 100%;
    }

    .wrap{
      max-width: 520px;
      margin: 0 auto;
    }

    .card{
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 16px;
      box-shadow: 0 8px 18px rgba(0,0,0,.08);
    }

    header{
      display:flex;
      align-items:center;
      justify-content:space-between;
      gap: 12px;
      margin-bottom: 10px;
    }

    h1{
      font-size: 1.25rem;
      margin:0;
      color: var(--primary);
      letter-spacing: .2px;
    }

    .status{
      display:flex;
      align-items:center;
      gap:8px;
      font-size: .9rem;
      color: var(--muted);
      white-space: nowrap;
    }

    .dot{
      width:10px; height:10px;
      border-radius:999px;
      background: #9ca3af;
      box-shadow: 0 0 0 3px rgba(156,163,175,.25);
    }
    .dot.ok{
      background: #22c55e;
      box-shadow: 0 0 0 3px rgba(34,197,94,.25);
    }
    .dot.bad{
      background: #ef4444;
      box-shadow: 0 0 0 3px rgba(239,68,68,.25);
    }

    .grid{
      display:grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
      margin-top: 10px;
    }

    .metric{
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 12px;
      background: #fafafa;
    }

    .metric .label{
      font-size:.85rem;
      color: var(--muted);
      margin-bottom: 6px;
    }
    .metric .value{
      font-size: 1.05rem;
      font-weight: 600;
      line-height: 1.25;
      word-break: break-word;
    }

    hr{
      border:0;
      border-top: 1px solid var(--border);
      margin: 14px 0;
    }

    h2{
      font-size: 1.02rem;
      margin: 0 0 10px 0;
    }

    .rtc-grid{
      display:grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
    }

    .field{
      display:flex;
      flex-direction:column;
      gap:6px;
    }

    .field label{
      font-size: .78rem;
      color: var(--muted);
    }

    input[type="number"]{
      width:100%;
      padding: 12px 10px;
      border-radius: 10px;
      border: 1px solid var(--border);
      font-size: 1rem;
      outline: none;
      background: #fff;
    }
    input[type="number"]:focus{
      border-color: rgba(25,118,210,.55);
      box-shadow: 0 0 0 3px rgba(25,118,210,.15);
    }

    .actions{
      display:flex;
      gap:10px;
      margin-top: 12px;
      flex-wrap: wrap;
    }

    button{
      appearance:none;
      border:0;
      border-radius: 10px;
      padding: 12px 14px;
      font-size: 1rem;
      cursor:pointer;
      background: var(--primary);
      color: #fff;
      font-weight: 600;
      min-width: 120px;
    }
    button.secondary{
      background: #111827;
    }
    button.ghost{
      background: transparent;
      color: var(--text);
      border: 1px solid var(--border);
      font-weight: 600;
    }
    button:active{ transform: translateY(1px); }

    .hint{
      font-size: .85rem;
      color: var(--muted);
      margin-top: 8px;
      line-height: 1.35;
    }

    .console{
      background: var(--console-bg);
      border-radius: 12px;
      border: 1px solid rgba(255,255,255,.06);
      padding: 10px;
    }

    .console-top{
      display:flex;
      align-items:center;
      justify-content:space-between;
      gap: 10px;
      margin-bottom: 8px;
    }

    .console-title{
      font-weight: 700;
      color: #e5e7eb;
      font-size: .95rem;
    }

    .console-actions{
      display:flex;
      gap: 8px;
      align-items:center;
    }

    .toggle{
      display:flex;
      align-items:center;
      gap: 6px;
      color: #cbd5e1;
      font-size: .85rem;
      user-select:none;
    }

    .toggle input{
      width: 16px;
      height: 16px;
    }

    #consoleBody{
      height: 200px;
      overflow: auto;
      white-space: pre-wrap;
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      font-size: .82rem;
      line-height: 1.35;
      color: var(--console-text);
    }

    .line{ display:block; margin: 0 0 4px 0; }
    .log{ color: var(--console-log); }
    .err{ color: var(--console-error); }
    .warn{ color: var(--console-warn); }

    @media (max-width: 380px){
      body{ padding: 12px; }
      .rtc-grid{ grid-template-columns: 1fr 1fr; }
      button{ width: 100%; }
      .grid{ grid-template-columns: 1fr; }
    }
  </style>
</head>

<body>
  <div class="wrap">
    <div class="card">
      <header>
        <h1>Injeção Programada</h1>
        <div class="status" title="Status do WebSocket">
          <span id="dot" class="dot"></span>
          <span id="wsStatus">Desconectado</span>
        </div>
      </header>

      <div class="grid">
        <div class="metric">
          <div class="label">Data/Hora</div>
          <div id="hora" class="value">--</div>
        </div>

        <div class="metric">
          <div class="label">Potência</div>
          <div id="power" class="value">--</div>
        </div>
      </div>

      <hr />

      <h2>Ajustar RTC (teste)</h2>

      <div class="rtc-grid">
        <div class="field">
          <label for="d">Dia</label>
          <input id="d" type="number" inputmode="numeric" placeholder="DD" min="1" max="31" />
        </div>
        <div class="field">
          <label for="m">Mês</label>
          <input id="m" type="number" inputmode="numeric" placeholder="MM" min="1" max="12" />
        </div>
        <div class="field">
          <label for="y">Ano</label>
          <input id="y" type="number" inputmode="numeric" placeholder="AAAA" min="2000" max="2099" />
        </div>

        <div class="field">
          <label for="hh">Hora</label>
          <input id="hh" type="number" inputmode="numeric" placeholder="HH" min="0" max="23" />
        </div>
        <div class="field">
          <label for="mm">Min</label>
          <input id="mm" type="number" inputmode="numeric" placeholder="MM" min="0" max="59" />
        </div>
        <div class="field">
          <label for="ss">Seg</label>
          <input id="ss" type="number" inputmode="numeric" placeholder="SS" min="0" max="59" />
        </div>
      </div>

      <div class="actions">
        <button id="btnSet" type="button">Ajustar</button>
        <button id="btnNow" type="button" class="ghost">Usar hora do dispositivo</button>
      </div>

      <div class="hint">
        Depois do teste, o RTC será sincronizado automaticamente com o inversor.
        O console abaixo mostra os logs enviados pelo ESP8266.
      </div>

      <hr />

      <div class="console">
        <div class="console-top">
          <div class="console-title">Console</div>

          <div class="console-actions">
            <label class="toggle" title="Manter rolagem no fim">
              <input id="autoScroll" type="checkbox" checked />
              Auto-scroll
            </label>
            <button id="btnClear" type="button" class="secondary">Limpar</button>
          </div>
        </div>

        <div id="consoleBody" aria-live="polite"></div>
      </div>
    </div>
  </div>

  <script>
    // ---------- Utilidades ----------
    const $ = (id) => document.getElementById(id);

    function pad2(n){ return String(n).padStart(2, "0"); }

    function dowPtBr(dow){
      switch (dow) {
        case 0: return "Domingo";
        case 1: return "Segunda-feira";
        case 2: return "Terça-feira";
        case 3: return "Quarta-feira";
        case 4: return "Quinta-feira";
        case 5: return "Sexta-feira";
        case 6: return "Sábado";
        default: return "";
      }
    }

    function setWsStatus(ok, text){
      $("wsStatus").textContent = text;
      const dot = $("dot");
      dot.classList.remove("ok", "bad");
      dot.classList.add(ok ? "ok" : "bad");
    }

    function addLine(text, kind="log"){
      const line = document.createElement("span");
      line.className = `line ${kind}`;
      const now = new Date().toLocaleTimeString();
      line.textContent = `[${now}] ${text}`;
      $("consoleBody").appendChild(line);

      if ($("autoScroll").checked){
        $("consoleBody").scrollTop = $("consoleBody").scrollHeight;
      }
    }

    function clamp(n, min, max){
      if (!Number.isFinite(n)) return null;
      return Math.max(min, Math.min(max, n));
    }

    function getNum(id){
      const v = Number($(id).value);
      return Number.isFinite(v) ? v : null;
    }

    function setInputsFromDate(dt){
      $("d").value  = dt.getDate();
      $("m").value  = dt.getMonth() + 1;
      $("y").value  = dt.getFullYear();
      $("hh").value = dt.getHours();
      $("mm").value = dt.getMinutes();
      $("ss").value = dt.getSeconds();
    }

    // ---------- RTC ajuste (HTTP /set) ----------
    async function ajustarRTC(){
      const d  = clamp(getNum("d"),  1, 31);
      const m  = clamp(getNum("m"),  1, 12);
      const y  = clamp(getNum("y"),  2000, 2099);
      const hh = clamp(getNum("hh"), 0, 23);
      const mm = clamp(getNum("mm"), 0, 59);
      const ss = clamp(getNum("ss"), 0, 59);

      if ([d,m,y,hh,mm,ss].some(v => v === null)){
        addLine("Preencha todos os campos do RTC corretamente.", "warn");
        return;
      }

      const url = `/set?d=${d}&m=${m}&y=${y}&hh=${hh}&mm=${mm}&ss=${ss}`;

      try{
        const res = await fetch(url, { cache: "no-store" });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        addLine(`RTC ajustado para ${pad2(d)}/${pad2(m)}/${y} ${pad2(hh)}:${pad2(mm)}:${pad2(ss)}.`, "log");
      }catch(err){
        addLine(`Falha ao ajustar RTC: ${err.message}`, "err");
      }
    }

    $("btnSet").addEventListener("click", ajustarRTC);

    $("btnNow").addEventListener("click", () => {
      setInputsFromDate(new Date());
      addLine("Campos preenchidos com a hora do dispositivo.", "log");
    });

    $("btnClear").addEventListener("click", () => {
      $("consoleBody").textContent = "";
    });

    // ---------- WebSocket ----------
    // Usa o host atual (melhor que fixar 192.168.4.1)
    const WS_PORT = 81;
    let ws;
    let reconnectTimer = null;

    function wsUrl(){
      const host = location.hostname || "192.168.4.1";
      return `ws://${host}:${WS_PORT}/`;
    }

    function connectWS(){
      if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;

      setWsStatus(false, "Conectando...");
      ws = new WebSocket(wsUrl());

      ws.onopen = () => {
        setWsStatus(true, "Conectado");
        addLine("WebSocket conectado.", "log");
      };

      ws.onerror = () => {
        // Alguns browsers chamam onerror sem detalhes
        addLine("Erro no WebSocket.", "err");
      };

      ws.onclose = () => {
        setWsStatus(false, "Desconectado");
        addLine("WebSocket desconectado. Tentando reconectar...", "warn");
        scheduleReconnect();
      };

      ws.onmessage = (e) => {
        const msg = String(e.data || "");

        if (msg.startsWith("LOG:")){
          addLine(msg, "log");
          return;
        }
        if (msg.startsWith("ERR:")){
          addLine(msg, "err");
          return;
        }

        // JSON de status
        if (msg.startsWith("{") && msg.endsWith("}")){
          let d;
          try{
            d = JSON.parse(msg);
          }catch{
            addLine("JSON inválido recebido.", "warn");
            return;
          }

          // Ajuste para o seu payload do ESP:
          // {"day":"..","month":"..","year":"..","hour":"..","minute":"..","second":"..","dayOfTheWeek":"..","power":".."}
          const day = Number(d.day);
          const month = Number(d.month);
          const year = Number(d.year);
          const hour = Number(d.hour);
          const minute = Number(d.minute);
          const second = Number(d.second);

          const dowKey = (d.dayOfTheWeek !== undefined) ? "dayOfTheWeek" : (d.dow !== undefined ? "dow" : null);
          const dow = dowKey ? Number(d[dowKey]) : NaN;

          // power pode vir number ou string
          const p = Number(d.power);

          const dowText = Number.isFinite(dow) ? dowPtBr(dow) : "";
          $("hora").textContent =
            `${pad2(day)}/${pad2(month)}/${String(year).padStart(4,"0")} ${pad2(hour)}:${pad2(minute)}:${pad2(second)} ` +
            (dowText ? `\n${dowText}` : "");

          $("power").textContent = Number.isFinite(p) ? `${p.toFixed(2)} kW` : "--";
          return;
        }

        // Qualquer outro texto cai como log “neutro”
        if (msg.trim().length){
          addLine(msg, "log");
        }
      };
    }

    function scheduleReconnect(){
      if (reconnectTimer) return;
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connectWS();
      }, 1500);
    }

    // Preenche inputs com a hora do dispositivo inicialmente (facilita teste)
    setInputsFromDate(new Date());

    connectWS();
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

#define HR_ACT_PWR_OUT      0x9C8F    // Holding Register Active Power Output. Scale: 100 [W]
#define HR_EN_ACT_PWR_LIM   0x9D6B    // Holding Register Enable Active Power Limit. [0 - Off, 1 - On]
#define HR_ACT_PWR_LIM_VL   0x9D6C    // Holding Register Active Power Limit Value. Scale: 0.1 [%]  (0~1100)
#define HR_SYNC_START_REG   0xA712    // Holding Register to sync the RTC (year, month, day, hour, minute, second)
#define REG_COUNT 1

#define RTC_SYNC_REG_COUNT  6
#define RTC_SYNC_TIMEOUT    600 // ms

// -----------------------------------------------------------------------------
// --------------------------------- TICKER ------------------------------------
// -----------------------------------------------------------------------------
// Flags vinculadas aos Tickers para atualização e sincronização do RTC

volatile bool tickFlag = false;
volatile bool rtcSyncFlag = false;

// -----------------------------------------------------------------------------
// ------------------------- VARIÁVEIS DE CONTROLE -----------------------------
// -----------------------------------------------------------------------------

uint16_t reg_act_power = 0;
float power = 0;
String logs = "";

const uint16_t MAX_LOG_SIZE = 300;



#define POT_INV 100000 //Potência do inversor especificada em 100kW
#define NUM_PROGRAMS 3

// --- Programação dos eventos ---
// ID do programa
// Dia da Semana
// Hora de início
// Minuto de início
// Hora de fim
// Minuto de fim
// Potência
// Ativado

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

// -----------------------------------------------------------------------------
// ------------------------ SERVIÇO DE SINCRONIZAÇÃO ---------------------------
// -----------------------------------------------------------------------------

enum SyncState {
  SYNC_IDLE,            // Estado inicial
  SYNC_REQUEST,         // Dispara Leitura Modbus
  SYNC_WAIT_RESPONSE,   // Aguarda resposta (com timeout)
  SYNC_VALIDATE,        // Valida data/hora
  SYNC_APPLY,           // Ajusta RTC
  SYNC_DONE,            // Finalizado com sucesso
  SYNC_ERROR            // Falha (não tenta novamente)
};

SyncState rtcSyncState = SYNC_IDLE;

uint16_t rtcRegs[RTC_SYNC_REG_COUNT];
uint32_t rtcSyncTimestamp = 0;

// -----------------------------------------------------------------------------
// --------------------------------- ROTAS -------------------------------------
// -----------------------------------------------------------------------------

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
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
// -----------------------------------------------------------------------------
// -------------------------- WEB SOCKETS SERVER -------------------------------
// -----------------------------------------------------------------------------

uint8_t wsClient = 255;

void wsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length){
  
  switch (type){
    case WStype_CONNECTED:
      wsClient = num;
      break;

    case WStype_DISCONNECTED:
      if (wsClient == num) wsClient = 255;
      break;

  }
}

void handleData(DateTime now) {

  char buffer[128];
  snprintf(buffer, sizeof(buffer), "{\"day\": \"%d\", \"month\": \"%d\", \"year\": \"%d\", \"hour\": \"%d\", \"minute\": \"%d\", \"second\": \"%d\", \"dayOfTheWeek\": \"%d\", \"power\": \"%.2f\"}", now.day(), now.month(), now.year(), now.hour(), now.minute(), now.second(), now.dayOfTheWeek(), power);

  ws.sendTXT(wsClient, buffer);
}

// -----------------------------------------------------------------------------
// --------------------------------- TICKER ------------------------------------
// -----------------------------------------------------------------------------

void onTick() {
  tickFlag = true;  // Sinaliza que passou 1 segundo  
}

void onRtcSync() {
  rtcSyncFlag = true; // Sinaliza que se passaram 600 sgundos
}

// -----------------------------------------------------------------------------
// --------------------- SERVIÇO DE SINCRONIZAÇÃO DO RTC -----------------------
// -----------------------------------------------------------------------------

bool isValidDateTime(uint16_t y, uint16_t m, uint16_t d,
                     uint16_t hh, uint16_t mm, uint16_t ss) {

  if (y < 2020 || y > 2099) return false;
  if (m < 1 || m > 12) return false;
  if (d < 1 || d > 31) return false;
  if (hh > 23) return false;
  if (mm > 59) return false;
  if (ss > 59) return false;

  return true;
}

void rtcSyncTask() {

  switch (rtcSyncState) {

    case SYNC_IDLE:
      if (rtc.isrunning()) {
        rtcSyncState = SYNC_REQUEST;
      } else {
        if (wsClient != 255) {
          rtcSyncState = SYNC_ERROR;
        }
      }
      break;

    case SYNC_REQUEST:
      if (!rtu.slave()) {
        rtu.readHreg(SLAVE_1_ID,
                     HR_SYNC_START_REG,
                     rtcRegs,
                     RTC_SYNC_REG_COUNT,
                     cb);

        rtcSyncTimestamp = millis();
        rtcSyncState = SYNC_WAIT_RESPONSE;
      }
      break;

    case SYNC_WAIT_RESPONSE:
      rtu.task();

      if (!rtu.slave()) {
        rtcSyncState = SYNC_VALIDATE;
      }
      else if (millis() - rtcSyncTimestamp > RTC_SYNC_TIMEOUT) {
        rtcSyncState = SYNC_ERROR;
      }
      break;

    case SYNC_VALIDATE: {
      uint16_t y  = rtcRegs[0];
      uint16_t mo = rtcRegs[1];
      uint16_t d  = rtcRegs[2];
      uint16_t h  = rtcRegs[3];
      uint16_t mi = rtcRegs[4];
      uint16_t s  = rtcRegs[5];

      if (!isValidDateTime(y, mo, d, h, mi, s)) {
        rtcSyncState = SYNC_ERROR;
        break;
      }

      DateTime invTime(y, mo, d, h, mi, s);
      DateTime rtcTime = rtc.now();

      long diff = invTime.unixtime() - rtcTime.unixtime();

      if (abs(diff) > 2) {
        rtcSyncState = SYNC_APPLY;
      } else {
        rtcSyncState = SYNC_DONE;
      }
      break;
    }

    case SYNC_APPLY: {
      DateTime invTime(
        rtcRegs[0], rtcRegs[1], rtcRegs[2],
        rtcRegs[3], rtcRegs[4], rtcRegs[5]
      );
      rtc.adjust(invTime);
      rtcSyncState = SYNC_DONE;
      break;
    }

    case SYNC_DONE:{
      rtcSyncFlag = false;
      rtcSyncState = SYNC_IDLE;

      if (wsClient != 255) {
        char buffer[128];
        ws.sendTXT(wsClient, "LOG: Sincronização do RTC com o inversor realizada com sucesso.");
      }
      break;
    }

    case SYNC_ERROR:
      rtcSyncFlag = false;
      rtcSyncState = SYNC_IDLE;
      // Falha segura, não tenta novamente

      if (wsClient != 255) {
        char buffer[128];
        ws.sendTXT(wsClient, "ERR: Erro com a sincronização do RTC com o inversor.");
      }
      break;
  }
}

// -----------------------------------------------------------------------------
// ----------------------------- CALLBACK MODBUS -------------------------------
// -----------------------------------------------------------------------------

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

bool cb(Modbus::ResultCode event, uint16_t, void*) {
  char buffer [128];

  if (wsClient == 255) return true;
  if (event != Modbus::EX_SUCCESS) {
    snprintf(buffer, sizeof(buffer), "ERR: Modbus: 0x%02X. %s", event, 
        resultCode(event));
    ws.sendTXT(wsClient, buffer);
  }
  return true;
}

// -----------------------------------------------------------------------------
// ------------------------- CHECAGEM DE PROGRAMAS -----------------------------
// -----------------------------------------------------------------------------

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
  char buffer [128]; // Buffer para formatação da string

  uint32_t powerLimit = p.power;
  uint32_t pwrPercent = powerLimit * 1000 / POT_INV; // Escala de 0,1%. É preciso multiplicar por 1000 para ajustar os valores

  // 1. Habilita a Limitação de Potência
  if (wsClient != 255) {
    sprintf(buffer, "LOG: Escrevendo valor 0x%04X no registrador 0x%04X do inversor ID 0x%02X.<br>", limit_enable, HR_EN_ACT_PWR_LIM, SLAVE_1_ID);
    ws.sendTXT(wsClient, buffer);
    memset(buffer, 0, sizeof(buffer));
  }

  if (!rtu.slave()) { // Verifica se não há nenhuma transação em progresso
    rtu.writeHreg(SLAVE_1_ID, HR_EN_ACT_PWR_LIM, limit_enable, cb); 
    while(rtu.slave()) {
      rtu.task();
      delay(10);
    }
  }
  delay(100);

  // 2. Define o Limite de Potência
  if (wsClient != 255) {
    sprintf(buffer, "LOG: Escrevendo valor 0x%04X no registrador 0x%04X do inversor ID 0x%02X.<br>", pwrPercent, HR_ACT_PWR_LIM_VL, SLAVE_1_ID);
    ws.sendTXT(wsClient, buffer);
    memset(buffer, 0, sizeof(buffer));
  }

  if (!rtu.slave()) { // Verifica se não há nenhuma transação em progresso
    rtu.writeHreg(SLAVE_1_ID, HR_ACT_PWR_LIM_VL, pwrPercent, cb);
    while(rtu.slave()) {
      rtu.task();
      delay(10);
    } 
  }
  delay(100);

  if (wsClient != 255) {
    sprintf(buffer, "LOG: Potência do inversor ajustada para %.2f%.", (float)pwrPercent / 10);
    ws.sendTXT(wsClient, buffer);
    memset(buffer, 0, sizeof(buffer));
  }
}

// -----------------------------------------------------------------------------
// --------------------------------- SETUP -------------------------------------
// -----------------------------------------------------------------------------

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
  server.on("/set", handleSetRTC);
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
    if (wsClient != 255) {
      ws.sendTXT(wsClient, "ERR: Problema no RTC");
    }
    //Debug.println("RTC não encontrado!");
    digitalWrite(LED_PIN, HIGH);
    while(1) yield();
  }

  // --- Configuração do Timer1 hardware (ESP8266) ---
  ticker.attach(1.0, onTick); //Interrupção a cada 1 segundo
  //Debug.println("Iniciado.");
  rtc_sync.attach(600.0, onRtcSync);
}

// -----------------------------------------------------------------------------
// ---------------------------------- LOOP -------------------------------------
// -----------------------------------------------------------------------------

void loop() {

  server.handleClient();
  ws.loop();
  rtu.task();
  yield();

  if (rtcSyncFlag) {
    rtcSyncTask();
  } else if (tickFlag) {

    tickFlag = false;
    digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);

    DateTime now = rtc.now();
    if (wsClient != 255) handleData(now);
    
    // Essa é a linha mais importante do código
    checkPrograms(now);

    // Leitura da potência do inversor
    if (!rtu.slave() && wsClient != 255) { // Verifica se não há nenhuma transação em progresso e se está disponível para leitura
      rtu.readHreg(SLAVE_1_ID, HR_ACT_PWR_OUT, &reg_act_power, REG_COUNT, cb);  
      power = (float)reg_act_power / 10; // Divide por 10 para ter a unidade como kW
      while(rtu.slave()) {
        rtu.task();
        delay(10);
      }
    }
  }
}