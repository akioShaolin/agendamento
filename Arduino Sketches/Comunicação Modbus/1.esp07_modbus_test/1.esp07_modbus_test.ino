#include <ModbusRTU.h>

// --- Configurações dos pinos ---
// Mapeamento de pinos do NodeMCU ESP8266 (para referência)
// D0 = GPIO16
// D1 = GPIO5
// D2 = GPIO4
// D3 = GPIO0
// D4 = GPIO2
// D5 = GPIO14
// D6 = GPIO12
// D7 = GPIO13
// D8 = GPIO15
// RX = GPIO3
// TX = GPIO1

#define SLAVE_ID 1       // ID do dispositivo escravo que queremos ler
#define REG_ADDR 40301     // Endereço do primeiro registrador a ser lido
#define REG_COUNT 10      // Quantidade de registradores a ler
#define CTRL_PIN 12      // Pino do ESP8266 conectado ao DE/RE do 75176B (GPIO12)

ModbusRTU mb; // Cria o objeto ModbusRTU

// Buffer para armazenar os registradores lidos
uint16_t holdingRegisters[REG_COUNT];

// Callback para tratar as respostas Modbus
// Esta função será chamada pela biblioteca quando uma resposta for recebida ou um erro ocorrer
// A assinatura correta para o callback é bool (*)(Modbus::ResultCode, uint16_t, void*)
// Baseado no exemplo master.ino da biblioteca
bool read_cb(Modbus::ResultCode event, uint16_t transactionId, void* data) {
  if (event == Modbus::EX_SUCCESS) {
    Serial.printf("Resposta recebida! ID da Transação: %d, Sucesso.\n", transactionId);
    // Os dados já foram escritos no array 'holdingRegisters' pela função readHreg
    for (int i = 0; i < REG_COUNT; i++) {
      Serial.printf("  - Registrador %d: %d\n", REG_ADDR + i, holdingRegisters[i]);
    }
  } else {
    Serial.printf("Resposta recebida! ID da Transação: %d, Erro: 0x%X\n", transactionId, event);
  }
  Serial.println("--------------------");
  return true; // Retorna true para que a biblioteca continue processando
}

void setup() {
  Serial.begin(9600); // Inicia a serial para debug no monitor

  delay(2000);

  Serial.println("Iniciando Mestre Modbus RTU...");

  // Inicia a comunicação Modbus na porta Serial principal (TX/RX)
  // A 9600 bps, 8 bits de dados, sem paridade, 1 stop bit (padrão)
  // E informa que o pino CTRL_PIN será usado para controle de direção (DE/RE)
  mb.begin(&Serial, CTRL_PIN); 
  
  // Define o ESP8266 como mestre
  mb.master();
}

void loop() {
  // A função task() deve ser chamada em cada ciclo do loop.
  // Ela gerencia o estado da comunicação, envia solicitações na fila e processa respostas.
  mb.task();

  // Envia uma solicitação de leitura a cada 5 segundos, apenas se não houver uma transação pendente
  static unsigned long lastRequestTime = 0;
  // A função isTransaction() retorna true se há uma transação em andamento
  if (millis() - lastRequestTime > 5000 && !mb.isTransaction()) {
    lastRequestTime = millis();c:\Users\pedro.sakuma\Downloads\modbus-esp8266-master.zip
    
    Serial.println("Enviando solicitação de leitura...");
    
    // Coloca uma solicitação de leitura na fila.
    // mb.readHreg(ID_ESCRAVO, ENDEREÇO_INICIAL, PONTEIRO_PARA_ARRAY_DE_DADOS, QUANTIDADE_DE_REGISTRADORES, CALLBACK_DA_RESPOSTA);
    // A função retorna um ID de transação, que pode ser usado para rastrear a solicitação.
    uint16_t transactionId = mb.readHreg(SLAVE_ID, REG_ADDR, holdingRegisters, REG_COUNT, read_cb);

    if (transactionId == 0) {
      Serial.println("Falha ao enviar solicitação. A fila pode estar cheia ou parâmetros inválidos.");
    } else {
      Serial.printf("Solicitação enviada com ID de Transação: %d\n", transactionId);
    }
  }
}


