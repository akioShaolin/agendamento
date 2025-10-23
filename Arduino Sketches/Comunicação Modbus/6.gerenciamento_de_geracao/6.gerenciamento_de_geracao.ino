#include <Arduino.h>
#include <SoftwareSerial.h>

// --- Configurações de Hardware e Pinos ---
// A SoftwareSerial Debug é usada para imprimir mensagens de depuração sem interferir na Serial principal
// que será usada para o Modbus RTU.
// No ESP8266, os pinos 2 (RX) e 14 (TX) são GPIOs comuns para SoftwareSerial.
SoftwareSerial Debug(2, 14); 

// Pino do ESP8266 conectado ao DE/RE do transceptor RS485 (GPIO12).
// Este pino controla a direção da comunicação: HIGH para transmitir, LOW para receber.
#define DE_RE 12            // Pino ligado ao SN75176B (RS485 transceiver)
#define RST_PIN 0           // Botão da placa com pull up
#define HALF_OR_FULL_PIN 13 // Half-Duplex:LOW  Full-Duplex-HIGH

// Taxa de comunicação RS485. Deve ser a mesma configurada nos dispositivos escravos.
#define RS485_BAUD 9600

// --- Configurações Modbus ---
// IDs dos dois dispositivos escravos Modbus.
#define SLAVE_ID_1 2
#define SLAVE_ID_2 3

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
#define MAX_WRITE_RETRIES 3        // Número máximo de tentativas para uma escrita
#define WRITE_RETRY_DELAY_MS 200   // Atraso entre as tentativas de escrita (em ms)
#define MAX_READ_RETRIES 2         // Número máximo de tentativas para uma leitura
#define READ_RETRY_DELAY_MS 100    // Atraso entre as tentativas de leitura (em ms)

// Quantidade de registradores a ler. 1 por 1 por enquanto
#define READ_REG_COUNT 1

#define HR_ACT_PWR_OUT      0x9C8F   //Holding Register Active Power Output. Scale: 100 [W]
#define HR_EN_ACT_PWR_LIM   0x9D6B   //Holding Register Enable Active Power Limit. [0 - Off, 1 - On]
#define HR_ACT_PWR_LIM_VL   0x9D6C   //Holding Register Active Power Limit Value. Scale: 0.1 [%]  (0~1100)

// --- Variáveis de Controle ---
// Flag para garantir que a escrita única seja feita apenas uma vez no setup.
bool initialWriteDone = false;

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
    Debug.println("Fila de requisicoes cheia!");
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

// --- Função para Calcular CRC16 (Modbus) ---
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

// --- Setup: Configurações Iniciais ---
void setup() {
  // Inicia a comunicação serial para depuração (SoftwareSerial) e para Modbus (HardwareSerial).
  Serial.begin(RS485_BAUD); 
  Debug.begin(9600); // Taxa de baud para o monitor serial de depuração.

  // Configura o pino DE_RE como saída para controlar a direção do transceptor RS485.
  pinMode(DE_RE, OUTPUT);
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(RST_PIN, INPUT_PULLUP); 

  digitalWrite(DE_RE, LOW); // Inicializa em modo de recepção (RX).
  digitalWrite(HALF_OR_FULL_PIN, LOW);  //Define a Rede RS485 como Half Duplex

  while(digitalRead(RST_PIN)) yield(); //Aguarda apertar o botão para iniciar
  delay(2000); // Pequena pausa para estabilização.
  Debug.println("\n=== Mestre Modbus RTU Manual Avançado ===");

  // --- Escrita Única no Setup ---
  Debug.println("Iniciando escrita unica nos dispositivos com retry...");

  // Esta seção garante que o valor 1 seja escrito no registrador 40300
  // de ambos os dispositivos escravos (ID 2 e ID 3) apenas uma vez.

  // --- Escrita da Flag (40300) com Retry ---
  Debug.printf("Escrevendo flag %d no registrador %d do Escravo ID %d...\n", 1, HR_EN_ACT_PWR_LIM, SLAVE_ID_1);
  if (!writeWithRetry(SLAVE_ID_1, HR_EN_ACT_PWR_LIM, 1, MAX_WRITE_RETRIES, WRITE_RETRY_DELAY_MS)) {
    Debug.println("Falha critica: Nao foi possivel escrever a flag no Escravo 1.");
  }
  delay(100);

  Debug.printf("Escrevendo flag %d no registrador %d do Escravo ID %d...\n", 1, HR_EN_ACT_PWR_LIM, SLAVE_ID_2);
  if (!writeWithRetry(SLAVE_ID_2, HR_EN_ACT_PWR_LIM, 1, MAX_WRITE_RETRIES, WRITE_RETRY_DELAY_MS)) {
    Debug.println("Falha critica: Nao foi possivel escrever a flag no Escravo 2.");
  }
  delay(100);

  // --- Escrita do Valor Principal (40301) com Retry ---
  Debug.printf("Escrevendo valor principal %d no registrador %d do Escravo ID %d...\n", 200, HR_ACT_PWR_LIM_VL, SLAVE_ID_1);
  if (!writeWithRetry(SLAVE_ID_1, HR_ACT_PWR_LIM_VL, 200, MAX_WRITE_RETRIES, WRITE_RETRY_DELAY_MS)) {
    Debug.println("Falha critica: Nao foi possivel escrever o valor principal no Escravo 1.");
  }
  delay(100);

  Debug.printf("Escrevendo valor principal %d no registrador %d do Escravo ID %d...\n", 500, HR_ACT_PWR_LIM_VL, SLAVE_ID_2);
  if (!writeWithRetry(SLAVE_ID_2, HR_ACT_PWR_LIM_VL, 500, MAX_WRITE_RETRIES, WRITE_RETRY_DELAY_MS)) {
    Debug.println("Falha critica: Nao foi possivel escrever o valor principal no Escravo 2.");
  }
  delay(100);

  Debug.println("Escritas iniciais com retry concluidas.");
  initialWriteDone = true; 
  Debug.println("--------------------------------------------------");
}

