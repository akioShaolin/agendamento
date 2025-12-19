#include <ESP8266WiFi.h>
#include <WiFiManager.h> // Para gerenciamento de credenciais WiFi (do tzapu/WiFiManager)
#include <ModbusRTU.h>   // Para comunicação RS485 (do emelianov/modbus-esp8266)
#include <ModbusTCP.h>   // Para comunicação WiFi (do emelianov/modbus-esp8266)
#include <EEPROM.h>      // Para salvar as configurações
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>

#include "config.h"
#include "webserver.h"
#include "wifi_manager.h"
#include "Modbus.h"

// =============================================================================
// VARIÁVEIS GLOBAIS
// =============================================================================

// Instâncias dos Managers
ESP8266WebServer server(80);
WebServerManager webManager(&server);
WiFiLocalManager wifiManager;

// Objetos Modbus
ModbusRTU mbRTU;
ModbusTCP mbTCP;

// Buffer para a ponte transparente
const int BUFFER_SIZE = 256;
uint8_t rtuBuffer[BUFFER_SIZE];
int rtuBufferLen = 0;
uint8_t tcpBuffer[BUFFER_SIZE];
int tcpBufferLen = 0;

// =============================================================================
// FUNÇÕES DE UTILIDADE (MANTIDAS PARA EEPROM)
// =============================================================================

// Função para carregar as configurações da EEPROM
void loadConfig() {
    EEPROM.begin(sizeof(GatewayConfig));
    EEPROM.get(0, gatewayConfig);
    EEPROM.end();

    // Validação básica para garantir que a EEPROM não está vazia
    if (gatewayConfig.mode != MODE_MASTER && gatewayConfig.mode != MODE_SLAVE) {
        // Valores padrão se a EEPROM estiver vazia ou corrompida
        gatewayConfig.mode = MODE_MASTER;
        strcpy(gatewayConfig.token, "default_token_12345678901234567890");
        strcpy(gatewayConfig.targetIp, "172.16.99.1");
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
// CALLBACKS MODBUS (PARA A LÓGICA DE BRIDGE)
// =============================================================================

// Callback para quando o Modbus RTU recebe um frame
// Este frame será encaminhado para o Modbus TCP
void onRtuFrame(uint8_t* frame, uint8_t len) {
    if (len > 0 && len < BUFFER_SIZE) {
        memcpy(rtuBuffer, frame, len);
        rtuBufferLen = len;
        Debug.println("RTU Frame recebido, encaminhando para TCP...");
    }
}

// Callback para quando o Modbus TCP recebe um frame
// Este frame será encaminhado para o Modbus RTU
void onTcpFrame(uint8_t* frame, uint8_t len) {
    if (len > 0 && len < BUFFER_SIZE) {
        memcpy(tcpBuffer, frame, len);
        tcpBufferLen = len;
        Debug.println("TCP Frame recebido, encaminhando para RTU...");
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(MODBUS_RTU_BAUDRATE);
    Debug.begin(115200);
    Debug.println("\nIniciando Ecopower Smart Gateway (Bridge Mode)...");

    // 1. Carregar Configurações
    loadConfig();

    // 2. Configurar WiFi (usando WiFiManager para credenciais)
    WiFiManager wm;
    wm.autoConnect("EcopowerGatewayAP", "ecopower123");

    // 3. Configurar mDNS
    wifiManager.beginMDNS();

    // 4. Configurar Modbus RTU
    mbRTU.begin(&Serial, RS485_TX_ENABLE_PIN);
    mbRTU.setBaudrate(MODBUS_RTU_BAUDRATE);
    mbRTU.onRaw(onRtuFrame); // Callback para o frame RTU

    // 5. Configurar Modbus TCP
    mbTCP.server(); // Ambos os Gateways atuam como servidores TCP
    mbTCP.onRaw(onTcpFrame); // Callback para o frame TCP

    // 6. Configurar Servidor Web
    webManager.begin();

    // 7. Configura o hardware para HalfDuplex
    pinMode(RS485_HALF_OR_FULL, OUTPUT);
    digitalWrite(RS485_HALF_OR_FULL, LOW);

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

    // 3. Lidar com o Modbus RTU e TCP
    mbRTU.task();
    mbTCP.task();

    // 4. Lógica de Bridge Transparente
    if (WiFi.status() == WL_CONNECTED) {
        // 4.1. Encaminhar de RTU para TCP
        if (rtuBufferLen > 0) {
            IPAddress targetIP;
            if (targetIP.fromString(gatewayConfig.targetIp)) {
                mbTCP.send(targetIP, rtuBuffer, rtuBufferLen);
                Debug.println("RTU Frame enviado para TCP.");
            }
            rtuBufferLen = 0; // Limpa o buffer
        }

        // 4.2. Encaminhar de TCP para RTU
        if (tcpBufferLen > 0) {
            mbRTU.send(tcpBuffer, tcpBufferLen);
            Debug.println("TCP Frame enviado para RTU.");
            tcpBufferLen = 0; // Limpa o buffer
        }
    }

    // Pequeno delay para evitar sobrecarga
    delay(10);
}
