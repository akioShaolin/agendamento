import serial
import struct
import time
import sys

# --- CONFIGURAÇÕES DE COMUNICAÇÃO ---
# Ajuste a porta e a taxa de baud conforme a sua configuração
SERIAL_PORT = 'COM3'  # Altere para a porta serial do seu ESP07 (ex: '/dev/ttyUSB0' no Linux)
BAUD_RATE = 9600
SLAVE_ADDRESS = 0x02  # Endereço do Inversor Slave

# --- MAPA DE REGISTRADORES (Baseado na Análise) ---
# Endereços 0-based
REG_MODE = 0xC350  # Modo de Reativa (0x0055, 0x00A1, 0x00A2)
REG_FP = 0xC351    # Fator de Potência (Signed, Escala 1000)
REG_PA = 0xC34F    # Controle de Potência Ativa (Signed)

# --- FUNÇÕES DE UTILIDADE ---

def crc16_modbus(data: bytes) -> bytes:
    """Calcula o CRC-16 Modbus RTU e retorna como bytes (Low Byte, High Byte)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    # Retorna o CRC em formato Low Byte, High Byte
    return struct.pack('<H', crc)

def build_fc23_request(slave_addr: int, read_start: int, read_qty: int, write_start: int, write_values: list) -> bytes:
    """
    Constrói a trama Modbus RTU para a Função 23 (Read/Write Multiple Registers).
    write_values é uma lista de inteiros de 16 bits.
    """
    # 1. Cabeçalho (Slave Addr, FC, Read Start, Read Qty, Write Start, Write Qty)
    write_qty = len(write_values)
    write_byte_count = write_qty * 2
    
    # Formato: >BHHHBH (Big-Endian)
    header = struct.pack('>B B H H H B', 
                         slave_addr, 
                         0x17, 
                         read_start, 
                         read_qty, 
                         write_start, 
                         write_qty)
    
    # 2. Contagem de Bytes de Escrita
    byte_count_field = struct.pack('>B', write_byte_count)
    
    # 3. Valores de Escrita
    write_data = b''
    for val in write_values:
        # Valores de 16 bits são Big-Endian (>)
        write_data += struct.pack('>H', val)
        
    # 4. Trama sem CRC
    adu = header + byte_count_field + write_data
    
    # 5. Adiciona CRC
    crc = crc16_modbus(adu)
    
    return adu + crc

def to_signed_16bit(value: int) -> int:
    """Converte um valor decimal para o formato de 16 bits com sinal (Two's Complement)."""
    if value < 0:
        return 0x10000 + value
    return value

# --- FUNÇÕES DE COMANDO ---

def set_power_factor(ser: serial.Serial, fp_value: float):
    """
    Envia o comando para definir o Fator de Potência (FP).
    FP é enviado como um valor Signed de 16 bits com escala 1000.
    Ex: 0.8 -> 800, -0.8 -> -800.
    """
    print(f"\n--- Definindo Fator de Potência para {fp_value:.3f} ---")
    
    # 1. Calcular o valor de 16 bits com escala 1000
    scaled_fp = int(fp_value * 1000)
    fp_16bit = to_signed_16bit(scaled_fp)
    
    # 2. Definir o Modo de Reativa (0x00A1 = Power Factor Enable)
    mode_enable = 0x00A1
    
    # 3. Construir a lista de valores de escrita (Regs C34F a C358)
    # Usaremos a trama padrão do inversor como base para garantir a consistência
    # Trama Padrão (Valores de Escrita): 0000, 0055, 03E8, 0000, 0055, 00CF, 03E8, 0055, 001E, 0000
    
    # Apenas os registradores REG_MODE (C350) e REG_FP (C351) serão alterados
    
    write_values = [
        0x0000,             # C34F (Controle de Potência Ativa)
        mode_enable,        # C350 (Modo de Reativa) -> MUDANÇA AQUI
        fp_16bit,           # C351 (Fator de Potência) -> MUDANÇA AQUI
        0x0000,             # C352 (% Potência Reativa)
        0x0055,             # C353
        0x00CF,             # C354
        0x03E8,             # C355 (Potencia Nominal do Inversor)
        0x0055,             # C356
        0x001E,             # C357
        0x0000              # C358
    ]
    
    # 4. Construir a Trama FC 23
    # O inversor lê 9 regs a partir de C34F e escreve 10 regs a partir de C34F
    request = build_fc23_request(
        slave_addr=SLAVE_ADDRESS,
        read_start=REG_PA,
        read_qty=9,
        write_start=REG_PA,
        write_values=write_values
    )
    
    # 5. Enviar e Receber
    print(f"  Enviando Trama ({len(request)} bytes): {request.hex().upper()}")
    ser.write(request)
    
    # Esperar a resposta (FC 23 Response é curta: 8 bytes + CRC = 10 bytes)
    time.sleep(0.1) # Pequeno atraso para o ESP07 processar e o Slave responder
    
    response = ser.read_all()
    
    if response:
        print(f"  Resposta Recebida ({len(response)} bytes): {response.hex().upper()}")
        # A resposta FC 23 é um eco da requisição (Slave Addr, FC, Write Start, Write Qty, CRC)
        # O script não valida o CRC da resposta, apenas exibe.
    else:
        print("  Erro: Nenhuma resposta recebida do Slave.")

# --- FUNÇÃO PRINCIPAL ---

def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.5)
        print(f"Conectado à porta {SERIAL_PORT} @ {BAUD_RATE} bps.")
    except serial.SerialException as e:
        print(f"Erro ao abrir a porta serial {SERIAL_PORT}: {e}")
        sys.exit(1)

    # Exemplo de uso: Definir Fator de Potência para 0.8 (Indutivo)
    set_power_factor(ser, 0.8)
    
    # Exemplo de uso: Definir Fator de Potência para -0.8 (Capacitivo)
    set_power_factor(ser, -0.8)
    
    ser.close()

if __name__ == '__main__':
    main()
