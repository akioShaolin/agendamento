// Feito sob medida para a cliente Vanessa Hespanha
// Concessionária: Neoenergia

// Declaração de Bibliotecas
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include "RTClib.h"
#include <Ticker.h>

// --- Configurações de Hardware e Pinos ---
// Inicialização do RTC, timer por hardware
RTC_DS1307 rtc;
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

// --- Configurações I2C ---
// ID dos dispositivos e configurações

#define I2C_ADDR_RTC 0x68
#define I2C_ADDR_MEM_1 0x50
#define I2C_ADDR_MEM_2 0x51
#define I2C_ADDR_MEM_3 0x52
#define I2C_ADDR_MEM_4 0x53
#define I2C_MAX_RETRIES 3
#define I2C_TIMEOUT_MS 10

// --- Configurações Modbus ---
// IDs do dispositivo escravo Modbus.
#define SLAVE_1_ID 2
#define RS485_BAUD 9600

// --- Configuração dos Registradores pertinentes ---

#define READ_REG_COUNT      1        // Quantidade de registradores a ler. 1 por 1 por enquanto
#define HR_ACT_PWR_OUT      0x9C8F   //Holding Register Active Power Output. Scale: 100 [W]
#define HR_EN_ACT_PWR_LIM   0x9D6B   //Holding Register Enable Active Power Limit. [0 - Off, 1 - On]
#define HR_ACT_PWR_LIM_VL   0x9D6C   //Holding Register Active Power Limit Value. Scale: 0.1 [%]  (0~1100)

// --- Variáveis de Controle ---
// Flag para garantir que a escrita única seja feita apenas uma vez no setup.
bool initialWriteDone = false;
// Flag vinculada ao Timer1 para atualização do RTC
volatile bool tickFlag = false;
// Variável que registra quanto tempo em segundos se passou após ligação
// Guarda valores até 604800 segundos, que é a duração de um ciclo semanal
unsigned long sec_time = 0;

// --- Programação dos eventos ---
// ID do programa
// Dia da Semana
// Hora
// Minuto
// Potência
// Ativado

#define POT_INV 75000
#define NUM_PROGRAMS 106

struct Program {
  uint8_t id;                   // ID do programa (1 a 4)
  uint8_t dayOfWeek;            // Dia da semana (0 - Domingo, 1 - Segunda, 10 - Dias úteis, 11 - Fim de Semana)
  uint8_t month;                // Mês (1 - Janeiro, 12 - Dezembro, 0 - Qualquer)
  uint8_t startHour;                 // Hora do inicio evento
  uint8_t startMinute;               // Minuto do inicio evento
  uint8_t endHour;                 // Hora do inicio evento
  uint8_t endMinute;               // Minuto do inicio evento
  uint32_t power;               // Potência (em W)
  bool enabled;                 // 1 - ativo, 0 - inativo
};

