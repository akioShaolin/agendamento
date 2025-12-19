import paho.mqtt.client as mqtt
from datetime import datetime
import time
 
# =====================================================
# CONFIGURAÇÕES MQTT
# =====================================================
MQTT_BROKER = "192.168.1.50"      # IP do broker MQTT
MQTT_PORT = 1883
MQTT_USER = "usuario"
MQTT_PASS = "senha"
TOPIC_CMD = "cc100/controle/potencia"
 
# =====================================================
# CONFIGURAÇÕES DO INVERSOR
# =====================================================
POTENCIA_NOMINAL_W = 75000  # Inversor de 75kW
 
# =====================================================
# REGRAS DE OPERAÇÃO
# =====================================================
# Formato: (mes, tipo_dia, hora_inicio, hora_fim, percentual_limite)
# tipo_dia: 0 = dias úteis, 1 = sábado, 2 = domingo
REGRAS = [
    # ======= JANEIRO =======
    (1, 0, 10, 14, 0),
    (1, 1, 11, 14, 0),
    (1, 2, 10, 14, 0),
 
    # ======= FEVEREIRO =======
    (2, 1, 11, 15, 0),
    (2, 2, 10, 15, 0),
 
    # ======= MARÇO =======
    (3, 0, 11, 13, 0),
    (3, 1, 11, 14, 0),
    (3, 2, 10, 13, 0),
    (3, 2, 14, 15, 45),
 
    # ======= ABRIL =======
    (4, 0, 10, 13, 0),
    (4, 1, 11, 14, 0),
    (4, 2, 11, 14, 0),
]
 
# =====================================================
# FUNÇÕES DE APOIO
# =====================================================
 
def obter_tipo_dia():
    """
    Retorna o tipo de dia:
    0 = dias úteis (segunda a sexta)
    1 = sábado
    2 = domingo
    """
    dia_semana = datetime.now().weekday()  # 0=segunda ... 6=domingo
    if dia_semana < 5:
        return 0
    elif dia_semana == 5:
        return 1
    else:
        return 2
 
def obter_limite_potencia():
    """Retorna o percentual de potência permitido conforme as regras"""
    agora = datetime.now()
    mes = agora.month
    tipo_dia = obter_tipo_dia()
    hora_atual = agora.hour
 
    for regra in REGRAS:
        mes_r, tipo_r, h_ini, h_fim, limite = regra
        if mes == mes_r and tipo_dia == tipo_r:
            if h_ini <= hora_atual < h_fim:
                return limite
    return 100  # Fora das faixas → 100%
 
def enviar_comando_mqtt(client, percentual):
    """Publica o comando MQTT no formato solicitado"""
    msg = f"limit potencia {percentual}%"
    client.publish(TOPIC_CMD, msg, qos=1, retain=True)
    print(f"[{datetime.now().strftime('%d/%m/%Y %H:%M:%S')}] → {msg}")
 
# =====================================================
# LOOP PRINCIPAL
# =====================================================
 
def main():
    client = mqtt.Client()
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start()
 
    ultima_potencia = None
 
    print("=== Controle de Injeção MQTT - CC100 WAGO ===")
    print(f"Inversor: {POTENCIA_NOMINAL_W/1000:.1f} kW\n")
 
    try:
        while True:
            potencia_percentual = obter_limite_potencia()
 
            if potencia_percentual != ultima_potencia:
                enviar_comando_mqtt(client, potencia_percentual)
                ultima_potencia = potencia_percentual
 
            time.sleep(60)  # verifica a cada 1 minuto
 
    except KeyboardInterrupt:
        print("\nEncerrando programa...")
    finally:
        client.loop_stop()
        client.disconnect()
 
# =====================================================
# EXECUÇÃO
# =====================================================
if __name__ == "__main__":
    main()