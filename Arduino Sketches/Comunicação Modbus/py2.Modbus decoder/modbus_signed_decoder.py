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

def parse_fc23_request(frame_bytes: bytes):
    """Parses and validates a Function 23 (Read/Write Multiple Registers) request frame."""
    # Min length: Slave(1) + FC(1) + ReadAddr(2) + NumRead(2) + WriteAddr(2) + NumWrite(2) + ByteCount(1) + Data(2) + CRC(2) = 15 bytes
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

    write_values_unsigned = []
    write_values_signed = []
    idx = 11
    for _ in range(write_qty):
        value = (frame_bytes[idx] << 8) | frame_bytes[idx + 1]
        write_values_unsigned.append(value)
        write_values_signed.append(decode_signed_16bit(value))
        idx += 2

    return {
        'slave_addr': frame_bytes[0],
        'function_code': frame_bytes[1],
        'read_start': read_start,
        'read_qty': read_qty,
        'write_start': write_start,
        'write_qty': write_qty,
        'write_byte_count': write_byte_count,
        'write_values_unsigned': write_values_unsigned,
        'write_values_signed': write_values_signed,
        'type': 'REQUEST'
    }

def parse_fc23_response(frame_bytes: bytes):
    """Parses and validates a Function 23 response frame."""
    # Min length: Slave(1) + FC(1) + ByteCount(1) + Data(2) + CRC(2) = 7 bytes
    if len(frame_bytes) < 7 or frame_bytes[1] != 0x17:
        return None

    byte_count = frame_bytes[2]
    expected_len = 3 + byte_count + 2
    if len(frame_bytes) != expected_len:
        return None

    # Data extraction
    data_bytes = frame_bytes[3:3+byte_count]
    registers_unsigned = []
    registers_signed = []
    for i in range(0, byte_count, 2):
        reg_val = (data_bytes[i] << 8) | data_bytes[i+1]
        registers_unsigned.append(reg_val)
        registers_signed.append(decode_signed_16bit(reg_val))

    return {
        'slave_addr': frame_bytes[0],
        'function_code': frame_bytes[1],
        'byte_count': byte_count,
        'registers_unsigned': registers_unsigned,
        'registers_signed': registers_signed,
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
    # Remove [RX] and split by spaces, filter empty strings
    hex_list = re.sub(r'\[RX\]', '', log_text).split()
    
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
        output.append(f"  Bytes: {' '.join(f'{b:02X}' for b in frame['bytes'])}")
        output.append(f"  CRC Válido: {crc16_modbus(frame['bytes'][:-2]):04X} (Recebido: {(frame['bytes'][-1] << 8) | frame['bytes'][-2]:04X})")
        
        if data['type'] == 'REQUEST':
            output.append(f"  🔵 MESTRE -> ESCRAVO 0x{data['slave_addr']:02X}")
            output.append(f"    Função: 0x{data['function_code']:02X} (Read/Write Multiple Registers)")
            output.append(f"    Leitura: Endereço 0x{data['read_start']:04X} ({data['read_start']} decimal), Qtd. {data['read_qty']}")
            output.append(f"    Escrita: Endereço 0x{data['write_start']:04X} ({data['write_start']} decimal), Qtd. {data['write_qty']}")
            output.append(f"    Valores de Escrita ({len(data['write_values_unsigned'])}):")
            
            for i in range(len(data['write_values_unsigned'])):
                hex_val = f"0x{data['write_values_unsigned'][i]:04X}"
                unsigned = data['write_values_unsigned'][i]
                signed = data['write_values_signed'][i]
                output.append(f"      - {hex_val}: Unsigned={unsigned}, Signed={signed}")
        
        elif data['type'] == 'RESPONSE':
            output.append(f"  🟢 ESCRAVO 0x{data['slave_addr']:02X} -> MESTRE")
            output.append(f"    Função: 0x{data['function_code']:02X} (Read/Write Multiple Registers)")
            output.append(f"    Contagem de Bytes: {data['byte_count']}")
            output.append(f"    Registros Lidos ({len(data['registers_unsigned'])}):")
            
            for i in range(len(data['registers_unsigned'])):
                hex_val = f"0x{data['registers_unsigned'][i]:04X}"
                unsigned = data['registers_unsigned'][i]
                signed = data['registers_signed'][i]
                output.append(f"      - {hex_val}: Unsigned={unsigned}, Signed={signed}")
        
        output.append("-" * 50)
    
    return "\n".join(output)

# Dados de exemplo (use o log completo do arquivo para uma análise real)
log_data_example = """
[RX] 02 17 C3 4F 00 09 C3 4F 00 0A 14 FF 29 00 55 03 E8 00 00 00 55 00 CF 03 E8 00 55 00 1E 00 00 8E BD 
[RX] 02 17 12 18 00 00 00 00 00 00 06 00 16 00 00 03 E8 00 3B 88 C5 3B 53 
[RX] 02 17 C3 4F 00 09 C3 4F 00 0A 14 FF 2A 00 55 03 E8 00 00 00 55 00 CF 03 E8 00 55 00 1E 00 00 CA F9 
[RX] 02 17 12 18 00 00 0E 00 00 00 07 00 1B 00 00 03 E8 00 3B 88 C5 E9 3B 
"""

if __name__ == '__main__':
    # Para a análise completa, você deve carregar o conteúdo do arquivo
    # /home/ubuntu/sniff_parallel_28102025_094740.txt
    try:
        with open("\\", "r") as f:
            log_data_full = f.read()
    except FileNotFoundError:
        log_data_full = log_data_example
    
    result = decode_modbus_log(log_data_full)
    print(result)
