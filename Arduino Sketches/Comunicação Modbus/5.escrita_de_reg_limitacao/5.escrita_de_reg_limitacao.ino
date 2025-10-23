#include <Arduino.h>
#include <SoftwareSerial.h>

// --- Configurações de Hardware e Pinos ---
// A SoftwareSerial Debug é usada para imprimir mensagens de depuração sem interferir na Serial principal
// que será usada para o Modbus RTU.
// No ESP8266, os pinos 2 (RX) e 14 (TX) são GPIOs comuns para SoftwareSerial.
SoftwareSerial Debug(2, 14); 

// Pino do ESP8266 conectado ao DE/RE do transceptor RS485 (GPIO12).
// Este pino controla a direção da comunicação: HIGH para transmitir, LOW para receber.
#define DE_RE 12 

// Taxa de comunicação RS485. Deve ser a mesma configurada nos dispositivos escravos.
#define RS485_BAUD 9600

// --- Configurações Modbus ---
// IDs dos dois dispositivos escravos Modbus.
#define SLAVE_ID_1 2     
#define SLAVE_ID_2 3     

#define RST_PIN 0

// Quantidade de registradores a ler.
#define READ_REG_COUNT 1 

// Endereço do registrador para escrita e leitura (40301 = 0x753D).
// Note que o Modbus usa endereços base 1 para referência, mas a maioria das implementações
// e o protocolo em si usam endereços base 0. 40301 é o registrador 300 (0x012C) na prática.
// Para o registrador 40301, o endereço Modbus é 40301 - 40001 = 300 (0x012C).
// No entanto, o seu sketch anterior usava 0x9D6C, que é 40300. Vamos manter a consistência
// com o seu uso anterior para o endereço 40301, que é 0x9D6C.
// Se o seu dispositivo espera 40301 como 0x012C, ajuste WRITE_REG_ADDR e READ_REG_ADDR para 0x012C.
// Por enquanto, usaremos 0x9D6C como no seu exemplo.
uint16_t WRITE_REG_ADDR = 0x9D6B; // Endereço do registrador 40301 (conforme seu uso anterior)
uint16_t READ_REG_ADDR = 0x9D6B;  // Endereço do registrador 40301 (conforme seu uso anterior)

// Valor a ser escrito no registrador 40301.
uint16_t WRITE_VALUE = 1;

// --- Variáveis de Controle ---
// Flag para garantir que a escrita única seja feita apenas uma vez no setup.
bool initialWriteDone = false;

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

// --- Função para Enviar Requisição Modbus (Leitura) ---
// Constrói e envia um pacote Modbus RTU para ler Holding Registers (Função 0x03).
void sendReadRequest(uint8_t id, uint16_t regAddr, uint16_t qty) {
  // Frame base para leitura: [ID][Função][AddrHi][AddrLo][QtyHi][QtyLo]
  uint8_t frame[8];
  frame[0] = id;              // ID do dispositivo escravo
  frame[1] = 0x03;            // Código da função: Ler Holding Registers
  frame[2] = regAddr >> 8;    // Byte alto do endereço inicial do registrador
  frame[3] = regAddr & 0xFF;  // Byte baixo do endereço inicial do registrador
  frame[4] = qty >> 8;        // Byte alto da quantidade de registradores a ler
  frame[5] = qty & 0xFF;      // Byte baixo da quantidade de registradores a ler

  // Calcula e adiciona o CRC ao final do frame.
  uint16_t crc = crc16_modbus(frame, 6);
  frame[6] = crc & 0xFF;      // Byte baixo do CRC
  frame[7] = crc >> 8;        // Byte alto do CRC

  // Exibe o pacote que será enviado para depuração.
  Debug.print("TX (Leitura): ");
  for (uint8_t i = 0; i < sizeof(frame); i++) {
    if (frame[i] < 0x10) Debug.print('0');
    Debug.print(frame[i], HEX);
    Debug.print(' ');
  }
  Debug.println();

  // Habilita o transceptor RS485 para o modo de transmissão (TX).
  digitalWrite(DE_RE, HIGH); 
  delayMicroseconds(100); // Pequeno atraso para garantir que o pino esteja estável.

  // Envia o frame Modbus pela porta Serial principal.
  Serial.write(frame, sizeof(frame)); 
  Serial.flush(); // Garante que todos os bytes foram enviados antes de mudar a direção.

  // Calcula o tempo necessário para transmitir o frame e adiciona uma margem.
  // Isso é crucial para evitar que o pino DE_RE mude para RX antes que a transmissão seja concluída.
  unsigned long txTime = (sizeof(frame) * 1040UL); // Aproximadamente 1040us por byte em 9600bps.
  delayMicroseconds(txTime + 300); 

  // Volta o transceptor RS485 para o modo de recepção (RX).
  digitalWrite(DE_RE, LOW); 

  Debug.println("Pacote de leitura enviado!");
}

