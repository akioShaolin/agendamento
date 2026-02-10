#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <SoftwareSerial.h>

// =============================================================================
// 1. PINAGEM DO HARDWARE (ESP07)
// =============================================================================

// Pino de controle de direção (DE/RE) para o transceptor RS485
const int RS485_TX_ENABLE_PIN = 12; // GPIO12

// Pino para Half Duplex (nível baixo ativa)
const int RS485_HALF_OR_FULL = 13; // GPIO13

// =============================================================================
// 2. CONFIGURAÇÕES DE COMUNICAÇÃO
// =============================================================================

// Baudrate para a comunicação MODBUS RTU (RS485)
const long MODBUS_RTU_BAUDRATE = 9600;

// ID do Slave Modbus RTU (para quando o Gateway for Slave)
const uint8_t MODBUS_RTU_SLAVE_ID = 1;

// Porta padrão para o Modbus TCP
const uint16_t MODBUS_TCP_PORT = 502;

// =============================================================================
// 3. ESTRUTURA DE DADOS MODBUS
// =============================================================================

// Endereços dos Registradores de Retenção (Holding Registers)
const uint16_t REG_START_ADDRESS = 0;
const uint16_t REG_COUNT = 1;

// Variável para armazenar o valor do registrador (compartilhada entre RTU e TCP)
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
// 5. CONFIGURAÇÃO DO SoftwareSerial (Debug)
// =============================================================================

// A SoftwareSerial Debug é usada para imprimir mensagens de depuração sem interferir na Serial principal
// RX (GPIO2) e TX (GPIO14)
SoftwareSerial Debug(2, 14); 

// =============================================================================
// 6. PROTÓTIPOS DE FUNÇÕES (Para evitar erros de escopo)
// =============================================================================

void loadConfig();
void saveConfig();

#endif // CONFIG_H