Program programs[NUM_PROGRAMS] PROGMEM = {
  // Janeiro
  {0x01,  10,  1,  0,  0, 10,  0,  75000, true},
  {0x02,  10,  1, 10,  0, 15,  0,      0, true},
  {0x03,  10,  1, 15,  0, 23, 59,  75000, true},
  {0x04,   6,  1,  0,  0, 11,  0,  75000, true},
  {0x05,   6,  1, 11,  0, 15,  0,      0, true},
  {0x06,   6,  1, 15,  0, 23, 59,  75000, true},
  {0x07,   0,  1,  0,  0, 10,  0,  75000, true},
  {0x08,   0,  1, 10,  0, 15,  0,      0, true},
  {0x09,   0,  1, 15,  0, 23, 59,  75000, true},
  // Fevereiro
  {0x0A,  10,  2,  0,  0, 23, 59,  75000, true},
  {0x0B,   6,  2,  0,  0, 11,  0,  75000, true},
  {0x0C,   6,  2, 11,  0, 16,  0,      0, true},
  {0x0D,   6,  2, 16,  0, 23, 59,  75000, true},
  {0x0E,   0,  2,  0,  0, 10,  0,  75000, true},
  {0x0F,   0,  2, 10,  0, 16,  0,      0, true},
  {0x10,   0,  2, 16,  0, 23, 59,  75000, true},
  // Março
  {0x11,  10,  3,  0,  0, 11,  0,  75000, true},
  {0x12,  10,  3, 11,  0, 14,  0,      0, true},
  {0x13,  10,  3, 14,  0, 23, 59,  75000, true},
  {0x14,   6,  3,  0,  0, 11,  0,  75000, true},
  {0x15,   6,  3, 11,  0, 15,  0,      0, true},
  {0x16,   6,  3, 15,  0, 23, 59,  75000, true},
  {0x17,   0,  3,  0,  0, 10,  0,  75000, true},
  {0x18,   0,  3, 10,  0, 14,  0,      0, true},
  {0x19,   0,  3, 14,  0, 15,  0,  34000, true},
  {0x1A,   0,  3, 15,  0, 23, 59,  75000, true},
  // Abril
  {0x1B,  10,  4,  0,  0, 10,  0,  75000, true},
  {0x1C,  10,  4, 10,  0, 14,  0,      0, true},
  {0x1D,  10,  4, 14,  0, 23, 59,  75000, true},
  {0x1E,   6,  4,  0,  0, 11,  0,  75000, true},
  {0x1F,   6,  4, 11,  0, 15,  0,      0, true},
  {0x20,   6,  4, 15,  0, 23, 59,  75000, true},
  {0x21,   0,  4,  0,  0, 11,  0,  75000, true},
  {0x22,   0,  4, 11,  0, 15,  0,      0, true},
  {0x23,   0,  4, 15,  0, 23, 59,  75000, true},
  // Maio
  {0x24,  10,  5,  0,  0, 10,  0,  75000, true},
  {0x25,  10,  5, 10,  0, 15,  0,      0, true},
  {0x26,  10,  5, 15,  0, 16,  0,  58000, true},
  {0x27,  10,  5, 16,  0, 23, 59,  75000, true},
  {0x28,   6,  5,  0,  0, 10,  0,  75000, true},
  {0x29,   6,  5, 10,  0, 16,  0,      0, true},
  {0x2A,   6,  5, 16,  0, 23, 59,  75000, true},
  {0x2B,   0,  5,  0,  0, 10,  0,  75000, true},
  {0x2C,   0,  5, 10,  0, 16,  0,      0, true},
  {0x2D,   0,  5, 16,  0, 23, 59,  75000, true},
  // Junho
  {0x2E,  10,  6,  0,  0, 11,  0,  75000, true},
  {0x2F,  10,  6, 11,  0, 16,  0,      0, true},
  {0x30,  10,  6, 16,  0, 23, 59,  75000, true},
  {0x31,   6,  6,  0,  0, 11,  0,  75000, true},
  {0x32,   6,  6, 11,  0, 16,  0,      0, true},
  {0x33,   6,  6, 16,  0, 23, 59,  75000, true},
  {0x34,   0,  6,  0,  0, 11,  0,  75000, true},
  {0x35,   0,  6, 11,  0, 16,  0,      0, true},
  {0x36,   0,  6, 16,  0, 23, 59,  75000, true},
  // Julho
  {0x37,  10,  7,  0,  0, 11,  0,  75000, true},
  {0x38,  10,  7, 11,  0, 16,  0,      0, true},
  {0x39,  10,  7, 16,  0, 23, 59,  75000, true},
  {0x3A,   6,  7,  0,  0, 11,  0,  75000, true},
  {0x3B,   6,  7, 11,  0, 16,  0,      0, true},
  {0x3C,   6,  7, 16,  0, 23, 59,  75000, true},
  {0x3D,   0,  7,  0,  0, 10,  0,  75000, true},
  {0x3E,   0,  7, 10,  0, 16,  0,      0, true},
  {0x3F,   0,  7, 16,  0, 23, 59,  75000, true},
  // Agosto
  {0x40,  10,  8,  0,  0, 11,  0,  75000, true},
  {0x41,  10,  8, 11,  0, 15,  0,      0, true},
  {0x42,  10,  8, 15,  0, 23, 59,  75000, true},
  {0x43,   6,  8,  0,  0, 11,  0,  75000, true},
  {0x44,   6,  8, 11,  0, 16,  0,      0, true},
  {0x45,   6,  8, 16,  0, 23, 59,  75000, true},
  {0x46,   0,  8,  0,  0, 10,  0,  75000, true},
  {0x47,   0,  8, 10,  0, 16,  0,      0, true},
  {0x48,   0,  8, 16,  0, 23, 59,  75000, true},
  // Setembro
  {0x49,  10,  9,  0,  0, 13,  0,  75000, true},
  {0x4A,  10,  9, 13,  0, 14,  0,      0, true},
  {0x4B,  10,  9, 14,  0, 23, 59,  75000, true},
  {0x4C,   6,  9,  0,  0, 23, 59,  75000, true},
  {0x4D,   0,  9,  0,  0, 11,  0,  75000, true},
  {0x4E,   0,  9, 11,  0, 15,  0,      0, true},
  {0x4F,   0,  9, 15,  0, 23, 59,  75000, true},
  // Outubro
  {0x50,  10, 10,  0,  0, 23, 59,  75000, true},
  {0x51,   6, 10,  0,  0, 12,  0,  75000, true},
  {0x52,   6, 10, 12,  0, 14,  0,      0, true},
  {0x53,   6, 10, 14,  0, 23, 59,  75000, true},
  {0x54,   0, 10,  0,  0, 11,  0,  75000, true},
  {0x55,   0, 10, 11,  0, 14,  0,      0, true},
  {0x56,   0, 10, 14,  0, 23, 59,  75000, true},
  // Novembro
  {0x57,  10, 11,  0,  0, 23, 59,  75000, true},
  {0x58,   6, 11,  0,  0, 13,  0,  75000, true},
  {0x59,   6, 11, 13,  0, 14,  0,      0, true},
  {0x5A,   6, 11, 14,  0, 23, 59,  75000, true},
  {0x5B,   0, 11,  0,  0,  9,  0,  75000, true},
  {0x5C,   0, 11,  9,  0, 16,  0,      0, true},
  {0x5D,   0, 11, 16,  0, 23, 59,  75000, true},
  // Dezembro
  {0x5E,  10, 12,  0,  0, 10,  0,  75000, true},
  {0x5F,  10, 12, 10,  0, 14,  0,      0, true},
  {0x60,  10, 12, 14,  0, 15,  0,  75000, true},
  {0x61,  10, 12, 15,  0, 16,  0,      0, true},
  {0x62,  10, 12, 16,  0, 23, 59,  75000, true},
  {0x63,   6, 12,  0,  0, 11,  0,  75000, true},
  {0x64,   6, 12, 11,  0, 14,  0,      0, true},
  {0x65,   6, 12, 14,  0, 23, 59,  75000, true},
  {0x66,   0, 12,  0,  0, 10,  0,  75000, true},
  {0x67,   0, 12, 10,  0, 13,  0,      0, true},
  {0x68,   0, 12, 13,  0, 14,  0,  75000, true},
  {0x69,   0, 12, 14,  0, 15,  0,      0, true},
  {0x6A,   0, 12, 15,  0, 23, 59,  75000, true},
};

