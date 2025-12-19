#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// 1. PINAGEM DO HARDWARE (ESP07)
// =============================================================================

// Pinos da Serial (HardwareSerial) para comunicação RS485
// TX (GPIO1) e RX (GPIO3) são os pinos padrão da Serial0 do ESP8266
// O ESP8266 usa a Serial0 para upload/debug, mas pode ser reconfigurada
// para uso com RS485, desde que o debug seja feito via GPIO14 (Debug)
// ou que o upload seja feito com o RS485 desconectado.
// Para o ESP07, o HardwareSerial é a Serial.

// Pino de controle de direção (DE/RE) para o transceptor RS485
const int RS485_TX_ENABLE_PIN = 12; // GPIO12
const int RS485_HALF_OR_FULL = 13; //GPIO13 

// =============================================================================
// 2. CONFIGURAÇÕES DE COMUNICAÇÃO
// =============================================================================

// Baudrate para a comunicação MODBUS RTU (RS485)
const long MODBUS_RTU_BAUDRATE = 9600; // Valor comum, pode ser ajustado

// ID do Slave Modbus RTU (para quando o Gateway for Slave)
const uint8_t MODBUS_RTU_SLAVE_ID = 1;

// Porta padrão para o Modbus TCP
const uint16_t MODBUS_TCP_PORT = 502;

// =============================================================================
// 3. ESTRUTURA DE DADOS MODBUS
// =============================================================================

// Endereços dos Registradores de Retenção (Holding Registers)
// Usaremos um único registrador para demonstração da comunicação.
// Este registrador será lido/escrito via RTU e replicado via TCP.
const uint16_t REG_START_ADDRESS = 0;
const uint16_t REG_COUNT = 1;

// Variável para armazenar o valor do registrador
// uint16_t holdingRegister[REG_COUNT];

//Precisei alterar o tipo de variável, pois um vetor é interpretado como ponteiro na hora de compilar
uint16_t holdingRegister;
// =============================================================================
// 4. CONFIGURAÇÕES DO GATEWAY (Persistentes)
// =============================================================================

// Enum para definir o modo de operação
enum GatewayMode {
    MODE_MASTER,
    MODE_SLAVE
};

// Estrutura para armazenar as configurações persistentes (salvas na EEPROM/Flash)
struct GatewayConfig {
    GatewayMode mode;
    char token[33]; // Token de segurança (32 caracteres + null terminator)
    char targetIp[16]; // Endereço IP do outro Gateway (ex: "192.168.1.100")
};

// Variável global para as configurações
GatewayConfig gatewayConfig;

// =============================================================================
// 5. VARIÁVEIS DE ESTADO
// =============================================================================

// Flag para indicar o modo de configuração (Web Server)
bool configMode = false;
// =============================================================================
// 6. CONFIGURAÇÃO DO SoftwareSerial
// =============================================================================

// A SoftwareSerial Debug é usada para imprimir mensagens de depuração sem interferir na Serial principal
// que será usada para o Modbus RTU.
// No ESP8266, os pinos 2 (RX) e 14 (TX) são GPIOs comuns para SoftwareSerial.
SoftwareSerial Debug(2, 14); 

#endif // CONFIG_H
