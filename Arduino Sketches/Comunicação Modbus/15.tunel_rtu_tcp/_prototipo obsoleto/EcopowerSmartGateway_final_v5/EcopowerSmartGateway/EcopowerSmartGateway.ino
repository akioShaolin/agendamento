#include <ESP8266WiFi.h>
#include <WiFiManager.h> // Por tzapu/WiFiManager
#include <ModbusRTU.h>   // Por emelianov/modbus-esp8266
#include <ModbusTCP.h>   // Por emelianov/modbus-esp8266
#include <EEPROM.h>      // Para salvar as configurações
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>

// Inclui os arquivos de configuração e webserver (assumindo que estão na mesma pasta)
#include "config.h"
#include "webserver.h"

// =============================================================================
// VARIÁVEIS GLOBAIS
// =============================================================================

// Instâncias dos Managers
ESP8266WebServer server(80);
WebServerManager webManager(&server);

// Objetos Modbus
ModbusRTU mbRTU;
ModbusTCP mbTCP;

// =============================================================================
// FUNÇÕES DE UTILIDADE (EEPROM)
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
        strcpy(gatewayConfig.token, "default_token_12345678901234567");
        strcpy(gatewayConfig.targetIp, "172.16.99.1");
        // Não chama saveConfig aqui para evitar loop infinito, mas a EEPROM será inicializada
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
    // Inicializa as Seriais
    Serial.begin(MODBUS_RTU_BAUDRATE);
    Debug.begin(115200);
    Debug.println("\nIniciando Ecopower Smart Gateway (Register Mode)...");

    // 1. Carregar Configurações
    loadConfig();

    // 2. Configurar WiFi (usando WiFiManager para credenciais)
    WiFiManager wm;
    wm.autoConnect("EcopowerGatewayAP", "ecopower123");

    // 3. Configurar mDNS
    if (MDNS.begin("ecopower-gateway")) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("modbus-tcp", "tcp", MODBUS_TCP_PORT);
        Debug.println("mDNS iniciado: ecopower-gateway.local");
    }

    // 4. Configurar Modbus RTU
    mbRTU.begin(&Serial, RS485_TX_ENABLE_PIN);
    mbRTU.setBaudrate(MODBUS_RTU_BAUDRATE);
    
    // 5. Configurar Modbus TCP
    mbTCP.server(); // Atua como servidor TCP para receber requisições

    // 6. Configurar Registradores Modbus (Compartilhados)
    // O Modbus RTU Slave (se for o caso) precisa ter o registrador definido.
    // O Modbus TCP Server precisa ter o registrador definido.
    // Usamos o mesmo endereço de memória para que a atualização seja automática.
    mbRTU.addHreg(REG_START_ADDRESS, holdingRegister, REG_COUNT);
    mbTCP.addHreg(REG_START_ADDRESS, holdingRegister, REG_COUNT);

    // 7. Configura o hardware para HalfDuplex
    pinMode(RS485_HALF_OR_FULL, OUTPUT);
    digitalWrite(RS485_HALF_OR_FULL, LOW); // Ativa Half Duplex

    // 8. Configurar Servidor Web
    webManager.begin();

    Debug.println("Configuração inicial concluída.");
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
    // 1. Lidar com o Servidor Web
    webManager.handleClient();

    // 2. Lidar com o mDNS
    MDNS.update();

    // 3. Lidar com o Modbus RTU e TCP
    mbRTU.task();
    mbTCP.task();

    // 4. Lógica de Gateway (Manipulação de Registradores)
    if (WiFi.status() == WL_CONNECTED) {
        if (gatewayConfig.mode == MODE_MASTER) {
            // MESTRE (RS485 Master -> TCP Client)
            // O Gateway Mestre (TCP Client) tenta ler o registrador do Escravo RTU
            // e o valor lido é armazenado em 'holdingRegister'.
            
            // Tenta conectar ao Gateway Escravo (TCP Server)
            IPAddress targetIP;
            if (targetIP.fromString(gatewayConfig.targetIp)) {
                if (mbTCP.isConnected(targetIP)) {
                    // Se conectado, o Mestre TCP lê o registrador do Escravo TCP
                    // O valor lido é armazenado em 'holdingRegister' (que é o mesmo para RTU e TCP)
                    // mbTCP.readHreg(targetIP, REG_START_ADDRESS, &holdingRegister, REG_COUNT);
                    
                    // Lógica de Gateway:
                    // 1. O Gateway Mestre (RTU Master) lê o registrador do dispositivo RTU Slave (ID 1)
                    uint16_t result = mbRTU.readHreg(MODBUS_RTU_SLAVE_ID, REG_START_ADDRESS, &holdingRegister, REG_COUNT);

                    if (result == Modbus::ResultCode::EX_SUCCESS) {
                        // 2. Se a leitura RTU foi bem-sucedida, o valor está em 'holdingRegister'.
                        // 3. O Gateway Mestre (TCP Client) escreve esse valor no Gateway Escravo (TCP Server)
                        mbTCP.writeHreg(targetIP, REG_START_ADDRESS, holdingRegister);
                    }
                } else {
                    // Tenta conectar
                    mbTCP.connect(targetIP);
                }
            }
        } else {
            // ESCRAVO (TCP Server -> RTU Slave)
            // 1. O Gateway Escravo (TCP Server) recebe dados do Mestre (TCP Client).
            // O valor recebido via TCP é escrito em 'holdingRegister' (pela mbTCP.task()).
            
            // 2. O Gateway Escravo (RTU Slave) responde às requisições do RTU Master (dispositivo)
            // usando o valor atualizado em 'holdingRegister' (pela mbRTU.task()).
            // Nenhuma ação adicional é necessária aqui, pois a atualização é automática.
        }
    }
    
    // Pequeno delay para evitar sobrecarga
    delay(1);
}