const Program defaultProgram PROGMEM  = {0x00,  0,  0,  0,  0,  0,  0,  0, true};
// Caso não for definido um programa, a potência de exportação será zerada

/****** DIA DA SEMANA ********
    0 - Domingo
    1 - Segunda-feira
    2 - Terça-feira
    3 - Quarta-feira
    4 - Quinta-feira
    5 - Sexta-feira
    6 - Sábado
*****************************/
// --- Função de interrupção (Timer1 hardware) ---
void onTick() {
  tickFlag = true;  // Sinaliza que passou 1 segundo  
  sec_time++;

  if (sec_time >= 604800) sec_time = 0;
}

// -----------------------------------------------
// --------------- ALA MODBUS --------------------
// -----------------------------------------------

// --- Códigos de Status de Resposta ---
enum ModbusResponseStatus {
  RESPONSE_SUCCESS,
  RESPONSE_NO_RESPONSE,
  RESPONSE_CRC_ERROR,
  RESPONSE_EXCEPTION,
  RESPONSE_UNEXPECTED_ID,
  RESPONSE_UNEXPECTED_FUNCTION,
  RESPONSE_TOO_SHORT,
  RESPONSE_UNEXPECTED_BYTE_COUNT // Novo status para contagem de bytes inesperada
};

// --- Configurações de Timeout e Retry ---
#define MODBUS_TIMEOUT_MS 500      // Timeout para receber a resposta Modbus (em ms)
#define MAX_WRITE_RETRIES 5        // Número máximo de tentativas para uma escrita
#define WRITE_RETRY_DELAY_MS 200   // Atraso entre as tentativas de escrita (em ms)
#define MAX_READ_RETRIES 5         // Número máximo de tentativas para uma leitura
#define READ_RETRY_DELAY_MS 100    // Atraso entre as tentativas de leitura (em ms)

// --- Função para Calcular CRC16 (Modbus) ---

// --- Estrutura para Fila de Requisições de Leitura ---
// Define uma estrutura para armazenar os detalhes de uma requisição Modbus.
struct ModbusRequest {
  uint8_t slaveId;      // ID do escravo
  uint16_t regAddr;     // Endereço do registrador
  uint16_t numRegs;     // Número de registradores a ler/escrever
  uint16_t value;       // Valor para escrita (usado apenas para requisições de escrita)
  uint8_t functionCode; // Código da função Modbus (0x03 para leitura, 0x06 para escrita)
  uint8_t retriesLeft;  // Tentativas restantes para esta requisição
  unsigned long lastAttemptTime; // Tempo da última tentativa de envio
  bool active;          // Indica se esta entrada da fila está ativa
};

