#include <Wire.h>
//Scanner i2c
void setup() {
  Wire.begin(); // SDA, SCL (ajuste conforme seu ESP)
  Serial.begin(115200);
  delay(1000);
  Serial.println("Iniciando scanner I2C...");

  while(!Serial.available()) yield();

  byte count = 0;
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.print("Dispositivo I2C encontrado no endereço 0x");
      Serial.println(i, HEX);
      count++;
      delay(10);
    }
  }
  if(count == 0) Serial.println("Nenhum dispositivo I2C encontrado!");
  else Serial.print("Total de dispositivos encontrados: "), Serial.println(count);
}

void loop() {}
