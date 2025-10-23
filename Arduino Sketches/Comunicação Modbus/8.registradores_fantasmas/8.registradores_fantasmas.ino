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
#define SLAVE_1_ID 2
#define RS485_BAUD 9600

// --- Configuração dos Registradores pertinentes ---
#define READ_REG_COUNT      1        // Quantidade de registradores a ler. 2 por 2

// Dicionario Medidor CHINT
struct Register {
  uint16_t ADDR;
  uint16_t LENGTH;
};

Register registers[17] PROGMEM = {
  {40256, 2},
  {40258, 1},
  {40259, 1},
  {40260, 1},
  {40261, 2},
  {40263, 1},
  {40264, 1},
  {40265, 1},
  {40266, 1},
  {40267, 2},
  {40269, 1},
  {40270, 1},
  {40271, 1},
  {40272, 2},
  {40274, 2},
  {40276, 1},
  {40277, 1},
};

/*Registradores não listados:

Registrador 40256: 0000
Registrador 40258: FFFF
Registrador 40259: FFFF
Registrador 40260: FFFF
Registrador 40261: FFFF
Registrador 40263: 0000
Registrador 40264: 0000
Registrador 40265: FFFF
Registrador 40266: FFFF
Registrador 40267: 0000
Registrador 40269: FFFF
Registrador 40270: FFFF
Registrador 40271: FFFF
Registrador 40272: FFFF
Registrador 40274: FFFF
Registrador 40276: FFFF
Registrador 40277: 02BF

*/

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
  Debug.println();
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

  Debug.print("RX: ");
  for (uint8_t i = 0; i < idx; i++){
    if (buffer[i] < 0x10) Debug.print('0');
    Debug.print(buffer[i], HEX);
    Debug.print(' ');
  }
  Debug.println();

  // CRC de resposta pode ser invertido. O algoritmo tenta nas duas ordens

  if (idx >= 5) {
    uint16_t crcCalc = crc16_modbus(buffer, idx - 2);
    uint16_t crcRecv1 = buffer[idx - 2] | (buffer[idx - 1] << 8);
    uint16_t crcRecv2 = buffer[idx - 1] | (buffer[idx - 2] << 8);

    if (crcCalc == crcRecv1 || crcCalc == crcRecv2){
      Debug.println("CRC OK");
      Debug.print("Dados: ");
      for (uint8_t i = 3; i < idx - 2; i++) {
        if (buffer[i] < 0x10) Debug.print('0');
        Debug.print(buffer[i], HEX);
        Debug.print(' ');
        response = (response << 8) | buffer[i];
      }
      Debug.println();
    }
    else {
      Debug.printf("CRC inválido (calc %04X, recv %04X / %04X)\n", crcCalc, crcRecv1, crcRecv2);
    }
  }
  yield();
}

uint16_t checkRegister(uint16_t reg, uint16_t length) {
  Debug.println("185");
    // Processa a fila de requisições Modbus
  unsigned long start = millis();
  sendRequest(SLAVE_1_ID, reg, length);
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
  ticker.attach(10.0, onTick); //Interrupção a cada 10 segundos
}

void loop() {
  
  uint16_t regValue[sizeof(registers)/sizeof(registers[0])];
  Register r;
  if (tickFlag) {

    tickFlag = false;
    Debug.println("235");
    for (int i = 0; i <= sizeof(registers)/sizeof(registers[0]); i++) {
      memcpy_P(&r, &registers[i], sizeof(Register));
      regValue[i] = checkRegister(r.ADDR, r.LENGTH);
      yield();
    }
    
    for (int i = 0; i <= sizeof(registers)/sizeof(registers[0]); i++){
      memcpy_P(&r, &registers[i], sizeof(Register));
      Debug.printf("Registrador %d: %04X\n", r.ADDR, regValue[i]);
      yield();
    }
  }
  yield();
}

