// Declaração de Bibliotecas
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Ticker.h>

// --- Configurações de Hardware e Pinos ---
Ticker ticker;

// A SoftwareSerial Debug é usada para imprimir mensagens de depuração sem interferir na Serial principal
// que será usada para o Modbus RTU.
// No ESP8266, os pinos 2 (RX) e 14 (TX) são GPIOs comuns para SoftwareSerial.
SoftwareSerial Debug(2, 14); 

// Definição dos pinos
#define LED_PIN           LED_BUILTIN   //  GPIO2 - Led Built in
#define DE_RE             12            // Pino ligado ao SN75176B (RS485 transceiver)
#define BTN_PIN           0             // Botão da placa com pull up
#define HALF_OR_FULL_PIN  13            // Half-Duplex:LOW  Full-Duplex-HIGH

// --- Configurações Modbus ---
// IDs do dispositivo escravo Modbus.
#define SLAVE_1_ID 1
#define RS485_BAUD 4800

// --- Configuração dos Registradores pertinentes ---
#define READ_REG_COUNT      2        // Quantidade de registradores a ler. 2 por 2

// Dicionario Medidor CHINT
enum RegType: uint8_t {TYPE_INT, TYPE_FLOAT, TYPE_STRING};
enum RegAccess: uint8_t {READ_ONLY, WRITE_ONLY, READ_WRITE};

// Registradores para leitura de parâmetros do medidor
struct REG {
  uint16_t ADDR;  //Address
  const char* DESCRIPTION;  //
  RegType TYPE;         //int, String
  uint8_t LENGTH; // Length of the word (1 - Default)
  float GAIN;
  RegAccess ACCESS;
};