#define MAX_REQUEST_QUEUE_SIZE 5 // Tamanho máximo da fila de requisições
ModbusRequest requestQueue[MAX_REQUEST_QUEUE_SIZE];
uint8_t queueHead = 0; // Índice do próximo item a ser processado
uint8_t queueTail = 0; // Índice do próximo slot livre

// --- Funções de Fila ---
// Adiciona uma requisição à fila.
bool enqueueRequest(uint8_t slaveId, uint16_t regAddr, uint16_t numRegs, uint8_t functionCode, uint16_t value = 0) {
  if (((queueTail + 1) % MAX_REQUEST_QUEUE_SIZE) == queueHead) {
    Debug.println(F("Fila de requisicoes cheia!"));
    return false;
  }
  requestQueue[queueTail] = {
    slaveId, regAddr, numRegs, value, functionCode,
    (functionCode == 0x06) ? MAX_WRITE_RETRIES : MAX_READ_RETRIES, // Define retries com base na função
    0, true
  };
  queueTail = (queueTail + 1) % MAX_REQUEST_QUEUE_SIZE;
  return true;
}

// Remove a requisição atual da fila.
void dequeueRequest() {
  if (queueHead == queueTail) {
    return; // Fila vazia
  }
  requestQueue[queueHead].active = false; // Marca como inativa
  queueHead = (queueHead + 1) % MAX_REQUEST_QUEUE_SIZE;
}

// Esta função implementa o algoritmo CRC-16 Modbus, essencial para a integridade dos pacotes.
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

// --- Funções de Envio de Requisição Modbus ---
void sendModbusRequest(uint8_t id, uint8_t functionCode, uint16_t regAddr, uint16_t data) {
  uint8_t frame[8]; // Tamanho máximo para FC 0x03 ou 0x06
  uint8_t frameSize = 0;

  frame[0] = id;              
  frame[1] = functionCode;    

  if (functionCode == 0x03) { // Leitura de Holding Registers
    frame[2] = regAddr >> 8;    
    frame[3] = regAddr & 0xFF;  
    frame[4] = data >> 8;       // data aqui é numRegs
    frame[5] = data & 0xFF;     // data aqui é numRegs
    frameSize = 6;
  } else if (functionCode == 0x06) { // Escrita de Single Holding Register
    frame[2] = regAddr >> 8;    
    frame[3] = regAddr & 0xFF;  
    frame[4] = data >> 8;       // data aqui é o valor a ser escrito
    frame[5] = data & 0xFF;     // data aqui é o valor a ser escrito
    frameSize = 6;
  } else {
    Debug.printf("Funcao Modbus 0x%02X nao suportada para envio.\n", functionCode);
    return;
  }

  uint16_t crc = crc16_modbus(frame, frameSize);
  frame[frameSize] = crc & 0xFF;      
  frame[frameSize + 1] = crc >> 8;        
  frameSize += 2;

  Debug.print("TX: ");
  for (uint8_t i = 0; i < frameSize; i++) {
    if (frame[i] < 0x10) Debug.print("0");
    Debug.print(frame[i], HEX);
    Debug.print(" ");
  }
  Debug.println();

  digitalWrite(DE_RE, HIGH); 
  delayMicroseconds(100); 

  Serial.write(frame, frameSize); 
  Serial.flush();

  unsigned long txTime = (frameSize * 1040UL); 
  delayMicroseconds(txTime + 300); 

  digitalWrite(DE_RE, LOW); 

  Debug.println("Pacote enviado!");
}