// --- Loop Principal: Leitura Contínua ---
void loop() {
  // Garante que a escrita inicial foi feita antes de começar as leituras contínuas.
  if (!initialWriteDone) {
    delay(100); // Espera um pouco se a escrita ainda não terminou (não deve acontecer se o setup for síncrono)
    return;
  }

  static unsigned long lastReadRequestTime = 0; // Armazena o tempo da última leitura.
  static uint8_t currentSlaveToRead = SLAVE_ID_1; 

  // Realiza leituras a cada 5000ms.
  if (millis() - lastReadRequestTime >= 5000 && (queueHead == queueTail || !requestQueue[queueHead].active)) {
    lastReadRequestTime = millis(); // Atualiza o tempo da última leitura.

    uint8_t slaveIdToAdd = currentSlaveToRead; // Escravo para adicionar à fila

    // Alterna entre os dois escravos.
    if (currentSlaveToRead == SLAVE_ID_1) {
      currentSlaveToRead = SLAVE_ID_2;
    } else {
      currentSlaveToRead = SLAVE_ID_1;
    }

    // Adiciona a requisição de leitura à fila
    if (!enqueueRequest(slaveIdToAdd, HR_ACT_PWR_OUT, READ_REG_COUNT, 0x03)) {
      Debug.println("Erro: Nao foi possivel adicionar requisicao de leitura a fila.");
    }
  }
  
  // Processa a fila de requisições
  if (queueHead != queueTail) { // Se a fila não estiver vazia
    ModbusRequest& currentRequest = requestQueue[queueHead];

    // Se a requisição ainda não foi enviada ou se é hora de re-tentar
    if (currentRequest.lastAttemptTime == 0 || (millis() - currentRequest.lastAttemptTime >= (currentRequest.functionCode == 0x06 ? WRITE_RETRY_DELAY_MS : READ_RETRY_DELAY_MS))) {
      currentRequest.lastAttemptTime = millis();
      currentRequest.retriesLeft--;

      Debug.printf("Processando requisicao da fila (ID: %d, End: %d, Func: 0x%X). Tentativas restantes: %d\n", 
                   currentRequest.slaveId, currentRequest.regAddr, currentRequest.functionCode, currentRequest.retriesLeft);

      ModbusResponseStatus status;
      uint16_t readValues[READ_REG_COUNT]; // Buffer temporário para leitura

      if (currentRequest.functionCode == 0x03) { // Requisição de Leitura
        sendModbusRequest(currentRequest.slaveId, currentRequest.functionCode, currentRequest.regAddr, currentRequest.numRegs);
        status = receiveResponse(currentRequest.slaveId, currentRequest.functionCode, readValues, currentRequest.numRegs);
        if (status == RESPONSE_SUCCESS) {
          Debug.printf("Leitura da fila bem-sucedida para Escravo ID %d.\n", currentRequest.slaveId);
          // Aqui você pode processar os valores lidos (readValues)
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
  yield();
}