// --- Função para Enviar Requisição Modbus (Escrita de um único registrador) ---
// Constrói e envia um pacote Modbus RTU para escrever um único Holding Register (Função 0x06).
void sendWriteRequest(uint8_t id, uint16_t regAddr, uint16_t value) {
  // Frame base para escrita: [ID][Função][AddrHi][AddrLo][ValHi][ValLo]
  uint8_t frame[8];
  frame[0] = id;              // ID do dispositivo escravo
  frame[1] = 0x06;            // Código da função: Escrever Single Holding Register
  frame[2] = regAddr >> 8;    // Byte alto do endereço do registrador
  frame[3] = regAddr & 0xFF;  // Byte baixo do endereço do registrador
  frame[4] = value >> 8;      // Byte alto do valor a ser escrito
  frame[5] = value & 0xFF;    // Byte baixo do valor a ser escrito

  // Calcula e adiciona o CRC ao final do frame.
  uint16_t crc = crc16_modbus(frame, 6);
  frame[6] = crc & 0xFF;      // Byte baixo do CRC
  frame[7] = crc >> 8;        // Byte alto do CRC

  // Exibe o pacote que será enviado para depuração.
  Debug.print("TX (Escrita): ");
  for (uint8_t i = 0; i < sizeof(frame); i++) {
    if (frame[i] < 0x10) Debug.print('0');
    Debug.print(frame[i], HEX);
    Debug.print(' ');
  }
  Debug.println();

  // Habilita o transceptor RS485 para o modo de transmissão (TX).
  digitalWrite(DE_RE, HIGH); 
  delayMicroseconds(100); 

  // Envia o frame Modbus pela porta Serial principal.
  Serial.write(frame, sizeof(frame)); 
  Serial.flush(); 

  // Calcula o tempo necessário para transmitir o frame e adiciona uma margem.
  unsigned long txTime = (sizeof(frame) * 1040UL); 
  delayMicroseconds(txTime + 300); 

  // Volta o transceptor RS485 para o modo de recepção (RX).
  digitalWrite(DE_RE, LOW); 

  Debug.println("Pacote de escrita enviado!");
}

