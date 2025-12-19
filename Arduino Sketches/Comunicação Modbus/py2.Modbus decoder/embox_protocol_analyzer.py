import struct

def crc16_modbus(data: bytes) -> int:
    """Calcula o CRC-16 Modbus RTU."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def analyze_embox_frame(frame_hex: str):
    """Analisa uma única trama do EMBOX."""
    frame_hex = frame_hex.replace(" ", "")
    if not frame_hex:
        return

    frame_bytes = bytes.fromhex(frame_hex)
    
    # A trama tem 15 bytes. O CRC deve ser os últimos 2 bytes.
    if len(frame_bytes) != 15:
        print(f"ERRO: Tamanho da trama inesperado ({len(frame_bytes)} bytes). Esperado 15.")
        return

    # Separação da trama: Dados (13 bytes) + CRC (2 bytes)
    data = frame_bytes[:-2]
    received_crc = struct.unpack('<H', frame_bytes[-2:])[0] # Assume Low Byte, High Byte
    
    # 1. Cálculo do CRC
    calculated_crc = crc16_modbus(data)
    
    # 2. Análise Estrutural
    header = data[:2].hex().upper() # 7F 7F
    length = data[2:3].hex().upper() # 09
    
    # Os bytes 9 e 10 parecem ser o ID do dispositivo e a Função
    device_id = data[9:10].hex().upper()
    function_code = data[10:11].hex().upper()
    
    # Os bytes 11 e 12 parecem ser o dado principal
    data_value = data[11:13].hex().upper()
    
    print("-" * 50)
    print(f"Trama Hex: {frame_hex.upper()}")
    print(f"Tamanho: {len(frame_bytes)} bytes")
    print(f"Cabeçalho: {header}")
    print(f"Comprimento (Byte 3): {length} (9 decimal)")
    print(f"ID do Dispositivo (Byte 10): {device_id}")
    print(f"Função (Byte 11): {function_code} (Modbus FC 06 - Write Single Register?)")
    print(f"Dado Principal (Bytes 12-13): {data_value}")
    
    # 3. Verificação do CRC
    print(f"\nCRC Recebido: {hex(received_crc).upper()} (Low/High)")
    print(f"CRC Calculado: {hex(calculated_crc).upper()}")
    
    if calculated_crc == received_crc:
        print("STATUS: CRC VÁLIDO (Modbus RTU Padrão)")
    else:
        # Tenta CRC invertido (High Byte, Low Byte)
        inverted_crc = struct.unpack('>H', frame_bytes[-2:])[0]
        if calculated_crc == inverted_crc:
            print("STATUS: CRC VÁLIDO (Bytes Invertidos)")
        else:
            print("STATUS: CRC INVÁLIDO")
            
    print("-" * 50)

def main():
    with open("embox_log.txt", "r") as f:
        log_data = f.readlines()
        
    for line in log_data:
        frame = line.strip()
        if frame:
            analyze_embox_frame(frame)

if __name__ == "__main__":
    main()
