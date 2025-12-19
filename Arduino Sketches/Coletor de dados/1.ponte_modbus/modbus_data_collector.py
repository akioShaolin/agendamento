import time
import csv
from datetime import datetime
from pymodbus.client import ModbusTcpClient
from pymodbus.constants import Endian
from pymodbus.payload import BinaryPayloadDecoder

# --- Configurações de Conexão ---
# O IP deve ser o mesmo configurado no ESP07 (esp07_modbus_bridge.ino)
HOST = '192.168.1.100' 
PORT = 502 # Porta padrão Modbus TCP
SLAVE_ID = 1 # ID do inversor WEG na rede RS485

# --- Mapeamento Modbus WEG SIW400G (Exemplo) ---
# Substitua pelos registradores reais do seu mapa Modbus
REGISTERS_TO_READ = [
    {'name': 'Tensao_Fase_A', 'address': 40001, 'count': 2, 'type': 'float'},
    {'name': 'Corrente_Fase_A', 'address': 40003, 'count': 2, 'type': 'float'},
    {'name': 'Potencia_Ativa', 'address': 40005, 'count': 2, 'type': 'float'},
    {'name': 'Frequencia', 'address': 40007, 'count': 1, 'type': 'int16'},
    {'name': 'Energia_Total', 'address': 40008, 'count': 4, 'type': 'uint64'},
]

# --- Configurações de Arquivo ---
CSV_FILE = 'weg_inverter_data.csv'
COLLECTION_INTERVAL_SECONDS = 5 # Coleta a cada 5 segundos

def setup_csv():
    """Cria o arquivo CSV e escreve o cabeçalho."""
    fieldnames = ['Timestamp'] + [reg['name'] for reg in REGISTERS_TO_READ]
    with open(CSV_FILE, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

def read_modbus_data(client):
    """Lê os dados do inversor via Modbus TCP."""
    data = {'Timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
    
    for reg in REGISTERS_TO_READ:
        address = reg['address'] - 40001 # Ajuste para 0-based (4xxxx)
        count = reg['count']
        
        # Lê os registradores de holding
        result = client.read_holding_registers(address, count, slave=SLAVE_ID)
        
        if result.isError():
            print(f"Erro ao ler {reg['name']} ({reg['address']}): {result}")
            data[reg['name']] = 'ERROR'
            continue
            
        # Decodifica os dados
        decoder = BinaryPayloadDecoder.fromRegisters(
            result.registers, 
            byteorder=Endian.Big, 
            wordorder=Endian.Big
        )
        
        value = None
        if reg['type'] == 'float':
            value = decoder.decode_32bit_float()
        elif reg['type'] == 'int16':
            value = decoder.decode_16bit_int()
        elif reg['type'] == 'uint64':
            value = decoder.decode_64bit_uint()
            
        data[reg['name']] = value
        
    return data

def save_to_csv(data):
    """Salva os dados coletados no arquivo CSV."""
    fieldnames = ['Timestamp'] + [reg['name'] for reg in REGISTERS_TO_READ]
    with open(CSV_FILE, 'a', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writerow(data)

def main():
    print(f"Iniciando coletor de dados Modbus TCP em {HOST}:{PORT}")
    setup_csv()
    
    client = ModbusTcpClient(HOST, port=PORT)
    
    try:
        if not client.connect():
            print("ERRO: Não foi possível conectar ao ESP07 (Modbus TCP). Verifique o IP e a conexão.")
            return

        print("Conexão Modbus TCP estabelecida. Coletando dados...")
        
        while True:
            data = read_modbus_data(client)
            save_to_csv(data)
            print(f"Dados coletados e salvos: {data}")
            time.sleep(COLLECTION_INTERVAL_SECONDS)

    except KeyboardInterrupt:
        print("\nColeta de dados interrompida pelo usuário.")
    except Exception as e:
        print(f"Ocorreu um erro: {e}")
    finally:
        if client.is_socket_open():
            client.close()

if __name__ == "__main__":
    main()
