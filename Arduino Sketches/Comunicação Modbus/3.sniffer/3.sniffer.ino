#include <SoftwareSerial.h>
#include <Arduino.h>

SoftwareSerial Debug(2, 14);

#define RS485_BAUD 9600 // Velocidade do barramento RS485 (modbus comum = 9600 bps)
#define DE_RE 12 // controle do M24M02DR

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
  Debug.println(F("\n=== Sniffer Modbus ==="));
}

void sniff (){
  static uint32_t lastByteTime = 0;
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
  sniff();
  yield();
}