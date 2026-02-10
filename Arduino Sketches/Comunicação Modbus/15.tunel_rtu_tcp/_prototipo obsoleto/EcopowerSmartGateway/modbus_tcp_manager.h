#ifndef MODBUS_TCP_MANAGER_H
#define MODBUS_TCP_MANAGER_H

#include <ModbusTCP.h>
#include <ESP8266WiFi.h>
#include "config.h"

// =============================================================================
// GERENCIADOR DE MODBUS TCP
// =============================================================================

class ModbusTCPManager {
private:
    ModbusTCP mb;
    IPAddress remoteIP;
    bool connected;
    unsigned long lastConnectionAttempt;
    const unsigned long CONNECTION_RETRY_INTERVAL = 5000; // 5 segundos

public:
    ModbusTCPManager() : connected(false), lastConnectionAttempt(0) {}

    // Inicializar Modbus TCP
    void begin() {
        if (gatewayConfig.mode == MODE_SLAVE) {
            // Se for Escravo, atua como Servidor TCP
            mb.server();
            Debug.println("Modbus TCP Server iniciado");
        } else {
            // Se for Mestre, atua como Cliente TCP
            mb.client();
            Debug.println("Modbus TCP Client iniciado");
        }

        // Adicionar registradores
        mb.addHreg(REG_START_ADDRESS, holdingRegister, REG_COUNT);
        mb.addCoil(0);
        mb.addIsts(0);
        mb.addIreg(0);
    }

    // Processar Modbus TCP (deve ser chamado no loop)
    void task() {
        mb.task();
    }

    // Conectar ao servidor Modbus TCP remoto (apenas para Cliente)
    bool connect(const char* ip) {
        remoteIP.fromString(ip);
        if (gatewayConfig.mode == MODE_SLAVE) {
            // Escravo não precisa conectar
            return true;
        }

        // Verificar se já está conectado
        if (connected && mb.isConnected(remoteIP)) {
            return true;
        }

        // Tentar conectar periodicamente
        unsigned long now = millis();
        if (now - lastConnectionAttempt < CONNECTION_RETRY_INTERVAL) {
            return false;
        }

        lastConnectionAttempt = now;

        // Converter string IP para IPAddress
        if (!remoteIP.fromString(ip)) {
            Debug.print("IP inválido: ");
            Debug.println(ip);
            return false;
        }

        Debug.print("Conectando ao servidor Modbus TCP em ");
        Debug.print(ip);
        Debug.println("...");

        if (mb.connect(remoteIP, MODBUS_TCP_PORT)) {
            Debug.println("Conectado com sucesso!");
            connected = true;
            return true;
        } else {
            Debug.println("Falha ao conectar");
            connected = false;
            return false;
        }
    }

    // Desconectar do servidor Modbus TCP
    void disconnect() {
        if (connected) {
            mb.disconnect(remoteIP);
            connected = false;
            Debug.println("Desconectado do servidor Modbus TCP");
        }
    }

    // Ler Holding Registers remotos (apenas para Cliente)
    bool readHoldingRegisters(uint16_t address, uint16_t count, uint16_t* buffer) {
        if (gatewayConfig.mode == MODE_SLAVE || !connected) {
            return false;
        }

        uint16_t result = mb.readHreg(remoteIP, address, buffer, count);
        return result == 0x00;
        // mb.transactionResult::SUCCESS;
        // EX_SUCCESS              = 0x00, // Custom. No error
    }

    // Escrever Holding Registers remotos (apenas para Cliente)
    bool writeHoldingRegisters(uint16_t address, uint16_t count, uint16_t* buffer) {
        if (gatewayConfig.mode == MODE_SLAVE || !connected) {
            return false;
        }

        uint16_t result = mb.writeHreg(remoteIP, address, buffer, count);
        return result == 0x00;
        // mb.transactionResult::SUCCESS;
        // EX_SUCCESS 
    }

    // Obter Holding Register local
    uint16_t getLocalHreg(uint16_t address) {
        return mb.Hreg(address);
    }

    // Definir Holding Register local
    void setLocalHreg(uint16_t address, uint16_t value) {
        mb.Hreg(address, value);
    }

    // Verificar se está conectado
    bool isConnected() {
        return connected && mb.isConnected(remoteIP);
    }

    // Obter status de conexão
    String getConnectionStatus() {
        if (gatewayConfig.mode == MODE_SLAVE) {
            return "Servidor (aguardando conexões)";
        } else if (isConnected()) {
            return "Cliente (conectado a " + remoteIP.toString() + ")";
        } else {
            return "Cliente (desconectado)";
        }
    }

    // Callback para quando um cliente se conecta (apenas para Servidor)
    void onConnect(bool (*callback)(IPAddress ip)) {
        // Nota: A biblioteca ModbusTCP pode não suportar callbacks de conexão
        // Esta função é um placeholder para futuras implementações
    }

    // Callback para quando um cliente se desconecta (apenas para Servidor)
    void onDisconnect(bool (*callback)(IPAddress ip)) {
        // Nota: A biblioteca ModbusTCP pode não suportar callbacks de desconexão
        // Esta função é um placeholder para futuras implementações
    }
};

#endif // MODBUS_TCP_MANAGER_H
