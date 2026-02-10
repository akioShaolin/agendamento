#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESP8266WebServer.h>
// #include <SHA256.h> // Removido para evitar dependência de biblioteca não padrão
#include "config.h"

// =============================================================================
// GERENCIADOR DE SERVIDOR WEB
// =============================================================================

class WebServerManager {
private:
    ESP8266WebServer* server;
    bool authenticated;

public:
    WebServerManager(ESP8266WebServer* srv) : server(srv), authenticated(false) {}

    // Inicializar o servidor web
    void begin() {
        server->on("/", HTTP_GET, [this]() { this->handleRoot(); });
        server->on("/api/config", HTTP_GET, [this]() { this->handleGetConfig(); });
        server->on("/api/config", HTTP_POST, [this]() { this->handlePostConfig(); });
        server->on("/api/status", HTTP_GET, [this]() { this->handleStatus(); });
        server->on("/api/discover", HTTP_GET, [this]() { this->handleDiscover(); });
        server->onNotFound([this]() { this->handleNotFound(); });
        server->begin();
    }

    // Processar requisições
    void handleClient() {
        server->handleClient();
    }

    // Validar token
    bool validateToken(const String& token) {
        return token == String(gatewayConfig.token);
    }

private:
    // Página inicial (HTML + CSS + JavaScript)
    void handleRoot() {
        String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>Ecopower Smart Gateway</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; justify-content: center; align-items: center; padding: 20px; }
        .container { background: white; border-radius: 10px; box-shadow: 0 10px 40px rgba(0,0,0,0.2); max-width: 500px; width: 100%; padding: 40px; }
        h1 { color: #333; margin-bottom: 30px; text-align: center; }
        .form-group { margin-bottom: 20px; }
        label { display: block; margin-bottom: 8px; color: #555; font-weight: 500; }
        input, select { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 5px; font-size: 14px; }
        input:focus, select:focus { outline: none; border-color: #667eea; box-shadow: 0 0 5px rgba(102, 126, 234, 0.3); }
        button { width: 100%; padding: 12px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; border: none; border-radius: 5px; font-size: 16px; font-weight: 600; cursor: pointer; margin-top: 10px; transition: opacity 0.3s; }
        button:hover { opacity: 0.9; }
        .status { background: #f0f0f0; padding: 15px; border-radius: 5px; margin-top: 30px; }
        .status-item { display: flex; justify-content: space-between; margin-bottom: 10px; }
        .status-label { font-weight: 600; color: #333; }
        .status-value { color: #666; }
        .alert { padding: 12px; border-radius: 5px; margin-bottom: 20px; display: none; }
        .alert.success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .alert.error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .alert.info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>⚙️ Ecopower Smart Gateway</h1>
        
        <div class='alert' id='alert'></div>

        <form id='configForm'>
            <div class='form-group'>
                <label for='mode'>Modo de Operação:</label>
                <select id='mode' name='mode' required>
                    <option value='0'>Mestre (Lê do RS485 → Envia via WiFi)</option>
                    <option value='1'>Escravo (Recebe via WiFi → Escreve no RS485)</option>
                </select>
            </div>

            <div class='form-group'>
                <label for='token'>Token de Segurança (até 32 caracteres):</label>
                <input type='password' id='token' name='token' maxlength='32' placeholder='Insira um token seguro' required>
            </div>

            <div class='form-group'>
                <label for='targetIp'>IP do Outro Gateway (Modo Mestre):</label>
                <input type='text' id='targetIp' name='targetIp' placeholder='192.168.1.100' pattern='\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}'>
            </div>

            <button type='submit'>💾 Salvar Configuração</button>
        </form>

        <div class='status'>
            <h3 style='margin-bottom: 15px; color: #333;'>📊 Status Atual</h3>
            <div class='status-item'>
                <span class='status-label'>Modo:</span>
                <span class='status-value' id='statusMode'>-</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>IP Local:</span>
                <span class='status-value' id='statusIp'>-</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>WiFi:</span>
                <span class='status-value' id='statusWifi'>-</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>Modbus RTU:</span>
                <span class='status-value' id='statusRtu'>-</span>
            </div>
            <div class='status-item'>
                <span class='status-label'>Modbus TCP:</span>
                <span class='status-value' id='statusTcp'>-</span>
            </div>
        </div>
    </div>

    <script>
        // Carregar configuração ao iniciar
        window.addEventListener('load', function() {
            loadConfig();
            updateStatus();
            setInterval(updateStatus, 5000); // Atualizar status a cada 5 segundos
        });

        // Carregar configuração
        function loadConfig() {
            fetch('/api/config')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('mode').value = data.mode;
                    document.getElementById('token').value = data.token;
                    document.getElementById('targetIp').value = data.targetIp;
                })
                .catch(error => showAlert('Erro ao carregar configuração: ' + error, 'error'));
        }

        // Enviar configuração
        document.getElementById('configForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const formData = new FormData(this);
            fetch('/api/config', {
                method: 'POST',
                body: formData
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    showAlert('Configuração salva com sucesso! Reiniciando...', 'success');
                    setTimeout(() => location.reload(), 2000);
                } else {
                    showAlert('Erro: ' + data.message, 'error');
                }
            })
            .catch(error => showAlert('Erro ao salvar: ' + error, 'error'));
        });

        // Atualizar status
        function updateStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('statusMode').textContent = data.mode === 0 ? 'Mestre' : 'Escravo';
                    document.getElementById('statusIp').textContent = data.localIp;
                    document.getElementById('statusWifi').textContent = data.wifiSignal + ' dBm';
                    document.getElementById('statusRtu').textContent = data.rtuConnected ? '✓ Conectado' : '✗ Desconectado';
                    document.getElementById('statusTcp').textContent = data.tcpConnected ? '✓ Conectado' : '✗ Desconectado';
                })
                .catch(error => console.log('Erro ao atualizar status: ' + error));
        }

        // Mostrar alerta
        function showAlert(message, type) {
            const alert = document.getElementById('alert');
            alert.textContent = message;
            alert.className = 'alert ' + type;
            alert.style.display = 'block';
            if (type !== 'error') {
                setTimeout(() => alert.style.display = 'none', 5000);
            }
        }
    </script>