// --- Função para Receber e Processar Respostas Modbus ---
// Aguarda uma resposta Modbus, verifica o CRC e exibe os dados.
void receiveResponse(uint8_t expectedId, uint8_t expectedFunction) {
  uint8_t buffer[64]; // Buffer para armazenar os bytes recebidos.
  uint8_t idx = 0;    // Índice para o buffer.
  unsigned long start = millis();
  
  // Loop para receber bytes com um timeout de 500ms.
  while (millis() - start < 500) {
    if (Serial.available()) {
      uint8_t b = Serial.read();

      // Ignora bytes 

//inválidos até encontrar um ID de escravo válido (1-247).
      if (idx == 0 && (b < 1 || b > 247)) continue;

      // Armazena o byte no buffer, se houver espaço.
      if (idx < sizeof(buffer)) buffer[idx++] = b;
    }
  }

  // Se nenhum byte foi recebido, imprime uma mensagem e retorna.
  if (idx == 0){
    Debug.println(" Nenhuma resposta recebida.");
    return;
  }

  // Exibe o pacote recebido para depuração.
  Debug.print("RX: ");
  for (uint8_t i = 0; i < idx; i++){
    if (buffer[i] < 0x10) Debug.print("0");
    Debug.print(buffer[i], HEX);
    Debug.print(" ");
  }
  Debug.println();

  // --- Verificação do CRC ---
  // O CRC é calculado sobre os bytes recebidos (excluindo os dois últimos bytes que são o próprio CRC).
  // O CRC recebido pode estar em ordem de bytes diferente dependendo da implementação do escravo.
  if (idx >= 5) { // Um frame Modbus válido tem pelo menos 5 bytes (ID, Função, Byte Count, CRC)
    uint16_t crcCalc = crc16_modbus(buffer, idx - 2); // Calcula o CRC dos dados recebidos
    uint16_t crcRecv = buffer[idx - 2] | (buffer[idx - 1] << 8); // CRC recebido (byte baixo primeiro)

    if (crcCalc == crcRecv){
      Debug.println("CRC OK");

      // --- Processamento da Resposta de Leitura (Função 0x03) ---
      if (buffer[0] == expectedId && buffer[1] == expectedFunction && expectedFunction == 0x03) {
        uint8_t byteCount = buffer[2]; // Número de bytes de dados (2 bytes por registrador)
        Debug.print("Dados (Registradores): ");
        for (uint8_t i = 0; i < byteCount; i += 2) {
          uint16_t value = (buffer[3 + i] << 8) | buffer[4 + i]; // Combina bytes alto e baixo
          Debug.printf("Reg[%d]: %d ", (READ_REG_ADDR - 40001) + (i/2), value);
        }
        Debug.println();
      }
      // --- Processamento da Resposta de Escrita (Função 0x06) ---
      else if (buffer[0] == expectedId && buffer[1] == expectedFunction && expectedFunction == 0x06) {
        uint16_t regAddr = (buffer[2] << 8) | buffer[3];
        uint16_t value = (buffer[4] << 8) | buffer[5];
        Debug.printf("Escrita confirmada no registrador %d com valor %d.\n", regAddr, value);
      }
      // --- Tratamento de Exceções Modbus ---
      else if (buffer[0] == expectedId && (buffer[1] & 0x80)) { // Bit 7 da função setado indica exceção
        Debug.printf("Excecao Modbus! Codigo: 0x%X\n", buffer[2]);
      }
    }
    else {
      Debug.printf("CRC inválido (calculado %04X, recebido %04X)\n", crcCalc, crcRecv);
    }
  }
  else {
    Debug.println("Resposta muito curta para conter CRC.");
  }
  yield(); // Permite que outras tarefas do ESP8266 sejam executadas.
}