// --- Função para Receber e Processar Respostas Modbus ---
ModbusResponseStatus receiveResponse(uint8_t expectedId, uint8_t expectedFunction, uint16_t* readBuffer = nullptr, uint8_t numRegsToRead = 0) {
  uint8_t buffer[64]; 
  uint8_t idx = 0;    
  unsigned long start = millis();
  
  while (millis() - start < MODBUS_TIMEOUT_MS) { // Usa o timeout ajustável
    if (Serial.available()) {
      uint8_t b = Serial.read();
      if (idx == 0 && (b < 1 || b > 247)) continue;
      if (idx < sizeof(buffer)) buffer[idx++] = b;
    }
  }

  if (idx == 0){
    Debug.println(" Nenhuma resposta recebida.");
    return RESPONSE_NO_RESPONSE;
  }

  Debug.print("RX: ");
  for (uint8_t i = 0; i < idx; i++){
    if (buffer[i] < 0x10) Debug.print("0");
    Debug.print(buffer[i], HEX);
    Debug.print(" ");
  }
  Debug.println();

  if (idx < 5) {
    Debug.println("Resposta muito curta para ser valida.");
    return RESPONSE_TOO_SHORT;
  }

  uint16_t crcCalc = crc16_modbus(buffer, idx - 2); 
  uint16_t crcRecv = buffer[idx - 2] | (buffer[idx - 1] << 8); 

  if (crcCalc != crcRecv){
    Debug.printf("CRC invalido (calculado %04X, recebido %04X)\n", crcCalc, crcRecv);
    return RESPONSE_CRC_ERROR;
  }
  Debug.println("CRC OK");

  if (buffer[0] != expectedId) {
    Debug.printf("ID de escravo inesperado na resposta: %d (esperado %d)\n", buffer[0], expectedId);
    return RESPONSE_UNEXPECTED_ID;
  }

  if ((buffer[1] & 0x80)) { 
    Debug.printf("Excecao Modbus! Codigo: 0x%02X\n", buffer[2]);
    return RESPONSE_EXCEPTION;
  }

  if (buffer[1] != expectedFunction) {
    Debug.printf("Funcao Modbus inesperada na resposta: 0x%02X (esperado 0x%02X)\n", buffer[1], expectedFunction);
    return RESPONSE_UNEXPECTED_FUNCTION;
  }

  // --- Processamento da Resposta de Leitura (Função 0x03) ---
  if (expectedFunction == 0x03 && readBuffer != nullptr && numRegsToRead > 0) {
    uint8_t byteCount = buffer[2]; 
    if (byteCount != (numRegsToRead * 2)) {
      Debug.printf("Numero de bytes de dados inesperado: %d (esperado %d)\n", byteCount, numRegsToRead * 2);
      return RESPONSE_UNEXPECTED_BYTE_COUNT; // Novo status de erro
    }
    Debug.print("Dados (Registradores): ");
    for (uint8_t i = 0; i < byteCount; i += 2) {
      uint16_t value = (buffer[3 + i] << 8) | buffer[4 + i]; 
      readBuffer[i/2] = value; // Armazena no buffer passado
      Debug.printf("Reg[%d]: %d W",HR_ACT_PWR_OUT, value * 100);
    }
    Debug.println();
  }
  // --- Processamento da Resposta de Escrita (Função 0x06) ---
  else if (expectedFunction == 0x06) {
    uint16_t regAddr = (buffer[2] << 8) | buffer[3];
    uint16_t value = (buffer[4] << 8) | buffer[5];
    Debug.printf("Escrita confirmada no registrador %d com valor %d.\n", regAddr, value);
  }
  
  yield(); 
  return RESPONSE_SUCCESS;
}

// --- Função de Escrita com Retry ---
bool writeWithRetry(uint8_t id, uint16_t regAddr, uint16_t value, uint8_t maxRetries, unsigned long retryDelay) {
  for (uint8_t attempt = 1; attempt <= maxRetries; attempt++) {
    Debug.printf("Tentativa %d de %d: Escrevendo %d no registrador %d do Escravo ID %d...\n", attempt, maxRetries, value, regAddr, id);
    sendModbusRequest(id, 0x06, regAddr, value);
    ModbusResponseStatus status = receiveResponse(id, 0x06);

    if (status == RESPONSE_SUCCESS) {
      Debug.printf("Escrita bem-sucedida para Escravo ID %d no registrador %d.\n", id, regAddr);
      return true;
    } else {
      Debug.printf("Falha na escrita para Escravo ID %d (Status: %d). Re-tentando em %lu ms...\n", id, status, retryDelay);
      delay(retryDelay);
    }
  }
  Debug.printf("Todas as %d tentativas de escrita falharam para Escravo ID %d no registrador %d.\n", maxRetries, id, regAddr);
  return false;
}

// --- Função de Leitura com Retry ---
bool readWithRetry(uint8_t id, uint16_t regAddr, uint16_t numRegs, uint16_t* readBuffer, uint8_t maxRetries, unsigned long retryDelay) {
  for (uint8_t attempt = 1; attempt <= maxRetries; attempt++) {
    Debug.printf("Tentativa %d de %d: Lendo %d registradores do endereco %d do Escravo ID %d...\n", attempt, maxRetries, numRegs, regAddr, id);
    sendModbusRequest(id, 0x03, regAddr, numRegs);
    ModbusResponseStatus status = receiveResponse(id, 0x03, readBuffer, numRegs);

    if (status == RESPONSE_SUCCESS) {
      Debug.printf("Leitura bem-sucedida para Escravo ID %d do endereco %d.\n", id, regAddr);
      return true;
    } else {
      Debug.printf("Falha na leitura para Escravo ID %d (Status: %d). Re-tentando em %lu ms...\n", id, status, retryDelay);
      delay(retryDelay);
    }
  }
  Debug.printf("Todas as %d tentativas de leitura falharam para Escravo ID %d do endereco %d.\n", maxRetries, id, regAddr);
  return false;
}

