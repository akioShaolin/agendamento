// Arquivo: esp07_modbus_gateway.ino
// Autor: Manus AI
// Descrição: Sketch para ESP07 (ESP8266) atuando como um Modbus RTU Gateway/Passthrough
//            Controla o pino DE/RE do transceptor RS485 para permitir a comunicação
//            bidirecional entre o PC (Master) e o Inversor (Slave).

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Ticker.h>

// --- Configurações de Hardware e Pinos ---
Ticker ticker;

// --- DEFINIÇÕES DE PINAGEM E COMUNICAÇÃO ---
#define RS485_BAUD 9600     // Velocidade do barramento RS485 (Modbus)
#define DE_RE_PIN 12        // Pino GPIO12 para controle DE/RE do transceptor 
// RS485 (Confirmado pelo diagrama)

// Pinos para SoftwareSerial (RS485) - Confirmado pelo diagrama: GPIO13 (TX), GPIO15 (RX)
#define RS485_RX_PIN 2
#define RS485_TX_PIN 0

#define HALF_OR_FULL_PIN 13

// Cria o objeto SoftwareSerial para a comunicação RS485
SoftwareSerial RS485Serial(RS485_RX_PIN, RS485_TX_PIN); 

// --- VARIÁVEIS DE ESTADO ---
// Estado do pino DE/RE: LOW = Recepção (RX), HIGH = Transmissão (TX)
volatile bool isTransmitting = false; 

// --- FUNÇÕES DE CONTROLE ---

// Função para configurar o pino DE/RE para o modo de Recepção (RX)
void setReceiveMode() {
  if (isTransmitting) {
    // Garante que a transmissão terminou antes de mudar o modo
    Serial.flush(); 
    digitalWrite(DE_RE_PIN, LOW);
    isTransmitting = false;
  }
}

// Função para configurar o pino DE/RE para o modo de Transmissão (TX)
void setTransmitMode() {
  if (!isTransmitting) {
    digitalWrite(DE_RE_PIN, HIGH);
    isTransmitting = true;
  }
}

// --- SETUP ---
void setup() {
  // 1. Configuração da Serial para o PC (Serial0)
  // Usada para receber comandos do Python e enviar respostas de volta.
  Serial.begin(RS485_BAUD); 
  
  // 2. Configuração da SoftwareSerial para o RS485
  RS485Serial.begin(RS485_BAUD); 
  
  // 3. Configuração do Pino DE/RE
  pinMode(DE_RE_PIN, OUTPUT);
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  setReceiveMode(); // Inicia em modo de Recepção

  digitalWrite(HALF_OR_FULL_PIN, HIGH);
  RS485Serial.println(F("\n=== ESP07 Modbus Gateway Iniciado (SoftwareSerial) ==="));
  RS485Serial.print(F("RS485 Baud Rate: "));
  RS485Serial.println(RS485_BAUD);
  RS485Serial.print(F("DE/RE Pin: "));
  RS485Serial.println(DE_RE_PIN);
}

// --- LOOP PRINCIPAL ---
void loop() {
  // 1. Comunicação PC (Master) -> RS485 (Slave)
  // O PC envia a requisição. O ESP07 deve retransmitir e controlar o DE/RE.
  if (RS485Serial.available()) {
    setTransmitMode(); // Mudar para modo de Transmissão
    
    // Passa todos os bytes do PC para o RS485
    while (RS485Serial.available()) {
      int byte_read = RS485Serial.read();
      Serial.write(byte_read);
      // Opcional: Echo para o PC para debug
      // Serial.write(byte_read); 
    }
    
    // Após enviar a requisição, o ESP07 deve esperar o tempo de silêncio Modbus
    // O tempo de 3.5 caracteres a 9600 bps é ~3.64ms. Usaremos 4ms.
    delay(4); 
    
    setReceiveMode(); // Mudar para modo de Recepção para esperar a resposta
  }

  // 2. Comunicação RS485 (Slave) -> PC (Master)
  // O Inversor envia a resposta. O ESP07 deve retransmitir para o PC.
  if (Serial.available()) {
    // Passa todos os bytes do RS485 para o PC
    while (Serial.available()) {
      RS485Serial.write(Serial.read());
    }
  }
  
  yield(); // Importante para o kernel do ESP8266
}
