// =======================
// WiFi_Manager.ino
// ESP8266 - AP + STA com EEPROM + página /wifi
// =======================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// -------- Config EEPROM --------
static const uint32_t WIFI_CFG_MAGIC = 0x57494649; // "WIFI"
static const uint16_t EEPROM_SIZE = 512;

struct WifiCfg {
  uint32_t magic;
  uint8_t  version;

  char ssid[33];
  char pass[65];

  uint8_t useDhcp; // 1 = DHCP, 0 = IP fixo

  uint8_t ip[4];
  uint8_t gw[4];
  uint8_t mask[4];
  uint8_t dns[4];

  uint8_t reserved[64]; // folga futura
};

static WifiCfg g_wifiCfg;

// AP padrão (sempre ligado)
static IPAddress AP_IP(192, 168, 4, 1);
static IPAddress AP_GW(192, 168, 4, 1);
static IPAddress AP_MASK(255, 255, 255, 0);

static const char* g_apSsid = "Scanner Modbus";
static const char* g_apPass = "1234567890";

// --------- Helpers ----------
static bool parseIP(const String& s, uint8_t out[4]) {
  int parts[4] = {0,0,0,0};
  int p = 0;
  String token = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '.') {
      if (token.length() == 0 || p > 3) return false;
      parts[p++] = token.toInt();
      token = "";
    } else if (isDigit(c)) {
      token += c;
    } else {
      return false;
    }
  }
  if (token.length() == 0 || p != 3) return false;
  parts[p] = token.toInt();

  for (int i = 0; i < 4; i++) {
    if (parts[i] < 0 || parts[i] > 255) return false;
    out[i] = (uint8_t)parts[i];
  }
  return true;
}

static IPAddress toIP(const uint8_t a[4]) {
  return IPAddress(a[0], a[1], a[2], a[3]);
}

static void cfgDefaults() {
  memset(&g_wifiCfg, 0, sizeof(g_wifiCfg));
  g_wifiCfg.magic = WIFI_CFG_MAGIC;
  g_wifiCfg.version = 1;
  g_wifiCfg.useDhcp = 1;

  // defaults IP (não usados quando DHCP=1, mas deixamos algo válido)
  g_wifiCfg.ip[0]=192;  g_wifiCfg.ip[1]=168; g_wifiCfg.ip[2]=1;  g_wifiCfg.ip[3]=50;
  g_wifiCfg.gw[0]=192;  g_wifiCfg.gw[1]=168; g_wifiCfg.gw[2]=1;  g_wifiCfg.gw[3]=1;
  g_wifiCfg.mask[0]=255;g_wifiCfg.mask[1]=255;g_wifiCfg.mask[2]=255;g_wifiCfg.mask[3]=0;
  g_wifiCfg.dns[0]=8;   g_wifiCfg.dns[1]=8;   g_wifiCfg.dns[2]=8; g_wifiCfg.dns[3]=8;
}

static void cfgLoad() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, g_wifiCfg);

  if (g_wifiCfg.magic != WIFI_CFG_MAGIC || g_wifiCfg.version != 1) {
    cfgDefaults();
    EEPROM.put(0, g_wifiCfg);
    EEPROM.commit();
  }
}

static void cfgSave() {
  g_wifiCfg.magic = WIFI_CFG_MAGIC;
  g_wifiCfg.version = 1;
  EEPROM.put(0, g_wifiCfg);
  EEPROM.commit();
}

static bool hasCreds() {
  return g_wifiCfg.ssid[0] != '\0';
}

// --------- Página WiFi ----------
static String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length()+10);
  for (size_t i=0;i<s.length();i++){
    char c=s[i];
    if (c=='&') o += "&amp;";
    else if (c=='<') o += "&lt;";
    else if (c=='>') o += "&gt;";
    else if (c=='"') o += "&quot;";
    else o += c;
  }
  return o;
}

