#include <ESP8266WiFi.h>
#include <WiFiManager.h> // Para gerenciamento de credenciais WiFi (do tzapu/WiFiManager)
#include <ModbusRTU.h>   // Para comunicação RS485 (do emelianov/modbus-esp8266)
#include <ModbusTCP.h>   // Para comunicação WiFi (do emelianov/modbus-esp8266)
#include <EEPROM.h>      // Para salvar as configurações
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>

#define WM_DEBUG_PORT Debug

#include "config.h"
#include "webserver.h"
#include "wifi_manager.h"
#include "modbus_rtu_manager.h"
#include "modbus_tcp_manager.h"

// =============================================================================
// VARIÁVEIS GLOBAIS
// =============================================================================

// Instâncias dos Managers
ESP8266WebServer server(80);
WebServerManager webManager(&server);
WiFiLocalManager wifiManager;
ModbusRTUManager rtuManager;
ModbusTCPManager tcpManager;

const char* ssid = "VISITANTES";
const char* password = "connection";

IPAddress ip(172, 16, 99, 101);
IPAddress gw(172, 16, 99, 1);
IPAddress sn(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);
// =============================================================================
// FUNÇÕES DE UTILIDADE (MANTIDAS PARA EEPROM)
// =============================================================================

// Função para carregar as configurações da EEPROM
void loadConfig() {
    EEPROM.begin(sizeof(Config));
    EEPROM.get(0, gatewayConfig);
    EEPROM.end();

    // Validação básica para garantir que a EEPROM não está vazia
    if (gatewayConfig.mode != MODE_MASTER && gatewayConfig.mode != MODE_SLAVE) {
        // Valores padrão se a EEPROM estiver vazia ou corrompida
        gatewayConfig.mode = MODE_MASTER;
        strcpy(gatewayConfig.token, "default_token_12345678901234567");
        strcpy(gatewayConfig.targetIp, "172.16.99.100");
        saveConfig();
    }
}

// Função para salvar as configurações na EEPROM
void saveConfig() {
    EEPROM.begin(sizeof(GatewayConfig));
    EEPROM.put(0, gatewayConfig);
    EEPROM.commit();
    EEPROM.end();
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(MODBUS_RTU_BAUDRATE);
    Debug.begin(115200);
    Debug.println("\nIniciando Ecopower Smart Gateway...");

    // 1. Carregar Configurações
    loadConfig();

    // 2. Configurar WiFi (usando WiFiManager para credenciais)
    //WiFiManager wm;
    //wm.autoConnect("EcopowerGatewayAP", "ecopower123");
    WiFi.mode(WIFI_STA);
    WiFi.config(ip, gw, sn, dns);
    WiFi.begin(ssid, password);

    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Connection Failed! Rebooting...");
        delay(5000);
        ESP.restart();
    }
    // 3. Configurar mDNS
    wifiManager.beginMDNS();

    // 4. Configurar Modbus RTU
    rtuManager.begin();

    // 5. Configurar Modbus TCP
    tcpManager.begin();

    // 6. Configurar Servidor Web
    webManager.begin();

    // 7. Configura o hardware para HalfDuplex
    pinMode(RS485_HALF_OR_FULL, OUTPUT);
    digitalWrite(RS485_HALF_OR_FULL, LOW);
    
    gatewayConfig.mode = MODE_MASTER;
    strcpy(gatewayConfig.token, "default_token_12345678901234567");
    strcpy(gatewayConfig.targetIp, "172.16.99.100");
    saveConfig();

    Debug.println("Configuração inicial concluída.");
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
    // 1. Lidar com o Servidor Web
    webManager.handleClient();

    // 2. Lidar com o mDNS
    wifiManager.update();

    // 3. Lidar com o Modbus RTU
    rtuManager.task();

    // 4. Lidar com o Modbus TCP
    tcpManager.task();

    // 5. Lógica de Gateway
    if (WiFi.status() == WL_CONNECTED) {
        if (gatewayConfig.mode == MODE_MASTER) {
            // MESTRE (RS485 Master -> TCP Client)
            // 5.1. Tenta conectar ao Gateway Escravo (TCP Server)
            if (tcpManager.connect(gatewayConfig.targetIp)) {
                // 5.2. Se conectado, lê do RTU Slave (dispositivo) e envia para o TCP Server (outro Gateway)
                
                // O array holdingRegister[0] é compartilhado entre RTU e TCP
                // A leitura RTU preenche o array local
                
                uint16_t result = rtuManager.readHoldingRegisters(MODBUS_RTU_SLAVE_ID, REG_START_ADDRESS, REG_COUNT, &holdingRegister);

                if (result == 0x00) {
                    // if (result == Modbus::transactionResult::SUCCESS) {
                    // Se a leitura RTU foi bem-sucedida, o valor está em holdingRegister[0]
                    // Agora, escreve esse valor no Modbus TCP Server (Gateway Escravo)
                    // Nota: A função writeHoldingRegisters no manager TCP usa o array local
                    tcpManager.writeHoldingRegisters(REG_START_ADDRESS, REG_COUNT, &holdingRegister);
                }
            }
        } else {
            // ESCRAVO (TCP Server -> RTU Slave)
            // 5.1. O Gateway Escravo (TCP Server) recebe dados do Mestre (TCP Client)
            // O valor recebido via TCP é escrito em holdingRegister[0] (pela mbTCP.task())

            // 5.2. O Gateway Escravo (RTU Slave) responde às requisições do RTU Master (dispositivo)
            // O valor em holdingRegister[0] é o que será retornado ao RTU Master.
            // A lógica de resposta é tratada automaticamente por rtuManager.task()
            // Nenhuma ação adicional é necessária aqui.
        }
    }
    
    // Pequeno delay para evitar sobrecarga
    delay(10);
}