// --- Verificação de NaN (Not a Number) ---
// Verifica se um valor lido corresponde a um dos padrões NaN da tabela fornecida.
bool isNaNValue(uint16_t value, const char* modelType) {
  if (strcmp(modelType, "bitfield16") == 0 || strcmp(modelType, "enum16") == 0 || strcmp(modelType, "uint16") == 0) {
    return value == 0xFFFF;
  } else if (strcmp(modelType, "Int16") == 0 || strcmp(modelType, "sunssf") == 0) {
    return value == 0x8000;
  } else if (strcmp(modelType, "acc32") == 0 || strcmp(modelType, "acc64") == 0) {
    // Para 32/64 bits, 0x0000 0000 indica NaN. Precisaríamos de 2 registradores para verificar.
    // Assumindo que 'value' é um registrador de 16 bits, não podemos verificar diretamente 32/64 bits NaN.
    // Para uma verificação completa, precisaríamos ler 2 ou 4 registradores e combiná-los.
    // Por simplicidade, para 16 bits, 0x0000 não é NaN.
    return false; 
  } else if (strcmp(modelType, "bitfield32") == 0 || strcmp(modelType, "uint32") == 0) {
    // Similar ao acc32/acc64, 0xFFFF FFFF para 32 bits. Não verificável com um único uint16_t.
    return false;
  } else if (strcmp(modelType, "Int32") == 0) {
    // 0x8000 0000 para 32 bits. Não verificável com um único uint16_t.
    return false;
  } else if (strcmp(modelType, "string") == 0) {
    // 0x0000 a nnnn. Depende do contexto da string. Não é um NaN fixo.
    return false;
  }
  return false; // Tipo de modelo desconhecido ou não aplicável para NaN de 16 bits
}

//--- Comandos de leitura e escrita na memoria RAM do RTC ---
byte r_mem(byte address) {
  byte data = 0xFF; //Valor padrão em caso de falha
  bool success = false;

  for (uint8_t attempt = 0; attempt < I2C_MAX_RETRIES && !success; attempt++) {
    Wire.beginTransmission(I2C_ADDR_RTC);
    Wire.write(address);
    uint8_t status = Wire.endTransmission(false); // false = mantém barramento ativo

    if (status != 0) {
      delay(2);
      continue; //erro de comuncicação, tenta de novo
    }

    // Solicitação 1 byte e espera resposta
    uint32_t start = millis();
    Wire.requestFrom((int)I2C_ADDR_RTC, 1); //Solicita 1 byte do DS1307
    while (!Wire.available() && millis() - start < I2C_TIMEOUT_MS) yield();
    if (Wire.available()) {
      data = Wire.read();
      success = true;
    }
    else {
      delay(2); //Pausa antes de retry
    }
  }
  
  //Se falhar, comunica o debug
  if (!success) {
    Debug.printf("[I2C][READ] Falha no endereço 0x%02X após %u tentativas\n", address, I2C_MAX_RETRIES);
  }
  return data;
}

void w_mem(byte address, byte data) {
  bool success = false;

  for (uint8_t attempt = 0; attempt < I2C_MAX_RETRIES && !success; attempt++) {

    Wire.beginTransmission(I2C_ADDR_RTC);
    Wire.write(address);
    Wire.write(data);
    uint8_t status = Wire.endTransmission();  //encerra transmissão

    if (status == 0) {
        success == true;
    }
    else {
      Debug.printf("[I2C][WRITE] Erro (%u) ao escrever 0x%20X em 0x%20X\n", status, data, address);
      delay(5); //Pausa antes do retry
    }
  }

  if (!success) {
    Debug.printf("[I2C][WRITE] Falha definitiva no endereço 0x%02X\n", address);
  }
}

