/*******************************************************
   EFEITO DE NATAL PARA 6 LEDs NO ESP32
   Defina aqui os pinos usados em cada LED
********************************************************/

int ledPins[6] = { 25, 26, 27, 32, 33, 14 }; 
//      LED1 LED2 LED3 LED4 LED5 LED6
//  Ajuste conforme necessário ⬆️

int totalLeds = 6;

void setup() {
  Serial.begin(115200);

  // Configura todos os LEDs como saída
  for (int i = 0; i < totalLeds; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  Serial.println("Iniciando efeitos de Natal 🎄✨");
}

void loop() {
  efeitoAlternado();
  efeitoCorrida();
  efeitoPiscaRapido();
  efeitoAleatorio();
}

/*******************************************************
   EFEITO 1 — PISCA ALTERNADO
********************************************************/
void efeitoAlternado() {
  for (int i = 0; i < 10; i++) {
    for (int l = 0; l < totalLeds; l++) {
      digitalWrite(ledPins[l], (l % 2 == 0) ? HIGH : LOW);
    }
    delay(350);

    for (int l = 0; l < totalLeds; l++) {
      digitalWrite(ledPins[l], (l % 2 == 0) ? LOW : HIGH);
    }
    delay(350);
  }
}

/*******************************************************
   EFEITO 2 — CORRIDA LUMINOSA (tipo KITT natalino)
********************************************************/
void efeitoCorrida() {
  for (int i = 0; i < totalLeds; i++) {
    acendeUnico(i);
    delay(120);
  }
  for (int i = totalLeds - 1; i >= 0; i--) {
    acendeUnico(i);
    delay(120);
  }
}

void acendeUnico(int idx) {
  for (int l = 0; l < totalLeds; l++) {
    digitalWrite(ledPins[l], (l == idx) ? HIGH : LOW);
  }
}

/*******************************************************
   EFEITO 3 — PISCA RÁPIDO
********************************************************/
void efeitoPiscaRapido() {
  for (int i = 0; i < 16; i++) {
    setTodos(HIGH);
    delay(70);
    setTodos(LOW);
    delay(70);
  }
}

/*******************************************************
   EFEITO 4 — ALEATÓRIO ESTILO “PISCA-PISCA”
********************************************************/
void efeitoAleatorio() {
  for (int i = 0; i < 50; i++) {
    int r = random(0, totalLeds);
    digitalWrite(ledPins[r], HIGH);
    delay(random(40, 180));
    digitalWrite(ledPins[r], LOW);
  }
}

/*******************************************************/
void setTodos(bool estado) {
  for (int l = 0; l < totalLeds; l++) {
    digitalWrite(ledPins[l], estado);
  }
}
