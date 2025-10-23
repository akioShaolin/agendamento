#include <Arduino.h>
#include <Wire.h>
#include <FS.h>

#define EEPROM_SIZE 256 * 1024 //256kb total (4 blocos de 64kb)
#define BLOCK_SIZE 65536

//uint8_t eepromAddr[] = {0x50, 0x51, 0x52, 0x53};  // Endereço da EEPROM
uint8_t eepromAddr[] = {0x58, 0x59, 0x5A, 0x5B};  // Endereço da EEPROM

void setup() {
  Serial.begin(115200);
  Wire.begin();

  while (!Serial.available()) yield();

  if (!SPIFFS.begin()){
    Serial.println("Falha ao montar SPIFFS!");
    return;
  }
  //Remova o arquivo no caso de ter errado alguma coisa

  //SPIFFS.remove("/eeprom_dump_0x50_0x53.txt");
  //SPIFFS.remove("/eeprom_dump_0x58_0x5B.txt");

  //File file = SPIFFS.open("/eeprom_dump_0x50_0x53.txt", "w");
  File file = SPIFFS.open("/eeprom_dump.txt", "w");

  if(!file){
    Serial.println("Falha ao abrir o arquivo!");
    return;
  }

  //Percorre os 4 blocos
  for(uint32_t dev = 0; dev < sizeof(eepromAddr); dev++) {
    uint32_t baseAddr = dev * BLOCK_SIZE;
    Serial.printf("Lendo dispositivo 0x%02X...\n", eepromAddr[dev]);

    for (uint32_t addr = 0; addr < BLOCK_SIZE; addr += 16) {
      uint8_t buf[16];

      Wire.beginTransmission(eepromAddr[dev]);
      Wire.write((addr >> 8) & 0xFF); //byte alto   0x12345678 --> 0x1234
      Wire.write(addr & 0xFF);        //byte baixo  0x12345678 --> 0x5678
      Wire.endTransmission();

      Wire.requestFrom((int)eepromAddr[dev], 16);
      for (uint8_t i = 0; i < 16; i++) {
        buf[i] = Wire.available() ? Wire.read() : 0xFF;
      }

      //Grava no arquivo
      file.printf("%05lX: ", baseAddr + addr);
      for (uint8_t i = 0; i < 16; i++){
        file.printf("%02X ", buf[i]);
      }
      file.println();
      Serial.print(".");
      delay(2);
      yield();
    }
    Serial.printf("\ndev %08X finalizado\n", dev);
  }

  file.close();
  //Serial.println("Dump concluido e salvo em /eeprom_dump_0x50_0x53.txt");
  Serial.println("Dump concluido e salvo em /eeprom_dump_0x58_0x5B.txt");
}

void loop() {
  // nada, executa só no setup
}