Program currentProgram;
void checkPrograms(DateTime now) {
  
  uint8_t activeProgram = 0; // 0 = default
  uint8_t today = now.dayOfTheWeek(); // 0 - Domingo ~ 6 - Sábado
  
  // Percorre todos os programas da PROGMEM
  for (int i = 0; i < NUM_PROGRAMS; i++) {
    bool dayMatch = false;
    Program p;
    memcpy_P(&p, &programs[i], sizeof(Program));    

    // Filtra por programa ativo
    if (!p.enabled) continue;

    // Filtra por mês (se configurado)
    if (p.month != 0 && now.month() != p.month) continue;

    // Filtra por dia da semana (se configurado)
    if(p.dayOfWeek <= 6) {
      dayMatch = (today == p.dayOfWeek);
    }
    else if (p.dayOfWeek == 10) {
      //Dias úteis (Segunda a Sexta)
      dayMatch = (today >= 1 && today <= 5);
    }
    else if (p.dayOfWeek == 11) {
      dayMatch = (today == 0 || today == 6);
    }

    if (!dayMatch) continue;

    uint16_t nowMins = now.hour() * 60 + now.minute();
    uint16_t startMins = p.startHour * 60 + p.startMinute;
    uint16_t endMins = p.endHour * 60 + p.endMinute;

    if (nowMins >= startMins && nowMins < endMins) {
      activeProgram = p.id;
      if (currentProgram.id != p.id) {
        activateProgram(p);
        currentProgram = p;
      }
      break; // Apenas um programa ativo por vez
    }
  }

  // Nenhum programa válido encontrado → volta para o Default
  if (activeProgram == 0 && currentProgram.id != 0) {
    Program p;
    memcpy_P(&p, &defaultProgram, sizeof(Program));
    activateProgram(p);
    currentProgram = p;
  }
}

// --- Rodando programa de alteração da potência ---
void activateProgram(Program p) {
  bool limit_enable = true;

  uint32_t powerLimit = p.power;
  uint32_t pwrPercent = powerLimit * 1000 / POT_INV; // Escala de 0,1%. É preciso multiplicar por 1000 para ajustar os valores

  // Desativa limitação caso a potência for maior ou igual a potência do inversor
  if (pwrPercent >= 1000) {
    limit_enable = false;
  }

  // 1. Habilita ou desabilita a Limitação
  Debug.printf("Escrevendo flag %d no registrador %d do Escravo ID %d...\n", limit_enable, HR_EN_ACT_PWR_LIM, SLAVE_1_ID);
    if (!writeWithRetry(SLAVE_1_ID, HR_EN_ACT_PWR_LIM, limit_enable, MAX_WRITE_RETRIES, WRITE_RETRY_DELAY_MS)) {
      Debug.println("Falha critica: Nao foi possivel escrever a flag no Escravo 2.");
    }
    delay(100);

  // 2. Define o Limite de Potência
  Debug.printf("Escrevendo valor principal %d no registrador %d do Escravo ID %d...\n", pwrPercent, HR_ACT_PWR_LIM_VL, SLAVE_1_ID);
  if (!writeWithRetry(SLAVE_1_ID, HR_ACT_PWR_LIM_VL, pwrPercent, MAX_WRITE_RETRIES, WRITE_RETRY_DELAY_MS)) {
    Debug.println("Falha critica: Nao foi possivel escrever o valor principal no Escravo 1.");
  }
  delay(100);

  // 3. Armazena o ID ativo na RAM do RTC (endereços 0x08-0x3F)

  uint32_t change = millis();
  while (millis() - change > 5000) { //Aguarda 10 segundos até a potência estabilizar
    digitalWrite(LED_PIN, HIGH);
  }
}

// ================================== SETUP ====================================
void setup() {

  // Inicia a comunicação serial para depuração (SoftwareSerial) e para Modbus (HardwareSerial).
  Serial.begin(RS485_BAUD); 
  Debug.begin(115000); // Taxa de baud para o monitor serial de depuração.

  // Início do barramento I2C
  Wire.begin(4, 5); // SDA = GPIO4, SCL = GPIO5

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
  //while(digitalRead(BTN_PIN)) yield();
  //Aguarda apertar o botão para iniciar Debug. Se comentado, inicia direto
  // =====================================================

  delay(2000); // Pequena pausa para estabilização.
  Debug.println("\n=== Mestre Modbus RTU Manual ===");

  // --- Iniciando RTC, com até 5 tentativas de conectar, caso não funcionar de primeira
  bool rtcFound = false;
  for (int i = 0; i < 5; i++) {
    if (rtc.begin()) {
      rtcFound = true;
      break;
    }
    delay(200);
  }
  // Caso o RTC não for encontrado ou não estiver funcionando corretamente, indica com o led ligado continuamente
  if (rtcFound) {
    Debug.println("RTC inciado.");
  } else {
    Debug.println("RTC não encontrado!");
    digitalWrite(LED_PIN, HIGH);
    while(1) yield();
  }
  
  // Verifica se o RTC está funcionando
  if (!rtc.isrunning()) {
    Debug.println("RTC não estava rodando, necessário ajuste de data e hora.");
    digitalWrite(LED_PIN, HIGH);
    while(1) yield();
  }

  // Variáveis armazenadas na RAM para o caso o RTC falhar
  DateTime now = rtc.now();

  int s = now.second();
  int m = now.minute();
  int h = now.hour();
  int d = now.dayOfTheWeek();

  //      0 Para Dom: 00:00:00
  // 604799 para Sab: 23:59:59
  sec_time = s + m * 60 + h * 3600 + (d - 1) * 86400;

  // --- Configuração do Timer1 hardware (ESP8266) ---
  ticker.attach(1.0, onTick); //Interrupção a cada 1 segundo
  Serial.println("Iniciado. Pressione o botão para ajustar o RTC.");
}

