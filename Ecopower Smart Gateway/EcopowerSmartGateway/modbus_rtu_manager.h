#ifndef MODBUS_RTU_MANAGER_H
#define MODBUS_RTU_MANAGER_H

#include <ModbusRTU.h>
#include "config.h"

// =============================================================================
// GERENCIADOR DE MODBUS RTU
// =============================================================================

class ModbusRTUManager {
private:
    ModbusRTU mb;

public:
    // Inicializar Modbus RTU
    void begin() {
        // Inicializa a Serial (HardwareSerial) para o RS485
        // O ESP8266 usa a Serial0 (GPIO1/GPIO3) para comunicação.
        // O pino de controle de direção (DE/RE) é o GPIO12.
        mb.setBaudrate(MODBUS_RTU_BAUDRATE);
        mb.begin(&Serial, RS485_TX_ENABLE_PIN);

        // Adicionar registradores (os mesmos do TCP)
        mb.addHreg(REG_START_ADDRESS, holdingRegister, REG_COUNT);
        mb.addCoil(0);
        mb.addIsts(0);
        mb.addIreg(0);

        // Se for Mestre, ele é o Cliente RTU (lê do dispositivo)
        if (gatewayConfig.mode == MODE_MASTER) {
            mb.client();
            Debug.println("Modbus RTU Client (Master) iniciado.");
        } else {
            // Se for Escravo, ele é o Servidor RTU (responde ao dispositivo)
            mb.server(MODBUS_RTU_SLAVE_ID);
            Debug.print("Modbus RTU Server (Slave ID: ");
            Debug.print(MODBUS_RTU_SLAVE_ID);
            Debug.println(") iniciado.");
        }
    }

    // Processar Modbus RTU (deve ser chamado no loop)
    void task() {
        mb.task();
    }

    // Ler Holding Registers remotos (apenas para Mestre)
    // Retorna o código de transação (SUCCESS, TIMEOUT, etc.)
    uint16_t readHoldingRegisters(uint8_t slaveId, uint16_t address, uint16_t count, uint16_t* buffer) {
        if (gatewayConfig.mode == MODE_SLAVE) {
            return 0x01;
            // Function Code not Supported;
            // EX_ILLEGAL_FUNCTION     = 0x01 
            // Não é função de Slave
        }
        // A função readHreg da biblioteca ModbusRTU (emelianov) é não-bloqueante
        // e retorna o ID da transação. Para simplificar, usaremos a versão
        // que preenche o buffer local e retorna o resultado da transação.
        // Nota: A versão mais recente da biblioteca usa callbacks para transações.
        // Para um sketch simples, usaremos a função de leitura que preenche o array local.
        // O valor lido será armazenado em holdingRegister[0]
        return mb.readHreg(slaveId, address, buffer, count);
    }

    // Obter Holding Register local (para o modo Slave)
    uint16_t getLocalHreg(uint16_t address) {
        return mb.Hreg(address);
    }

    // Definir Holding Register local (para o modo Slave)
    void setLocalHreg(uint16_t address, uint16_t value) {
        mb.Hreg(address, value);
    }
};

#endif // MODBUS_RTU_MANAGER_H
