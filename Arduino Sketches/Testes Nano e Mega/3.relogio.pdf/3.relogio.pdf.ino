#include <Ds1302.h>
#include <LiquidCrystal.h>

#define D2 2
#define D3 3
#define D4 4
#define D5 5
#define D6 6
#define D7 7
#define D8 8
#define D9 9
#define D10 10
#define D11 11
#define D12 12
//#define D13 13

// Atribuição dos pinos do Display
uint8_t lcdRS = D8;
uint8_t lcdE = D9;
uint8_t lcdDB4 = D4;
uint8_t lcdDB5 = D5;
uint8_t lcdDB6 = D6;
uint8_t lcdDB7 = D7;

// Atribuição dos pinos do RTC
uint8_t rtcRST = D2;
uint8_t rtcIO = D3;
uint8_t rtcSCLK = D10;

// Atribuição dos pinos dos botões
uint8_t btnMode = A4;
uint8_t btnUp = A5;
uint8_t btnDown = A6;
uint8_t btnSet = A7;

// Atribuição dos pinos de saída
uint8_t O1 = A0;
uint8_t O2 = A1;
uint8_t O3 = A2;
uint8_t O4 = A3;

// pinos LCD (RS, E, DB4, DB5, DB6, DB7)
LiquidCrystal lcd (lcdRS, lcdE, lcdDB4, lcdDB5, lcdDB6, lcdDB7);
// instanciar RTC
Ds1302 rtc (rtcRST, rtcSCLK, rtcIO);

// Variáveis para uso do programa
uint8_t menu_principal = 0;
uint8_t menu_estado = 1;
uint8_t cursor = 0;
uint8_t evento_editar = 0xFF;
// Variáveis de datas
uint8_t ano = 0;
uint8_t mes = 0;
uint8_t dia = 0;
uint8_t sem = 0;
uint8_t hor = 0;
uint8_t min = 0;
uint8_t seg = 0;

//Flag para interrupção
volatile bool flag_updt = false;

ISR(TIMER1_OVF_vect){
  TCNT1 = 0xC2F7; //Reinicia o timer com valor para que o estouro ocorra em 1 segundo
                // 65536 - (16MHz / 1024 / 1Hz) = 49911 = 0xC2F7
  flag_updt = true;
  //digitalWrite(D13, digitalRead(D13) ^ 1);
}

//Buffers para escrever nas linhas do display
char ultima_data[17] = "";
char ultima_hora[17] = "";
char data[17] = "";
char hora[17] = "";

//Menus
void menuPrincipal(){}

// Menu onde se exibe o relógio
void menuRelogio(){

  // Linha 0 --> Data
  sprintf(data, " %02d/%02d/20%02d", dia, mes, ano);

  //Acrescenta o dia da semana
  switch(sem) {
    case 1: strcat(data, " SEG"); break;
    case 2: strcat(data, " TER"); break;
    case 3: strcat(data, " QUA"); break;
    case 4: strcat(data, " QUI"); break;
    case 5: strcat(data, " SEX"); break;
    case 6: strcat(data, " SAB"); break;
    case 7: strcat(data, " DOM"); break;
  }

  if (strcmp(ultima_data, data) != 0) { //Só atualiza se mudar
    lcd.setCursor(0, 0);
    lcd.print("                "); // Limpa a linha (16 espaços)
    lcd.setCursor(0, 0);
    lcd.print(data);
    strcpy(ultima_data, data);
  }

  //Linha 1 --> Hora
  sprintf(hora, " %02d:%02d:%02d ", hor, min, seg);

  if (strcmp(ultima_hora, hora) != 0) { //Só atualiza se mudar
    lcd.setCursor(0, 1);
    lcd.print("                "); // Limpa a linha (16 espaços)
    lcd.setCursor(0, 1);
    lcd.print(hora);
    strcpy(ultima_hora, hora);
  }
}

void menuAgendamentos(){}
void menuCriarAgendamento(){}
void menuAjustarRelogio(){}


void setup() {
//Configuração do TIMER1 para interrupção
  TCCR1A = 0; //Configra timer para operação normal. Pinos OC1A e OC1B desconectados
  TCCR1B = 0; //Limpa o Registrador

  TCCR1B |= (1 << CS10) | (1 << CS12); //Configura prescaler para 1024: CS12 = 1 e CS10 = 1

  TCNT1 = 0xC2F7; //Inicia o timer com valor para que o estouro ocorra em 1 segundo
                  // 65536 - (16MHz / 1024 / 1Hz) = 49911 = 0xC2F7
  TIMSK1 |= (1 << TOIE1); // Habilita a interrupção do TIMER1

//pinModes
// Atribuição dos pinos dos botões
  pinMode(btnMode, INPUT);
  pinMode(btnUp, INPUT);
  pinMode(btnDown, INPUT);
  pinMode(btnSet, INPUT);
//Atribuição das saídas
  pinMode(O1, OUTPUT);
  pinMode(O2, OUTPUT);
  pinMode(O3, OUTPUT);
  pinMode(O4, OUTPUT);
  //pinMode(D13, OUTPUT);
//Escrita de nível baixo nas saídas
  digitalWrite(O1, LOW);
  digitalWrite(O2, LOW);
  digitalWrite(O3, LOW);
  digitalWrite(O4, LOW);

//LCD setup
  lcd.begin(16, 2);
  lcd.clear();

//RTC setup
  rtc.init();

  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");
  delay(800);
}

void loop() {

// Atualização das variaveis de tempo
  if (flag_updt)
  {
    flag_updt = false;
    Ds1302::DateTime now;
    rtc.getDateTime(&now);

    ano = now.year;
    mes = now.month;
    dia = now.day;
    sem = now.dow;
    hor = now.hour;
    min = now.minute;
    seg = now.second;
  }

//Exibição do Menu corrente

  switch (menu_estado) {
    case 0: menuPrincipal(); break;
    case 1: menuRelogio(); break;
    case 2: menuAgendamentos(); break;
    case 3: menuCriarAgendamento(); break;
    case 4: menuAjustarRelogio(); break;
  }
}