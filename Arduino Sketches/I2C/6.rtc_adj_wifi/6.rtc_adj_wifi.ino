#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include "RTClib.h"

// ================= OBJETOS =================
RTC_DS1307 rtc;
ESP8266WebServer server(80);

// ================= WIFI AP =================
const char* ssid = "ESP07_RTC";
const char* password = "12345678";

// ================= VARIÁVEL =================
int valorExemplo = 0;

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

  <p><b>Variável:</b></p>
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
  json += "\"valor\":" + String(valorExemplo);
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

// ================= SETUP =================
void setup() {

  // I2C
  Wire.begin(4, 5); // SDA / SCL

  // RTC
  rtc.begin();

  // WIFI AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // SERVER
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/set", handleSetRTC);
  server.begin();
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  static unsigned long t = 0;
  if (millis() - t >= 1000) {
    t = millis();
    valorExemplo++;
  }
}
