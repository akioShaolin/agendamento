import struct
import re

def crc16_modbus(data: bytes) -> int:
    """Calculates the Modbus RTU CRC-16 checksum."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def decode_signed_16bit(value: int) -> int:
    """Converts a 16-bit unsigned integer to a signed integer using Two's Complement."""
    if value & 0x8000:  # Check if the sign bit is set
        return value - 0x10000
    return value

def decode_float_16bit(value: int, scale: int = 100) -> float:
    """
    Interprets a 16-bit integer as a scaled float.
    Assumes the value is a signed 16-bit integer divided by a scale (default 100).
    """
    signed_val = decode_signed_16bit(value)
    return signed_val / scale

def regs_to_float32(reg_high, reg_low):
    """
    Converte dois registradores Modbus (16 bits cada) em um float IEEE-754 de 32 bits (big-endian).
    """
    # Monta os 4 bytes em ordem big-endian (reg_high primeiro)
    data = bytes([
        (reg_high >> 8) & 0xFF,  # byte alto do primeiro registrador
        reg_high & 0xFF,         # byte baixo do primeiro registrador
        (reg_low >> 8) & 0xFF,   # byte alto do segundo registrador
        reg_low & 0xFF           # byte baixo do segundo registrador
    ])
    
    # Interpreta como float (big-endian)
    return struct.unpack('>f', data)[0]

def regs_to_uint32(reg_high, reg_low):
    """
    Converte dois registradores Modbus (16 bits cada) em um inteiro sem sinal de 32 bits (big-endian).
    """
    return (reg_high << 16) | reg_low

def parse_fc23_request(frame_bytes: bytes):
    """Parses and validates a Function 23 (Read/Write Multiple Registers) request frame."""
    # Min length: 15 bytes
    if len(frame_bytes) < 15 or frame_bytes[1] != 0x17:
        return None

    read_start = (frame_bytes[2] << 8) | frame_bytes[3]
    read_qty = (frame_bytes[4] << 8) | frame_bytes[5]
    write_start = (frame_bytes[6] << 8) | frame_bytes[7]
    write_qty = (frame_bytes[8] << 8) | frame_bytes[9]
    write_byte_count = frame_bytes[10]
    
    expected_data_len = write_qty * 2
    if write_byte_count != expected_data_len:
        return None

    expected_len = 11 + write_byte_count + 2
    if len(frame_bytes) != expected_len:
        return None

    write_values = []
    idx = 11
    for _ in range(write_qty):
        value = (frame_bytes[idx] << 8) | frame_bytes[idx + 1]
        write_values.append(value)
        idx += 2

    return {
        'slave_addr': frame_bytes[0],
        'function_code': frame_bytes[1],
        'read_start': read_start,
        'read_qty': read_qty,
        'write_start': write_start,
        'write_qty': write_qty,
        'write_byte_count': write_byte_count,
        'write_values': write_values,
        'type': 'REQUEST'
    }

def parse_fc23_response(frame_bytes: bytes):
    """Parses and validates a Function 23 response frame."""
    # Min length: 7 bytes
    if len(frame_bytes) < 7 or frame_bytes[1] != 0x17:
        return None

    byte_count = frame_bytes[2]
    expected_len = 3 + byte_count + 2
    if len(frame_bytes) != expected_len:
        return None

    # Data extraction
    data_bytes = frame_bytes[3:3+byte_count]
    registers = []
    for i in range(0, byte_count, 2):
        reg_val = (data_bytes[i] << 8) | data_bytes[i+1]
        registers.append(reg_val)

    return {
        'slave_addr': frame_bytes[0],
        'function_code': frame_bytes[1],
        'byte_count': byte_count,
        'registers': registers,
        'type': 'RESPONSE'
    }

def find_modbus_frames(byte_stream: bytes) -> list:
    """
    Scans the byte stream for valid Modbus RTU frames (FC 23) and returns them.
    This function handles the concatenation problem by checking all possible frame lengths
    starting from a potential frame header.
    """
    frames = []
    i = 0
    while i < len(byte_stream) - 4:
        # Look for potential frame start: Slave Address (02) and Function Code (17)
        if byte_stream[i] == 0x02 and byte_stream[i+1] == 0x17:
            
            # 1. Try to parse as a REQUEST (Variable length based on Write Byte Count)
            # Min request length is 15 bytes
            if i + 15 <= len(byte_stream):
                # Try to determine length from Write Byte Count (byte at index i+10)
                write_byte_count = byte_stream[i+10]
                expected_req_len = 11 + write_byte_count + 2
                
                if i + expected_req_len <= len(byte_stream):
                    req_candidate = byte_stream[i : i + expected_req_len]
                    data_for_crc = req_candidate[:-2]
                    crc_received = (req_candidate[-1] << 8) | req_candidate[-2]
                    crc_calculated = crc16_modbus(data_for_crc)

                    if crc_received == crc_calculated:
                        parsed_data = parse_fc23_request(req_candidate)
                        if parsed_data:
                            frames.append({
                                'start_idx': i,
                                'end_idx': i + expected_req_len - 1,
                                'bytes': req_candidate,
                                'parsed_data': parsed_data
                            })
                            i += expected_req_len
                            continue
            
            # 2. Try to parse as a RESPONSE (Variable length based on Byte Count)
            # Min response length is 7 bytes
            if i + 3 < len(byte_stream): # Ensure Byte Count byte exists (i+2)
                byte_count = byte_stream[i+2]
                expected_res_len = 3 + byte_count + 2
                
                if i + expected_res_len <= len(byte_stream):
                    res_candidate = byte_stream[i : i + expected_res_len]
                    data_for_crc = res_candidate[:-2]
                    crc_received = (res_candidate[-1] << 8) | res_candidate[-2]
                    crc_calculated = crc16_modbus(data_for_crc)

                    if crc_received == crc_calculated:
                        parsed_data = parse_fc23_response(res_candidate)
                        if parsed_data:
                            frames.append({
                                'start_idx': i,
                                'end_idx': i + expected_res_len - 1,
                                'bytes': res_candidate,
                                'parsed_data': parsed_data
                            })
                            i += expected_res_len
                            continue
        
        i += 1 # Move to the next byte if no valid frame was found starting at 'i'

    return frames