REG UAB = {0x1FFF, "Three phase line voltage data, Volt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG UBC = {0x2001, "Three phase line voltage data, Volt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG UCA = {0x2003, "Three phase line voltage data, Volt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG UA = {0x2005, "Three phase line voltage data, Volt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG UB = {0x2007, "Three phase line voltage data, Volt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG UC = {0x2009, "Three phase line voltage data, Volt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG IA = {0x200B, "Three phase line current data, Amp", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG IB = {0x200D, "Three phase line current data, Amp", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG IC = {0x200F, "Three phase line current data, Amp", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG PT = {0x2011, "Combined active power, Watt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG PA = {0x2013, "A phase active power, Watt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG PB = {0x2015, "B phase active power, Watt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG PC = {0x2017, "C phase active power, Watt", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};
REG FREQ = {0x2043, "Frequência, Hz", TYPE_FLOAT, READ_REG_COUNT, 0.1, READ_ONLY};

// --- Variáveis de Controle ---
// Flag vinculada ao Timer1 para atualização do RTC
volatile bool tickFlag = false;
// Variável que registra quanto tempo em segundos se passou após ligação
// Guarda valores até 604800 segundos, que é a duração de um ciclo semanal
unsigned long sec_time = 0;
uint16_t response;

// --- Função de interrupção (Timer1 hardware) ---
void onTick() {
  tickFlag = true;  // Sinaliza que passou 1 segundo  
  sec_time++;

  if (sec_time >= 604800) sec_time = 0;
  digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);
}

// -----------------------------------------------
// --------------- ALA MODBUS --------------------
// -----------------------------------------------

// --- Função para Calcular CRC16 (Modbus) ---

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
}

void receiveResponse (){
  uint8_t buffer[64];
  uint8_t idx = 0;
  unsigned long start = millis();
  response = 0x0000;
  
  while (millis() - start < 500){ //Timeout de 1500ms
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

  // Log da resposta Bruta
  Debug.print("RX: ");
  for (uint8_t i = 0; i < idx; i++){
    if (buffer[i] < 0x10) Debug.print('0');
    Debug.print(buffer[i], HEX);
    Debug.print(' ');
  }
  Debug.println("\n");

  // CRC de resposta pode ser invertido. O algoritmo tenta nas duas ordens

  if (idx >= 5) {
    uint16_t crcCalc = crc16_modbus(buffer, idx - 2);
    uint16_t crcRecv1 = buffer[idx - 2] | (buffer[idx - 1] << 8);
    uint16_t crcRecv2 = buffer[idx - 1] | (buffer[idx - 2] << 8);

    if (crcCalc == crcRecv1 || crcCalc == crcRecv2){
      Debug.println("CRC OK");

      uint8_t byteCount = buffer[2];
      Debug.print("Byte count: ");
      Debug.println(byteCount);

      // Interpretação de dados

      if (byteCount == 4) {
        //Converte 4 bytes (float IEEE754) - endianness ABCD (High word first)
        union {
          uint8_t b[4];
          float f;
        } conv;

        conv.b[0] = buffer[3]; //A
        conv.b[1] = buffer[2]; //B
        conv.b[2] = buffer[1]; //C
        conv.b[3] = buffer[0]; //D

        float valor = conv.f;
        response = 0; // Limpa variável anterior
        Debug.print("Valor float: ");
        Debug.println(valor, 3); // Mostra com 3 casas decimais
      } else if (byteCount == 2) {
        // Valor inteiro normal (2 bytes)
        uint16_t valor = (buffer[3] << 8) | buffer[4];
        response = valor;
        Debug.print("Valor inteiro: ");
        Debug.println(valor);
      } else {
        Debug.println("Tamanho de dado inesperado.");
      }
    }
    else {
      Debug.printf("CRC inválido (calc %04X, recv %04X / %04X)\n", crcCalc, crcRecv1, crcRecv2);
    }
  }
  yield();
}

uint16_t checkRegister(uint16_t reg) {
    // Processa a fila de requisições Modbus
  unsigned long start = millis();
  sendRequest(SLAVE_1_ID, reg, READ_REG_COUNT);
  receiveResponse();
  while(millis() - start < 500){yield();}

  return response;
  }

// ================================== SETUP ====================================
void setup() {

  // Inicia a comunicação serial para depuração (SoftwareSerial) e para Modbus (HardwareSerial).
  Serial.begin(RS485_BAUD); 
  Debug.begin(9600); // Taxa de baud para o monitor serial de depuração.

  // Configura o pino DE_RE como saída para controlar a direção do transceptor RS485. O pino HALF_OR_FULL_PIN se refere a Half ou Full Duplex, devido a um HC4066 selecionando entre 2 transceivers de RS485.
  pinMode(DE_RE, OUTPUT);
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP); 

  digitalWrite(DE_RE, LOW); // Inicializa em modo de recepção (RX).
  digitalWrite(HALF_OR_FULL_PIN, LOW);  //Define a Rede RS485 como Half Duplex
  digitalWrite(BTN_PIN, LOW); // Inicia o led como desligado

  //   === ATENÇÃO! COMENTAR LINHA ABAIXO AO APLICAR ===
  // === ESSA LINHA SERVE APENAS PARA AGUARDAR O DEBUG ===
  while(digitalRead(BTN_PIN)) yield();
  //Aguarda apertar o botão para iniciar Debug. Se comentado, inicia direto
  // =====================================================

  delay(2000); // Pequena pausa para estabilização.
  Debug.println("\n=== Mestre Modbus RTU Manual ===");

  //      0 Para Dom: 00:00:00
  // 604799 para Sab: 23:59:59
  sec_time = 0;

  // --- Configuração do Timer1 hardware (ESP8266) ---
  ticker.attach(20.0, onTick); //Interrupção a cada 10 segundos
}

void loop() {
  
  if (tickFlag) {

    tickFlag = false;
    
    float uab, ubc, uca, ua, ub, uc, ia, ib, ic, pt, pa, pb, pc, freq;

    uab = (float)checkRegister(UAB.ADDR) * 0.1f;
    ubc = (float)checkRegister(UBC.ADDR) * 0.1f;
    uca = (float)checkRegister(UCA.ADDR) * 0.1f;
    ua = (float)checkRegister(UA.ADDR) * 0.1f;
    ub = (float)checkRegister(UB.ADDR) * 0.1f;
    uc = (float)checkRegister(UC.ADDR) * 0.1f;
    ia = (float)checkRegister(IA.ADDR) * 0.001f;
    ib = (float)checkRegister(IB.ADDR) * 0.001f;
    ic = (float)checkRegister(IC.ADDR) * 0.001f;
    pt = (float)checkRegister(PT.ADDR) * 0.1f;
    pa = (float)checkRegister(PA.ADDR) * 0.1f;
    pb = (float)checkRegister(PB.ADDR) * 0.1f;
    pc = (float)checkRegister(PC.ADDR) * 0.1f;
    freq = (float)checkRegister(FREQ.ADDR) * 0.01f;
    
    Debug.printf("Tensão AB: %.1f V\nTensão BC: %.1f V\nTensão CA: %.1f V\nTensão A: %.1f V\nTensão B: %.1f V\nTensão C: %.1f V\nCorrente A: %.1f A\nCorrente B: %.1f A\nCorrente C: %.1f A\nPotência Total: %.1f W\nPotência A: %.1f W\nPotência B: %.1f W\nPotência C: %.1f W\nFrequência: %.1f Hz\n\n", uab, ubc, uca, ua, ub, uc, ia, ib, ic, pt, pa, pb, pc, freq);
  }
}

