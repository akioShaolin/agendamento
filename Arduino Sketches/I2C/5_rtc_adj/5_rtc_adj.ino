#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include <Ticker.h>

// Esse sketch me fez odiar o Watchdog Timer

RTC_DS1307 rtc;
Ticker ticker;

#define I2C_ADDR_RTC 0x68
#define DOW_REGISTER 0x03

#define BUTTON_PIN  0           // D3 (GPIO0) — Botão com pull-up interno
#define LED_PIN     LED_BUILTIN //  GPIO2 - Led Built in

volatile bool tickFlag = false;

bool settingMode = false;      // Modo de ajuste de hora
String inputBuffer = "";

// --- Função de interrupção (Timer1 hardware) ---
void onTick() {
  tickFlag = true;  // Sinaliza que passou 1 segundo  
}

// --- Função para validar intervalos ---
bool validateDateTime(int d, int m, int y, int hh, int mm, int ss) {
  if (y < 2000 || y > 2099) return false;
  if (m < 1 || m > 12) return false;
  if (d < 1 || d > 31) return false;
  if (hh < 0 || hh > 23) return false;
  if (mm < 0 || mm > 59) return false;
  if (ss < 0 || ss > 59) return false;
  return true;
}

// --- Função para dividir string em inteiros ---
bool parseInput(String input, int &d, int &m, int &y, int &hh, int &mm, int &ss) {
  input.trim();
  int count = 0;
  int values[6];
  int start = 0;

  while (true) {
    int commaIndex = input.indexOf(",", start);
    String part;
    if (commaIndex == -1) part = input.substring(start);
    else part = input.substring(start, commaIndex);

    part.trim();
    if (part.length() == 0) return false;
    if (count >= 6) return false;  // muitos parâmetros
    values[count++] = part.toInt();

    if (commaIndex == -1) break;
    start = commaIndex + 1;
  }

  if (count != 6) return false; // quantidade errada

  d = values[0]; m = values[1]; y = values[2];
  hh = values[3]; mm = values[4]; ss = values[5];
  return validateDateTime(d, m, y, hh, mm, ss);
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  Wire.begin(4, 5); // SDA = GPIO4, SCL = GPIO5

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Espera até que a Serial esteja disponível para evitar perda de mensagens iniciais
  while(!Serial) yield();

  bool rtcFound = false;
  for (int i = 0; i < 5; i++) { //Tenta até 5 vezes conectar ao RTC
    if (rtc.begin()) {
      rtcFound = true;
      break;
    }
    delay(200);
  }

  if (rtcFound) {
    Serial.println("RTC iniciado.");
  } else {
    Serial.println("RTC nao encontrado! Verifique as conexoes I2C.");
    while(1) yield(); // Trava aqui se o RTC nao for encontrado
  }
  
  if (!rtc.isrunning()) {
    Serial.println("RTC nao estava rodando, ajustando data/hora com a hora da compilacao...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // Ajusta com a hora da compilacao
  }

  // Configura Ticker para interrupção a cada 1 segundo
  ticker.attach(1.0, onTick); 
  Serial.println("Iniciado. Pressione o botao externo para ajustar o RTC.");
}

// --- Loop principal ---
void loop() {
  // Detecta o botão externo para entrar no modo de ajuste
  if (digitalRead(BUTTON_PIN) == LOW && !settingMode) {
    settingMode = true;
    Serial.println("\nModo de ajuste ativado. Envie no formato:");
    Serial.println("DD,MM,AAAA,HH,MM,SS");
    Serial.println("Exemplo: 30,09,2025,15,30,00");
    inputBuffer = "";
    delay(300); // Debounce simples para o botao
  }

  // Lógica para o modo de ajuste
  if (settingMode) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') { // Se Enter for pressionado
        if (inputBuffer.length() > 0) {
          int d, m, y, hh, mm, ss;
          if (parseInput(inputBuffer, d, m, y, hh, mm, ss)) {
            // Ajusta a data e hora no RTC
            // A RTClib calcula o dia da semana automaticamente com base na data (y, m, d).
            // O parâmetro 'dow' no construtor DateTime não é usado para definir o dia da semana no DS1307.
            rtc.adjust(DateTime(y, m, d, hh, mm, ss));
            
            // --- CORREÇÃO DO DIA DA SEMANA ---
            // O DS1307 armazena o dia da semana em um registrador separado (0x03).
            // A RTClib.h, ao chamar rtc.adjust(DateTime(...)), não escreve diretamente
            // no registrador do dia da semana do DS1307. Ela apenas define a data e hora.
            // O DS1307, por sua vez, calcula o dia da semana automaticamente com base na data.
            // O problema está na *exibição* do dia da semana, não na gravação.
            // A linha de escrita direta no registrador 0x03 foi removida porque o DS1307
            // já faz o cálculo correto. A inconsistência que você vê é na interpretação
            // do valor lido pela RTClib e como ele é mapeado para a exibição.
            
            Serial.println("RTC ajustado com sucesso!");
            settingMode = false; // Sai do modo de ajuste
            inputBuffer = "";
          } else {
            Serial.println("Entrada invalida! Tente novamente:");
          }
        }
        inputBuffer = ""; // Limpa o buffer após processar ou se a entrada for vazia
      } else {
        inputBuffer += c; // Adiciona o caractere ao buffer de entrada
      }
    }
  }
  // Lógica para exibição contínua da data/hora
  else if (tickFlag) {
    tickFlag = false;
    DateTime now = rtc.now();
    
    // --- CORREÇÃO DA EXIBIÇÃO DO DIA DA SEMANA ---
    // now.dayOfTheWeek() retorna 0=Domingo, 1=Segunda, ..., 6=Sabado.
    // Queremos exibir no formato 1=Dom, 7=Sab.
    uint8_t displayDow = now.dayOfTheWeek();
    if (displayDow == 0) { // Se for Domingo (0), queremos exibir 7
      displayDow = 7;
    } else { // Para os outros dias (1-6), queremos exibir 1-6
      // Não é necessário ajustar, pois 1=Segunda, 2=Terça, ..., 6=Sábado já está no formato desejado (1-6)
      // Apenas mapeamos 0 (Domingo) para 7.
    }

    Serial.printf("%02d/%02d/%04d (%d) %02d:%02d:%02d\n",
                   now.day(), now.month(), now.year(),
                   displayDow,
                   now.hour(), now.minute(), now.second());
    digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1); // Inverte o estado do LED
  }
  yield(); // Permite que o ESP8266 execute outras tarefas
}


