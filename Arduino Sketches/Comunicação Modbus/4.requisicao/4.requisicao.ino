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
  unsigned long start = millis();
  while (millis() - start < 3000){}
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
  unsigned long txTime = (sizeof(frame) * 1040UL); // cada byte demora 1040 microsegundos para ser enviado em 9600bps
  delayMicroseconds(txTime + 300); // Margem extra para estabilidade. Se o pino mudar antes, a mensagem pode ser distorcida

  digitalWrite(DE_RE, LOW); // volta para RX

  Debug.println("Pacote enviado!");
}

void receiveResponse (){
  uint8_t buffer[64];
  uint8_t idx = 0;
  unsigned long start = millis();
  
  while (millis() - start < 500){ //Timeout de 6s
    if (Serial.available()) {
      uint8_t b = Serial.read();

      // Ignora lixo até encontrar ID válido (1-247)
      if (idx == 0 && (b < 1 || b > 247)) continue;

      if (idx < sizeof(buffer)) buffer[idx++] = b;
    }
  }

  if (idx == 0){
    Debug.println(" Nenhuma resposta recebida");
    return;
  }

  Debug.print("RX: ");
  for (uint8_t i = 0; i < idx; i++){
    if (buffer[i] < 0x10) Debug.print('0');
    Debug.print(buffer[i], HEX);
    Debug.print(' ');
  }
  Debug.println();

  // CRC de resposta pode ser invertido. O algoritmo tenta nas duas ordens

  if (idx >= 5) {
    uint16_t crcCalc = crc16_modbus(buffer, idx - 2);
    uint16_t crcRecv1 = buffer[idx - 2] | (buffer[idx - 1] << 8);
    uint16_t crcRecv2 = buffer[idx - 1] | (buffer[idx - 2] << 8);

    if (crcCalc == crcRecv1 || crcCalc == crcRecv2){
      Debug.println("CRC OK");
      Debug.print("Dados: ");
      for (uint8_t i = 3; i < idx - 2; i++) {
        if (buffer[i] < 0x10) Debug.print('0');
        Debug.print(buffer[i], HEX);
        Debug.print(' ');
      }
      Debug.println();
    }
    else {
      Debug.printf("CRC inválido (calc %04X, recv %04X / %04X)\n", crcCalc, crcRecv1, crcRecv2);
    }
  }
  yield();
}

void loop() {
  uint8_t slaveID1 = 0x02;    //ID configurado no inversor 1
  uint8_t slaveID2 = 0x03;    //ID Configurado no inversor 2
  uint16_t regAddr1 = 0x9C8F;  //Registrador 40079 (potência)
  uint16_t regAddr2 = 0x9D6C;  //Registrador 40301 (Limitação de potência ativa)
  uint16_t regCount1 = 0x0001; //1 Registradores
  uint16_t regCount2 = 0x0002; //2 Registradores

  sendRequest(slaveID1, regAddr1, regCount1);
  receiveResponse();
  delay(10);
  sendRequest(slaveID1, regAddr2, regCount2);
  receiveResponse();
  delay(10);
  sendRequest(slaveID2, regAddr1, regCount1);
  receiveResponse();
  delay(10);
  sendRequest(slaveID2, regAddr2, regCount2);
  receiveResponse();
  delay(10);
  Debug.println("-----------------------");

}