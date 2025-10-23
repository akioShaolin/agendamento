#include <Arduino.h>
#include <SoftwareSerial.h>

SoftwareSerial Debug(2, 14);

#define DE_RE 12 // controle do M24M02DR
#define RS485_BAUD 9600

// Função para calcular CRC16 (Modbus)
uint16_t crc16_modbus(const uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (uint8_t i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void setup() {
  Serial.begin(RS485_BAUD);
  Debug.begin(9600);
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(2, INPUT);  
  pinMode(DE_RE, OUTPUT);

  digitalWrite(DE_RE, LOW);
  digitalWrite(13, LOW);

  delay(2000);
  Debug.println("\n=== Teste Modbus Manual ===");
}

void sendRequest(uint8_t id, uint16_t regAddr, uint16_t qty){
  // Frame base: [ID][Função][AddrHi][AddrLo][QtyHi][QtyLo]
  uint8_t frame[8];
    frame[0] = id;              // Slave ID
    frame[1] = 0x03;            // Função: Ler Holding Register
    frame[2] = regAddr >> 8;    // Endereço alto
    frame[3] = regAddr & 0xFF;  // Endereço baixo
    frame[4] = qty >> 8;        // Qtd alta
    frame[5] = qty & 0xFF;      // Qtd baixa

    uint16_t crc = crc16_modbus(frame, 6);
    frame[6] = crc & 0xFF;      // CRC baixo
    frame[7] = crc >> 8;        // CRC alto

  // Exibe o pacote
  Debug.print("TX: ");
  for (uint8_t i = 0; i < sizeof(frame); i++) {
    if (frame[i] < 0x10) Debug.print('0');
    Debug.print(frame[i], HEX);
    Debug.print(' ');
  }
  Debug.println();

  // Envia pelo barramento RS485
  digitalWrite(DE_RE, HIGH); // habilita TX
  delayMicroseconds(100);

  Serial.write(frame, sizeof(frame)); // envia dados
  Serial.flush();

  digitalWrite(DE_RE, LOW); // volta para RX

  Debug.println("Pacote enviado!");
}

void receiveResponse (){
  uint8_t buffer[64];
  uint8_t idx = 0;
  static long start = millis();
  
  static bool inFrame = false;  
  delay(10);
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (!inFrame) {
      Debug.print("[RX] ");
      inFrame = true;
    }
    Debug.printf("%02X ", b);
    lastByteTime = millis();
  }

  // Se passou mais de 10 ms sem receber nada, encerra a linha
  if (inFrame && millis() - lastByteTime > 10) {
    Debug.println();
    inFrame = false;
  }

  yield();
}

void loop() {
  send();
  listen();
  Debug.println(".");
  delay(1000);
}