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

def bytes_to_float(data: bytes) -> float:
    """Converts 4 bytes (2 registers) to a single-precision float (IEEE 754)."""
    # Assuming Big-Endian (ABCD) float representation, common in Modbus
    # If it's Little-Endian (CDAB) or other, this needs adjustment.
    # We will assume Big-Endian (ABCD) for now.
    return struct.unpack('>f', data)[0]

def parse_fc03_request(frame_bytes: bytes):
    """Parses and validates a Function 03 (Read Holding Registers) request frame."""
    # Min length: Slave(1) + FC(1) + StartAddr(2) + NumRegs(2) + CRC(2) = 8 bytes
    if len(frame_bytes) != 8 or frame_bytes[1] != 0x03:
        return None

    return {
        'slave_addr': frame_bytes[0],
        'function_code': frame_bytes[1],
        'start_address': (frame_bytes[2] << 8) | frame_bytes[3],
        'num_registers': (frame_bytes[4] << 8) | frame_bytes[5],
        'type': 'REQUEST'
    }

def parse_fc03_response(frame_bytes: bytes):
    """Parses and validates a Function 03 response frame."""
    # Min length: Slave(1) + FC(1) + ByteCount(1) + CRC(2) = 5 bytes
    if len(frame_bytes) < 5 or frame_bytes[1] != 0x03:
        return None

    byte_count = frame_bytes[2]
    expected_len = 3 + byte_count + 2
    if len(frame_bytes) != expected_len:
        return None

    # Data extraction
    data_bytes = frame_bytes[3:3+byte_count]
    registers = []
    for i in range(0, byte_count, 2):
        registers.append((data_bytes[i] << 8) | data_bytes[i+1])

    return {
        'slave_addr': frame_bytes[0],
        'function_code': frame_bytes[1],
        'byte_count': byte_count,
        'registers': registers,
        'type': 'RESPONSE'
    }

def find_modbus_frames(byte_stream: bytes) -> list:
    """
    Scans the byte stream for valid Modbus RTU frames (FC 03) and returns them.
    This function handles the concatenation problem by checking all possible frame lengths
    starting from a potential frame header.
    """
    frames = []
    i = 0
    while i < len(byte_stream) - 4:
        # Look for potential frame start: Slave Address (01) and Function Code (03)
        if byte_stream[i] == 0x01 and byte_stream[i+1] == 0x03:
            
            # 1. Try to parse as a REQUEST (Fixed length 8 bytes)
            if i + 8 <= len(byte_stream):
                req_candidate = byte_stream[i : i + 8]
                data_for_crc = req_candidate[:-2]
                crc_received = (req_candidate[-1] << 8) | req_candidate[-2]
                crc_calculated = crc16_modbus(data_for_crc)

                if crc_received == crc_calculated:
                    parsed_data = parse_fc03_request(req_candidate)
                    if parsed_data:
                        frames.append({
                            'start_idx': i,
                            'end_idx': i + 7,
                            'bytes': req_candidate,
                            'parsed_data': parsed_data
                        })
                        i += 8
                        continue
            
            # 2. Try to parse as a RESPONSE (Variable length based on Byte Count)
            if i + 3 < len(byte_stream): # Ensure Byte Count byte exists
                byte_count = byte_stream[i+2]
                expected_res_len = 3 + byte_count + 2
                
                if i + expected_res_len <= len(byte_stream):
                    res_candidate = byte_stream[i : i + expected_res_len]
                    data_for_crc = res_candidate[:-2]
                    crc_received = (res_candidate[-1] << 8) | res_candidate[-2]
                    crc_calculated = crc16_modbus(data_for_crc)

                    if crc_received == crc_calculated:
                        parsed_data = parse_fc03_response(res_candidate)
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
        return "Nenhuma trama Modbus (FC 03) válida encontrada com CRC correto."

    output = ["\n--- Análise da Comunicação Modbus RTU (FC 03) ---\n"]
    
    for idx, frame in enumerate(frames, 1):
        data = frame['parsed_data']
        
        output.append(f"### Trama #{idx} - {data['type']}")
        output.append(f"  Bytes: {' '.join(f'{b:02X}' for b in frame['bytes'])}")
        output.append(f"  CRC Válido: {crc16_modbus(frame['bytes'][:-2]):04X} (Recebido: {(frame['bytes'][-1] << 8) | frame['bytes'][-2]:04X})")
        
        if data['type'] == 'REQUEST':
            output.append(f"  🔵 MESTRE -> ESCRAVO 0x{data['slave_addr']:02X}")
            output.append(f"    Função: 0x{data['function_code']:02X} (Read Holding Registers)")
            output.append(f"    Endereço Inicial: 0x{data['start_address']:04X} ({data['start_address']} decimal)")
            output.append(f"    Registros Solicitados: {data['num_registers']}")
        
        elif data['type'] == 'RESPONSE':
            output.append(f"  🟢 ESCRAVO 0x{data['slave_addr']:02X} -> MESTRE")
            output.append(f"    Função: 0x{data['function_code']:02X} (Read Holding Registers)")
            output.append(f"    Contagem de Bytes: {data['byte_count']}")
            output.append(f"    Registros Lidos ({len(data['registers'])}):")
            
            # Attempt to decode data, focusing on 4-byte floats
            reg_output = []
            
            # Check for 4-byte float pattern (groups of 2 registers)
            if len(data['registers']) >= 2:
                
                # Reconstruct the raw data bytes for float conversion
                raw_data = b''
                for reg in data['registers']:
                    raw_data += struct.pack('>H', reg) # Big-Endian Half-Word (Register)
                
                # Iterate over the raw data in 4-byte chunks
                for i in range(0, len(raw_data), 4):
                    float_bytes = raw_data[i:i+4]
                    if len(float_bytes) == 4:
                        try:
                            float_val = bytes_to_float(float_bytes)
                            reg_output.append(f"      - Float (4 bytes): {float_bytes.hex().upper()} -> {float_val:.4f}")
                        except struct.error:
                            reg_output.append(f"      - Float (4 bytes): {float_bytes.hex().upper()} -> ERRO DE DECODIFICAÇÃO")
                    else:
                        reg_output.append(f"      - Dados restantes: {float_bytes.hex().upper()}")
            
            # Also show raw register values
            #reg_output.append("\n    Valores Brutos dos Registros (2 bytes):")
            #for reg in data['registers']:
            #    reg_output.append(f"      - 0x{reg:04X} ({reg} decimal)")
            
            output.extend(reg_output)
        
        output.append("-" * 50)
    
    return "\n".join(output)

# Dados fornecidos pelo usuário para teste
log_data = """
[RX] 07 03 A0 18 00 18 E7 A1 
[RX] 07 03 30 22 BA 00 00 22 BA 00 00 21 07 00 00 21 07 00 00 20 FD 00 00 20 FD 00 00 1A 0A 00 00 1A 0A 00 00 1A 12 00 00 1A 12 00 00 18 5B 00 00 18 5B 00 00 88 91 
"""

if __name__ == '__main__':
    result = decode_modbus_log(log_data)
    print(result)
