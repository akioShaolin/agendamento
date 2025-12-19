#include <ESP8266WiFi.h>
#include <ModbusIP_ESP8266.h>
#include <ModbusRTU.h>

// --- Configurações de Rede ---
const char* ssid = "VISITANTES"; // Substitua pelo nome da sua rede Wi-Fi
const char* password = "connection"; // Substitua pela senha da sua rede Wi-Fi

// --- Configurações de IP Fixo ---
IPAddress ip(192, 168, 1, 100); // IP Fixo para o ESP07
IPAddress gateway(192, 168, 1, 1); // Gateway da sua rede
IPAddress subnet(255, 255, 255, 0); // Máscara de sub-rede

// --- Configurações Modbus ---
ModbusIP mb; // Servidor Modbus TCP/IP
ModbusRTU rtu; // Cliente Modbus RTU

// --- Pinagem ---
#define DE_RE_PIN 12 // Pino de controle DE/RE para o RS485
#define HALF_OR_FULL_PIN 13 // Half-Duplex:LOW Full-Duplex:HIGH

// Função de callback para a ponte Modbus
// Esta função é chamada quando o ESP07 recebe uma requisição Modbus TCP
// e precisa encaminhá-la para o barramento Modbus RTU.
void modbus_bridge_callback(uint8_t* data, uint8_t len) {
  // 1. Ativa o modo de transmissão do RS485
  digitalWrite(DE_RE_PIN, HIGH);
  
  // 2. Envia a trama Modbus RTU para o inversor
  rtu.send(data, len);
  
  // 3. Aguarda o fim da transmissão
  rtu.flush();
  
  // 4. Ativa o modo de recepção do RS485
  digitalWrite(DE_RE_PIN, LOW);
}

void setup() {
  // Inicia a Serial para debug
  Serial.begin(115200);
  
  // Inicia a Serial para o Modbus RTU (conectada ao inversor)
  rtu.begin(&Serial, 9600); // Assumindo 9600 bps para o inversor
  
  // Configura o pino DE/RE
  pinMode(DE_RE_PIN, OUTPUT);
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, LOW); // Modo de recepção por padrão
  digitalWrite(HALF_OR_FULL_PIN, LOW); //Define o RS485 como Half-Duplex
  
  // Conecta ao Wi-Fi
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);
  
  Serial.print("Conectando ao Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConectado!");
  Serial.print("IP do ESP07: ");
  Serial.println(WiFi.localIP());
  
  // Inicia o servidor Modbus TCP/IP
  mb.server();
  
  // Configura a função de callback para a ponte
  mb.onRaw(modbus_bridge_callback);
}

void loop() {
  // Processa as requisições Modbus TCP/IP
  mb.task();
  
  // Processa as respostas Modbus RTU do inversor
  if (rtu.available()) {
    uint8_t* data = rtu.receive();
    if (data) {
      // Encaminha a resposta do inversor para o cliente Modbus TCP (PC)
      mb.raw(data, rtu.size());
      free(data);
    }
  }
  
  yield();
}
