#include <Wire.h>

#define EEPROM_ADDR 0x5B  // Endereço base M24M02DR Vá trocando da sessão 0x50 até a 0x58,. Cada uma tem 64kB

void setup() {
  pinMode(0, INPUT);
  Serial.begin(9600);
  Wire.begin();  // SDA=D2 (GPIO4), SCL=D1 (GPIO5) no ESP8266

  while (digitalRead(0)) yield();
  
  delay(1000);
  Serial.println("Dump EEPROM M24M02DR:");
  Serial.println("----------------------");

  // Vamos percorrer só os primeiros 256 bytes para exemploc:\Users\pedro.sakuma\OneDrive - EcoPower Energia Solar\Área de Trabalho\Agendamento\Arduino Sketches\Comunicação Modbus\7.dtsu666\7.dtsu666.ino
  // (se quiser o chip todo, mude o limite para 262144 = 0x40000)
  for (uint32_t addr = 0; addr < 65536; addr += 16) {
    // imprime endereço base da linha
    Serial.printf("%05X: ", addr);

    // ler 16 bytes por linha
    for (int i = 0; i < 16; i++) {
      uint8_t data = readEEPROM(addr + i);
      Serial.printf("%02X ", data);
    }
    Serial.println();
  }

  Serial.println("Fim do dump.");
}

void loop() {
  // nada, executa só no setup
}

// Função para ler 1 byte da EEPROM
uint8_t readEEPROM(uint32_t addr) {
  uint8_t rdata = 0xFF;

  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write((addr >> 8) & 0xFF);  // MSB do endereço
  Wire.write(addr & 0xFF);         // LSB do endereço
  Wire.endTransmission();

  Wire.requestFrom(EEPROM_ADDR, 1);
  if (Wire.available()) rdata = Wire.read();

  return rdata;
}
