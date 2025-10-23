#include <Wire.h>
#include "RTClib.h"
#include <Ticker.h>

RTC_DS1307 rtc;
Ticker timer1;

const int ledPin = LED_BUILTIN; // LED onboard do ESP8266 (GPIO2)
volatile bool flagISR = false;  // flag que a ISR altera
bool ledState = false;

// ISR do timer - apenas seta a flag
void ICACHE_RAM_ATTR isrTimer() {
  flagISR = true;
}

void setup() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // no ESP8266 o LED onboard é ativo em LOW

  Serial.begin(115200);
  Wire.begin(); // SDA=D2(GPIO4), SCL=D1(GPIO5)

  rtc.begin();

  if (!rtc.isrunning()) {
    Serial.println("RTC parado, ajustando data/hora...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Configura timer para chamar ISR a cada 1 segundo
  timer1.attach(1, isrTimer);
}

void loop() {
  if (flagISR) {
    flagISR = false;  // limpa a flag

    // Lê o RTC
    DateTime now = rtc.now();
    Serial.printf("%02d:%02d:%02d\n", now.hour(), now.minute(), now.second());

    // Pisca o LED
    ledState = !ledState;
    digitalWrite(ledPin, ledState ? LOW : HIGH); // invertido no LED onboard
  }
}