def decode_modbus_log(log_text: str):
    """Main function to decode the Modbus log."""
    
    # 1. Clean and convert to byte stream
    # Remove timestamp (e.g., "402.797 ") and "[RX]"
    # Use regex to remove anything that is NOT a hex character or a space/newline
    # Then remove all non-hex characters
    cleaned_text = re.sub(r'[^0-9a-fA-F\s]', '', log_text)
    hex_list = cleaned_text.split()
    
    # Convert list of hex strings to a single byte string
    try:
        byte_stream = bytes.fromhex(''.join(hex_list))
    except ValueError as e:
        return f"Erro ao converter para bytes: {e}. Verifique se todos os caracteres são hexadecimais válidos."

    # 2. Find and parse frames
    frames = find_modbus_frames(byte_stream)

    if not frames:
        return "Nenhuma trama Modbus (FC 23) válida encontrada com CRC correto."

    output = ["\n--- Análise da Comunicação Modbus RTU (FC 23) ---\n"]
    
    for idx, frame in enumerate(frames, 1):
        data = frame['parsed_data']
        
        output.append(f"### Trama #{idx} - {data['type']}")
        #output.append(f"  Bytes: {' '.join(f'{b:02X}' for b in frame['bytes'])}")
        #output.append(f"  CRC Válido: {crc16_modbus(frame['bytes'][:-2]):04X} (Recebido: {(frame['bytes'][-1] << 8) | frame['bytes'][-2]:04X})")
        
        if data['type'] == 'REQUEST':
            output.append(f"  🔵 MESTRE -> ESCRAVO 0x{data['slave_addr']:02X}")
            #output.append(f"    Função: 0x{data['function_code']:02X} (Read/Write Multiple Registers)")
            #output.append(f"    Leitura: Endereço 0x{data['read_start']:04X} ({data['read_start']} decimal), Qtd. {data['read_qty']}")
            #output.append(f"    Escrita: Endereço 0x{data['write_start']:04X} ({data['write_start']} decimal), Qtd. {data['write_qty']}")
            #output.append(f"    Valores de Escrita ({len(data['write_values'])}):")
            
            d = data['write_values']

            master = {
                "Controle Ativo": f"{decode_signed_16bit(d[0])*0.1:.1f} kW",
                "Modo": f"0x{d[1]:04X}",
                "Fator de Potência": f"{decode_signed_16bit(d[2])*0.001:.3f}",
                "Potência Reativa Fixa": f"{decode_signed_16bit(d[3])*0.1:.1f} kVAr",
                "C353": f"0x{d[4]:04X}",
                "C354": f"0x{d[5]:04X}",
                "Potência Nominal": f"{decode_signed_16bit(d[6])*0.1:.1f} kW",
                "C356": f"0x{d[7]:04X}",
                "C357": f"0x{d[8]:04X}",
                "C358": f"0x{d[9]:04X}",
            }

            for rotulo, val in master.items():
                output.append(f"{rotulo}: {val}")
        
        elif data['type'] == 'RESPONSE':
            output.append(f"  🟢 ESCRAVO 0x{data['slave_addr']:02X} -> MESTRE")
            #output.append(f"    Função: 0x{data['function_code']:02X} (Read/Write Multiple Registers)")
            #output.append(f"    Contagem de Bytes: {data['byte_count']}")
            #output.append(f"    Registros Lidos ({len(data['registers'])}):\n")

            d = data['registers']

            slave = {
                "Estado de Operação": d[0],
                "Potência Ativa": f"{decode_signed_16bit(d[1])*0.1:.1f} kW",
                "Potência Reativa": f"{decode_signed_16bit(d[2])*0.1:.1f} kVAr",
                "Potência Fotovoltaica": f"{decode_signed_16bit(d[3])*0.1:.1f} kW",
                "Potência Aparente": f"{decode_signed_16bit(d[4])*0.1:.1f} kVA",
                "C354": f"0x{d[5]:04X}",
                "Potência Nominal": f"{decode_signed_16bit(d[6])*0.1:.1f} kW",
                "Geração Acumulada": f"{regs_to_uint32(d[7], d[8])*0.1:.1f} kWh",
            }

            for rotulo, val in slave.items():
                output.append(f"{rotulo}: {val}")
        
        output.append("")  # Linha em branco para separar tramas
        output.append("-" * 50)
    
    return "\n".join(output)

# Dados fornecidos pelo usuário para teste
log_data = """
1327.615 [RX] 07 03 A0 18 00 18 E7 A1 
1327.630 [RX] 07 03 30 22 BA 00 00 22 BA 00 00 21 07 00 00 21 07 00 00 20 FD 00 00 20 FD 00 00 1A 0A 00 00 1A 0A 00 00 1A 12 00 00 1A 12 00 00 18 5B 00 00 18 5B 00 00 88 91 
"""

if __name__ == '__main__':
    result = decode_modbus_log(log_data)
    print(result)