static void handleWifiPage(ESP8266WebServer& server) {
  String sta = (WiFi.status() == WL_CONNECTED) ? "conectado" : "desconectado";
  String staIp = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
  String apIp  = WiFi.softAPIP().toString();

  String ssid = htmlEscape(String(g_wifiCfg.ssid));
  String pass = htmlEscape(String(g_wifiCfg.pass));

  String ip   = toIP(g_wifiCfg.ip).toString();
  String gw   = toIP(g_wifiCfg.gw).toString();
  String mask = toIP(g_wifiCfg.mask).toString();
  String dns  = toIP(g_wifiCfg.dns).toString();

  String checked = (g_wifiCfg.useDhcp ? "checked" : "");

  String page;
  page.reserve(3500);
  page = R"rawliteral(
<!doctype html>
<html lang='pt-BR'>
  <head>
    <meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
    <title>WiFi - Scanner Modbus</title>
    <style>
    body{font-family:system-ui,Arial;margin:0;background:#0b1220;color:#e9eefc;padding:16px}
    .card{max-width:720px;margin:0 auto;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.10);border-radius:16px;padding:16px}
    h1{margin:0 0 6px 0;font-size:18px}p{margin:6px 0;color:#a9b6d3;font-size:13px}
    label{display:block;margin-top:10px;margin-bottom:6px;color:#a9b6d3;font-size:12px}
    input{width:100%;padding:10px;border-radius:12px;border:1px solid rgba(255,255,255,.12);background:rgba(0,0,0,.25);color:#e9eefc}
    .row{display:grid;grid-template-columns:1fr 1fr;gap:10px} @media(max-width:560px){.row{grid-template-columns:1fr}}
    .btn{margin-top:14px;background:linear-gradient(180deg,#4ea1ff,#2b74ff);border:0;color:#08101f;padding:10px 14px;border-radius:12px;font-weight:700;cursor:pointer}
    .btn2{margin-left:10px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.12);color:#e9eefc}
    .k{display:inline-block;padding:4px 8px;border-radius:999px;border:1px solid rgba(255,255,255,.12);color:#a9b6d3;font-size:12px}
    </style>
  </head>
  <body>
    <div class='card'>
      <h1>Configurar WiFi</h1>
      <p><span class='k'>AP: " + apIp + "</span> &nbsp; <span class='k'>STA: " + sta + " (" + staIp + ")</span></p>
      <p>Após salvar, o ESP reinicia e tenta conectar na rede informada. O AP <b>continua</b> disponível para acessar o scanner.</p>

      <form method='POST' action='/wifi/save'>
        <label>SSID</label><input name='ssid' maxlength='32' value='" + ssid + "'>
        <label>Senha</label><input name='pass' maxlength='64' value='" + pass + "'>
        <label><input type='checkbox' name='dhcp' value='1' " + checked + "> Usar DHCP</label>

        <div class='row'>
          <div><label>IP fixo</label><input name='ip' value='" + ip + "' placeholder='192.168.1.50'></div>
          <div><label>Gateway</label><input name='gw' value='" + gw + "' placeholder='192.168.1.1'></div>
          <div><label>Máscara</label><input name='mask' value='" + mask + "' placeholder='255.255.255.0'></div>
          <div><label>DNS</label><input name='dns' value='" + dns + "' placeholder='8.8.8.8'></div>
        </div>

        <button class='btn' type='submit'>Salvar e reiniciar</button>
        <button class='btn btn2' type='button' onclick='location.href=\"/\"'>Voltar</button>
      </form>

      <p style='margin-top:14px;'>Dica: Se você quer só usar o scanner offline, não precisa configurar nada aqui.</p>
    </div>
  </body>
</html>
)rawliteral";
  server.send(200, "text/html", page);
}

static void handleWifiSave(ESP8266WebServer& server) {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  ssid.trim();
  // pass não trim (pode ter espaço)

  if (ssid.length() > 32) ssid = ssid.substring(0, 32);
  if (pass.length() > 64) pass = pass.substring(0, 64);

  memset(g_wifiCfg.ssid, 0, sizeof(g_wifiCfg.ssid));
  memset(g_wifiCfg.pass, 0, sizeof(g_wifiCfg.pass));
  ssid.toCharArray(g_wifiCfg.ssid, sizeof(g_wifiCfg.ssid));
  pass.toCharArray(g_wifiCfg.pass, sizeof(g_wifiCfg.pass));

  bool dhcp = server.hasArg("dhcp");
  g_wifiCfg.useDhcp = dhcp ? 1 : 0;

  if (!dhcp) {
    uint8_t ip[4], gw[4], mask[4], dns[4];
    if (!parseIP(server.arg("ip"), ip) ||
        !parseIP(server.arg("gw"), gw) ||
        !parseIP(server.arg("mask"), mask) ||
        !parseIP(server.arg("dns"), dns)) {
      server.send(400, "text/plain", "IP fixo inválido. Verifique ip/gw/mask/dns.");
      return;
    }
    memcpy(g_wifiCfg.ip, ip, 4);
    memcpy(g_wifiCfg.gw, gw, 4);
    memcpy(g_wifiCfg.mask, mask, 4);
    memcpy(g_wifiCfg.dns, dns, 4);
  }

  cfgSave();

  server.send(200, "text/html",
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Salvo</title></head><body style='font-family:system-ui,Arial'>"
    "<h3>Configuração salva.</h3><p>Reiniciando...</p>"
    "<script>setTimeout(()=>{location.href='/'},1500)</script></body></html>"
  );

  delay(400);
  ESP.restart();
}

// --------- API pública pro sketch principal ----------

// Chame 1x no setup() do seu projeto (antes de server.begin())
void wifiManagerBegin(ESP8266WebServer& server,const char* apSsid, const char* apPass, uint32_t staTimeoutMs)  {
  g_apSsid = apSsid;
  g_apPass = apPass;

  cfgLoad();

  // Sempre: AP+STA
  WiFi.mode(WIFI_AP_STA);

  // AP fixo 192.168.4.1
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  WiFi.softAP(g_apSsid, g_apPass);

  // Rotas de WiFi
  server.on("/wifi", HTTP_GET, [&server](){ handleWifiPage(server); });
  server.on("/wifi/save", HTTP_POST, [&server](){ handleWifiSave(server); });

  // Tenta STA se tiver credenciais
  if (!hasCreds()) {
    //Serial.println("[WiFi] Sem credenciais gravadas. Mantendo AP apenas.");
    return;
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  if (g_wifiCfg.useDhcp) {
    // DHCP
    WiFi.config(0U, 0U, 0U);
  } else {
    // IP fixo
    IPAddress ip = toIP(g_wifiCfg.ip);
    IPAddress gw = toIP(g_wifiCfg.gw);
    IPAddress mk = toIP(g_wifiCfg.mask);
    IPAddress dn = toIP(g_wifiCfg.dns);
    WiFi.config(ip, gw, mk, dn);
  }

  //Serial.printf("[WiFi] Conectando em SSID: %s\n", g_wifiCfg.ssid);
  WiFi.begin(g_wifiCfg.ssid, g_wifiCfg.pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < staTimeoutMs) {
    delay(150);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    //Serial.print("[WiFi] STA conectado. IP: ");
    //Serial.println(WiFi.localIP());
  } else {
    //Serial.println("[WiFi] STA não conectou (timeout). Mantendo AP para acesso.");
    WiFi.disconnect();
  }
}