</body>
</html>
        )";
        server->send(200, "text/html; charset=utf-8", html);
    }

    // Obter configuração (JSON)
    void handleGetConfig() {
        String json = "{\"mode\":" + String(gatewayConfig.mode) + 
                      ",\"token\":\"" + String(gatewayConfig.token) + 
                      "\",\"targetIp\":\"" + String(gatewayConfig.targetIp) + "\"}";
        server->send(200, "application/json", json);
    }

    // Salvar configuração (JSON)
    void handlePostConfig() {
        // Validar token
        if (server->hasArg("token") && !validateToken(server->arg("token"))) {
            // Se o token foi fornecido mas não é válido, rejeitar
            // (Apenas na primeira vez, o token padrão é aceito)
            if (String(gatewayConfig.token) != "default_token_12345678901234567890") {
                server->send(403, "application/json", "{\"success\":false,\"message\":\"Token inválido\"}");
                return;
            }
        }

        // Salvar configuração
        if (server->hasArg("mode")) {
            gatewayConfig.mode = (GatewayMode)server->arg("mode").toInt();
        }
        if (server->hasArg("token")) {
            String token = server->arg("token");
            token.toCharArray(gatewayConfig.token, 33);
        }
        if (server->hasArg("targetIp")) {
            String ip = server->arg("targetIp");
            ip.toCharArray(gatewayConfig.targetIp, 16);
        }

        //EEPROM.begin(sizeof(GatewayConfig));
        EEPROM.put(0, gatewayConfig);
        EEPROM.commit();
        EEPROM.end();
        EEPROM.begin(sizeof(GatewayConfig));
        EEPROM.put(0, gatewayConfig);
        EEPROM.commit();
        EEPROM.end();
        // Trecho do código saveConfig retirado da linha 46 do arquivo EcopowerSmartGateway.ino
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Configuração salva\"}");
        delay(1000);
        ESP.restart();
    }

    // Obter status (JSON)
    void handleStatus() {
        String json = "{\"mode\":" + String(gatewayConfig.mode) + 
                      ",\"localIp\":\"" + WiFi.localIP().toString() + 
                      "\",\"wifiSignal\":" + String(WiFi.RSSI()) + 
                      ",\"rtuConnected\":true" +
                      ",\"tcpConnected\":true}";
        server->send(200, "application/json", json);
    }

    // Descoberta de dispositivos (para encontrar outros Gateways)
    void handleDiscover() {
        String json = "{\"deviceName\":\"EcopowerSmartGateway\",\"ip\":\"" + 
                      WiFi.localIP().toString() + 
                      "\",\"mode\":" + String(gatewayConfig.mode) + "}";
        server->send(200, "application/json", json);
    }

    // 404
    void handleNotFound() {
        server->send(404, "text/plain", "404 Not Found");
    }
};

#endif // WEBSERVER_H