void loop() {
  
  if (tickFlag) {

    tickFlag = false;
    digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);

    DateTime now = rtc.now();
    
    checkPrograms(now);
    Debug.printf("%02d:%02d:%02d (%d)\n", now.hour(), now.minute(), now.second(), now.dayOfTheWeek());
  }
}/*
    // Verificação da potência do inversor após ajuste. No caso de não estar o mesmo do valor setado, envia o ajuste outra vez
    
    // Adiciona a requisição de leitura à fila
    if (!enqueueRequest(SLAVE_1_ID, HR_ACT_PWR_OUT, READ_REG_COUNT, 0x03)) {
      Debug.println(F("[Modbus][Erro] Nao foi possivel adicionar requisicao de leitura a fila."));
    }
    // Processa a fila de requisições
    if (queueHead != queueTail) { // Se a fila não estiver vazia
      ModbusRequest& currentRequest = requestQueue[queueHead];

      // Se a requisição ainda não foi enviada ou se é hora de re-tentar
      if (currentRequest.lastAttemptTime == 0 || (millis() - currentRequest.lastAttemptTime >= (currentRequest.functionCode == 0x06 ? WRITE_RETRY_DELAY_MS : READ_RETRY_DELAY_MS))) {
        currentRequest.lastAttemptTime = millis();
        currentRequest.retriesLeft--;

        Debug.printf("Processando requisicao da fila (ID: %d, End: %d, Func: 0x%02X). Tentativas restantes: %d\n", 
                    currentRequest.slaveId, currentRequest.regAddr, currentRequest.functionCode, currentRequest.retriesLeft);

        ModbusResponseStatus status;
        uint16_t readValues[READ_REG_COUNT]; // Buffer temporário para leitura

        if (currentRequest.functionCode == 0x03) { // Requisição de Leitura
          sendModbusRequest(currentRequest.slaveId, currentRequest.functionCode, currentRequest.regAddr, currentRequest.numRegs);
          status = receiveResponse(currentRequest.slaveId, currentRequest.functionCode, readValues, currentRequest.numRegs);
          if (status == RESPONSE_SUCCESS) {
            Debug.printf("Leitura da fila bem-sucedida para Escravo ID %d.\n", currentRequest.slaveId);
            // Aqui você pode processar os valores lidos (readValues)
            /*Program p = 
            if (readValues[i] > p.power/100 + 1) {
              activateProgram(p);
            }*//*
            for (int i = 0; i < currentRequest.numRegs; i++) {
              Debug.printf("  Lido Reg[%d]: %d. Eh NaN? %s\n", currentRequest.regAddr + i, readValues[i], isNaNValue(readValues[i], "Int16") ? "SIM" : "NAO");
            }
            
            dequeueRequest(); // Remove da fila se bem-sucedida
          } else if (currentRequest.retriesLeft == 0) {
            Debug.printf("Leitura da fila falhou para Escravo ID %d apos todas as tentativas.\n", currentRequest.slaveId);
            dequeueRequest(); // Remove da fila se todas as tentativas falharam
          }
        } else if (currentRequest.functionCode == 0x06) { // Requisição de Escrita
          sendModbusRequest(currentRequest.slaveId, currentRequest.functionCode, currentRequest.regAddr, currentRequest.value);
          status = receiveResponse(currentRequest.slaveId, currentRequest.functionCode);
          if (status == RESPONSE_SUCCESS) {
            Debug.printf("Escrita da fila bem-sucedida para Escravo ID %d.\n", currentRequest.slaveId);
            dequeueRequest(); // Remove da fila se bem-sucedida
          } else if (currentRequest.retriesLeft == 0) {
            Debug.printf("Escrita da fila falhou para Escravo ID %d apos todas as tentativas.\n", currentRequest.slaveId);
            dequeueRequest(); // Remove da fila se todas as tentativas falharam
          }
        }
      }
    }
  }
}*/
