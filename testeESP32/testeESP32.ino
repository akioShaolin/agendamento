void setup() {
  Serial.begin(115200);
  pinMode(32, OUTPUT);  // LED interno do ESP32 (em placas DevKit)
}

void loop() {
  digitalWrite(32, HIGH);
  delay(500);
  digitalWrite(32, LOW);
  delay(500);
}