// --- Setup: Configurações Iniciais ---
void setup() {
  // Inicia a comunicação serial para depuração (SoftwareSerial) e para Modbus (HardwareSerial).
  Serial.begin(RS485_BAUD); 
  Debug.begin(9600); // Taxa de baud para o monitor serial de depuração.

  // Configura o pino DE_RE como saída para controlar a direção do transceptor RS485.
  pinMode(DE_RE, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(RST_PIN, INPUT_PULLUP);

  digitalWrite(DE_RE, LOW); // Inicializa em modo de recepção (RX).
  digitalWrite(13, LOW);

  while(digitalRead(RST_PIN)) yield();
  delay(2000); // Pequena pausa para estabilização.
  Debug.println("\n=== Mestre Modbus RTU Manual ===");

  // --- Escrita Única no Setup ---
  // Esta seção garante que o valor 200 seja escrito no registrador 40301
  // de ambos os dispositivos escravos (ID 2 e ID 3) apenas uma vez.
  Debug.println("Iniciando escrita unica nos dispositivos...");

  // Escreve no Escravo 1
  Debug.printf("Escrevendo %d no registrador %d do Escravo ID %d...\n", WRITE_VALUE, WRITE_REG_ADDR, SLAVE_ID_1);
  sendWriteRequest(SLAVE_ID_1, WRITE_REG_ADDR, WRITE_VALUE);
  receiveResponse(SLAVE_ID_1, 0x06); // Espera resposta de escrita para o Escravo 1
  delay(100); // Pequeno atraso entre as operações para evitar colisões

  // Escreve no Escravo 2
  Debug.printf("Escrevendo %d no registrador %d do Escravo ID %d...\n", WRITE_VALUE, WRITE_REG_ADDR, SLAVE_ID_2);
  sendWriteRequest(SLAVE_ID_2, WRITE_REG_ADDR, WRITE_VALUE);
  receiveResponse(SLAVE_ID_2, 0x06); // Espera resposta de escrita para o Escravo 2
  delay(100); // Pequeno atraso

  WRITE_REG_ADDR = 0x9D6C;
  READ_REG_ADDR = 0x9D6C;
  WRITE_VALUE = 200;

    // Escreve no Escravo 1
  Debug.printf("Escrevendo %d no registrador %d do Escravo ID %d...\n", WRITE_VALUE, WRITE_REG_ADDR, SLAVE_ID_1);
  sendWriteRequest(SLAVE_ID_1, WRITE_REG_ADDR, WRITE_VALUE);
  receiveResponse(SLAVE_ID_1, 0x06); // Espera resposta de escrita para o Escravo 1
  delay(100); // Pequeno atraso entre as operações para evitar colisões

  // Escreve no Escravo 2
  Debug.printf("Escrevendo %d no registrador %d do Escravo ID %d...\n", WRITE_VALUE, WRITE_REG_ADDR, SLAVE_ID_2);
  sendWriteRequest(SLAVE_ID_2, WRITE_REG_ADDR, WRITE_VALUE);
  receiveResponse(SLAVE_ID_2, 0x06); // Espera resposta de escrita para o Escravo 2
  delay(100); // Pequeno atraso

  Debug.println("Escritas unicas concluidas.");
  initialWriteDone = true; // Marca que a escrita inicial foi concluída.
  Debug.println("--------------------------------------------------");
}

// --- Loop Principal: Leitura Contínua ---
void loop() {
  // Garante que a escrita inicial foi feita antes de começar as leituras contínuas.
  if (!initialWriteDone) {
    delay(100); // Espera um pouco se a escrita ainda não terminou (não deve acontecer se o setup for síncrono)
    return;
  }

  static unsigned long lastReadTime = 0; // Armazena o tempo da última leitura.
  static uint8_t currentSlaveIndex = 0; // 0 para SLAVE_ID_1, 1 para SLAVE_ID_2.
  uint8_t currentSlaveID; // ID do escravo atual a ser lido.

  // Realiza leituras a cada 1000ms.
  if (millis() - lastReadTime >= 1000) {
    lastReadTime = millis(); // Atualiza o tempo da última leitura.

    // Alterna entre os dois escravos.
    if (currentSlaveIndex == 0) {
      currentSlaveID = SLAVE_ID_1;
      currentSlaveIndex = 1;
    } else {
      currentSlaveID = SLAVE_ID_2;
      currentSlaveIndex = 0;
    }

    Debug.printf("Enviando solicitacao de leitura para Escravo ID %d...\n", currentSlaveID);
    sendReadRequest(currentSlaveID, READ_REG_ADDR, READ_REG_COUNT);
    receiveResponse(currentSlaveID, 0x03); // Espera resposta de leitura para o escravo atual.
    
    Debug.println("-----------------------");
  }
  yield(); // Permite que outras tarefas do ESP8266 sejam executadas.
}


