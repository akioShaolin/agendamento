#include <SoftwareSerial.h>
#include <Arduino.h>

// Configuração de Hardware
#define RS485_BAUD 9600 // Velocidade do barramento RS485
#define DEBUG_BAUD 115200 // Aumentar a velocidade do Debug para reduzir gargalo
#define DE_RE 12 // Controle do transceptor RS485 (DE/RE)

// O ESP07 usa a Hardware Serial (UART0) nos pinos padrão (TX/RX)
// A SoftwareSerial será usada para o Debug (o que você chamou de Debug(2, 14))
// No ESP8266, o pino 2 é o RX0 e o pino 14 é o TX2 (não padrão para SoftwareSerial).
// Vou assumir que você está usando a Serial padrão (UART0) para o RS485.
SoftwareSerial DebugSerial(2, 14); // Pinos D2 (RX) e D14 (TX) para Debug

// Tempo de silêncio Modbus RTU (3.5 caracteres @ 9600 bps ≈ 3.64 ms). Usamos 4ms para segurança.
#define MODBUS_TIMEOUT_MS 4 

void setup() {
  // Inicializa a Serial principal (UART0) para o RS485
  Serial.begin(RS485_BAUD);
  // Inicializa a Serial de Debug com velocidade alta
  DebugSerial.begin(DEBUG_BAUD);
  
  // Configuração dos pinos
  pinMode(DE_RE, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT); // Pino 2 é o LED_BUILTIN em alguns ESPs, mas você o usou para RX do Debug. Vou usar o LED_BUILTIN para o pino 13.
  
  // Configura o transceptor para modo de recepção (LOW)
  digitalWrite(DE_RE, LOW);
  digitalWrite(LED_BUILTIN, LOW); // LED desligado

  delay(2000);
  DebugSerial.println(F("\n=== Sniffer Modbus Otimizado ==="));
  DebugSerial.print(F("RS485 @ "));
  DebugSerial.print(RS485_BAUD);
  DebugSerial.println(F(" bps"));
}

void sniff() {
  static uint32_t lastByteTime = 0;
  static bool inFrame = false;
  float time = 0;
  
  // 1. Processamento de Bytes (Não-Bloqueante)
  while (Serial.available()) {
    uint8_t b = Serial.read();
    
    // Se a trama anterior terminou, ou se estamos começando uma nova
    if (!inFrame) {
      time = (float)millis() / 1000;
      DebugSerial.printf("%5.3f [RX] ", time);
      inFrame = true;
    }
    
    DebugSerial.printf("%02X ", b);
    lastByteTime = millis();
    
    // O yield() é importante para o ESP8266, mas deve ser usado com moderação
    // para não introduzir atrasos no loop de leitura.
    // yield(); 
  }

  // 2. Detecção de Fim de Trama (Timeout)
  // Verifica se a trama estava ativa E se o tempo de silêncio Modbus (4ms) foi excedido
  if (inFrame && (millis() - lastByteTime > MODBUS_TIMEOUT_MS)) {
    DebugSerial.println();
    inFrame = false;
  }
}

void loop() {
  sniff();
  // O loop roda o mais rápido possível, maximizando a chance de ler o buffer serial.
}
