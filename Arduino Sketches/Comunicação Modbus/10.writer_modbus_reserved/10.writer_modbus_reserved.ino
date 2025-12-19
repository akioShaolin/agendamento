// Concessionária: Neoenergia

// Declaração de Bibliotecas
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Ticker.h>

// --- Configurações de Hardware e Pinos ---
// Inicialização do RTC, timer por hardware
Ticker ticker;

// A SoftwareSerial Debug é usada para imprimir mensagens de depuração sem interferir na Serial principal
// que será usada para o Modbus RTU.
// No ESP8266, os pinos 2 (RX) e 14 (TX) são GPIOs comuns para SoftwareSerial.
SoftwareSerial Debug(2, 0); 

// Definição dos pinos
#define LED_PIN           LED_BUILTIN   //  GPIO2 - Led Built in
#define DE_RE             12            // Pino ligado ao SN75176B (RS485 transceiver)
//#define BTN_PIN           0             // Botão da placa com pull up
#define HALF_OR_FULL_PIN  13            // Half-Duplex:LOW  Full-Duplex-HIGH

// --- Configurações Modbus ---
// IDs do dispositivo escravo Modbus.
#define SLAVE_1_ID 2
#define RS485_BAUD 9600

// --- Configuração dos Registradores pertinentes ---

#define COMM_REGISTER 0xC34F

// --- Variáveis de Controle ---
// Flag para garantir que a escrita única seja feita apenas uma vez no setup.
bool initialWriteDone = false;
// Flag vinculada ao Timer1 para envio de mensagens a inversor
volatile bool tickFlag = false;

#define POT_INV 75000
#define NUM_PROGRAMS 106

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
void sendModbusReadWrite(uint8_t id, uint16_t regAddr, uint16_t data) {
  uint8_t frame[31]; // Tamanho máximo para FC 0x03 ou 0x06
  uint8_t frameSize = 0;

  frame[0] = id;              
  frame[1] = 0x17;    
  frame[2] = regAddr >> 8;
  frame[3] = regAddr & 0xFF;
  frame[4] = 0x00;
  frame[5] = 0x09;
  frame[6] = regAddr >> 8;
  frame[7] = regddr & 0xFF;
  frame[8] = 0x00;
  frame[9] = 0x14;

  for (int i; i <= 20; i+=2) {
    frame[i+9] = data[]
    frame[i+10] = 
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
  Debug.begin(115200); // Taxa de baud para o monitor serial de depuração.

  // Configura o pino DE_RE como saída para controlar a direção do transceptor RS485. O pino HALF_OR_FULL_PIN se refere a Half ou Full Duplex, devido a um HC4066 selecionando entre 2 transceivers de RS485.
  pinMode(DE_RE, OUTPUT);
  pinMode(HALF_OR_FULL_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  //pinMode(BTN_PIN, INPUT_PULLUP); 

  digitalWrite(DE_RE, LOW); // Inicializa em modo de recepção (RX).
  digitalWrite(HALF_OR_FULL_PIN, LOW);  //Define a Rede RS485 como Half Duplex
  //digitalWrite(BTN_PIN, LOW); // Inicia o led como desligado

  //   === ATENÇÃO! COMENTAR LINHA ABAIXO AO APLICAR ===
  // === ESSA LINHA SERVE APENAS PARA AGUARDAR O DEBUG ===
  //while(digitalRead(BTN_PIN)) yield();
  //Aguarda apertar o botão para iniciar Debug. Se comentado, inicia direto
  // =====================================================

  delay(2000); // Pequena pausa para estabilização.
  Debug.println("\n=== Mestre Modbus RTU Manual ===");

  // --- Configuração do Timer1 hardware (ESP8266) ---
  ticker.attach(0.5, onTick); //Interrupção a cada 0,5 segundo
}

void loop() {
  
  if (tickFlag) {

    tickFlag = false;
    digitalWrite(LED_PIN, digitalRead(LED_PIN) ^ 1);
    
  }
}