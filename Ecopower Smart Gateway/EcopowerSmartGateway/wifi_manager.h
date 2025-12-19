#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include "config.h"

// =============================================================================
// GERENCIADOR DE WIFI E DESCOBERTA DE DISPOSITIVOS
// =============================================================================

class WiFiLocalManager {
private:
    WiFiUDP udp;
    const char* mdnsName = "ecopower-gateway";
    const char* mdnsService = "modbus-tcp";
    const char* mdnsProto = "tcp";
    const uint16_t mdnsPort = MODBUS_TCP_PORT;

public:
    // Inicializar WiFi
    bool begin(const char* ssid, const char* password) {
        Debug.println("Conectando ao WiFi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Debug.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Debug.println("\nWiFi conectado!");
            Debug.print("IP: ");
            Debug.println(WiFi.localIP());
            Debug.print("RSSI: ");
            Debug.println(WiFi.RSSI());
            return true;
        } else {
            Debug.println("\nFalha ao conectar ao WiFi");
            return false;
        }
    }

    // Inicializar mDNS para descoberta de dispositivos
    bool beginMDNS() {
        if (!MDNS.begin(mdnsName)) {
            Debug.println("Erro ao inicializar mDNS");
            return false;
        }

        // Adicionar serviço Modbus TCP ao mDNS
        MDNS.addService(mdnsService, mdnsProto, mdnsPort);
        MDNS.addServiceTxt(mdnsService, mdnsProto, "mode", 
                           gatewayConfig.mode == MODE_MASTER ? "master" : "slave");
        MDNS.addServiceTxt(mdnsService, mdnsProto, "version", "1.0");

        Debug.println("mDNS iniciado com sucesso");
        Debug.print("Acesse em: ");
        Debug.print(mdnsName);
        Debug.println(".local");
        return true;
    }

    // Atualizar mDNS (deve ser chamado no loop)
    void update() {
        MDNS.update();
    }

    // Descobrir outros Gateways na rede
    void discoverGateways() {
        Debug.println("Procurando por outros Ecopower Gateways...");
        int n = MDNS.queryService(mdnsService, mdnsProto);

        if (n == 0) {
            Debug.println("Nenhum Gateway encontrado");
        } else {
            Debug.print(n);
            Debug.println(" Gateway(s) encontrado(s):");
            for (int i = 0; i < n; ++i) {
                Debug.print("  ");
                Debug.print(i + 1);
                Debug.print(". ");
                Debug.print(MDNS.hostname(i));
                Debug.print(" (");
                Debug.print(MDNS.IP(i));
                Debug.print(":");
                Debug.print(MDNS.port(i));
                Debug.println(")");

                // Verificar se é o outro Gateway (não a si mesmo)
                if (MDNS.IP(i) != WiFi.localIP()) {
                    // Se for Mestre e ainda não tiver um IP alvo, usar este
                    if (gatewayConfig.mode == MODE_SLAVE && 
                        strcmp(gatewayConfig.targetIp, "172.168.99.100") == 0) {
                        MDNS.IP(i).toString().toCharArray(gatewayConfig.targetIp, 16);
                        Debug.print("  -> IP alvo atualizado para: ");
                        Debug.println(gatewayConfig.targetIp);
                    }
                }
            }
        }
    }

    // Obter força do sinal WiFi
    int getSignalStrength() {
        return WiFi.RSSI();
    }

    // Obter IP local
    IPAddress getLocalIP() {
        return WiFi.localIP();
    }

    // Obter MAC address
    String getMacAddress() {
        return WiFi.macAddress();
    }

    // Verificar se está conectado
    bool isConnected() {
        return WiFi.status() == WL_CONNECTED;
    }

    // Desconectar
    void disconnect() {
        WiFi.disconnect();
    }
};

#endif // WIFI_MANAGER_H
