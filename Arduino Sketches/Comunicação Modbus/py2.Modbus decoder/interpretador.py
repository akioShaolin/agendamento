import struct

def float16_from_bits(bits):
    """Converte um inteiro de 16 bits (0..65535) para float (IEEE 754 half-precision)."""
    s = (bits >> 15) & 0x1
    e = (bits >> 10) & 0x1F
    f = bits & 0x3FF
    if e == 0:
        if f == 0:
            return -0.0 if s else 0.0
        return ((-1)**s) * (f / 2**10) * 2**(1 - 15)
    elif e == 0x1F:
        if f == 0:
            return float('-inf') if s else float('inf')
        return float('nan')
    else:
        return ((-1)**s) * (1 + f / 2**10) * 2**(e - 15)

def signed_int16(x):
    return x - 0x10000 if x & 0x8000 else x

def main():
    print("Digite os bytes (ex: '9B EE' ou '9bee2f41'):")
    entrada = input("> ").replace(" ", "").lower()

    # Verifica quantidade de bytes
    if len(entrada) not in (4, 8):
        print("❌ Digite 2 bytes (4 hex dígitos) ou 4 bytes (8 hex dígitos).")
        return

    # --- INTERPRETAÇÃO 16 BITS ---
    if len(entrada) == 4:
        b1 = int(entrada[0:2], 16)
        b2 = int(entrada[2:4], 16)
        bits_be = (b1 << 8) | b2
        bits_le = (b2 << 8) | b1

        print("\n📘 Interpretações para 16 bits (2 bytes):")
        print(f"Bytes: 0x{b1:02X} 0x{b2:02X}")
        print(f"uint16 (big-endian) = {bits_be}")
        print(f"int16 (big-endian) = {signed_int16(bits_be)}")
        print(f"uint16 (little-endian) = {bits_le}")
        print(f"int16 (little-endian) = {signed_int16(bits_le)}")
        print(f"bytes individuais: 0x{b1:02X} = {b1} (ou {b1 - 256 if b1 >= 128 else b1}), "
              f"0x{b2:02X} = {b2} (ou {b2 - 256 if b2 >= 128 else b2})")
        print(f"float16 (IEEE754, big-endian) = {float16_from_bits(bits_be)}")
        print(f"float16 (IEEE754, little-endian) = {float16_from_bits(bits_le)}")
        print(f"Q8.8 (raw / 256) = {bits_be / 256}")
        print(f"Q4.12 (raw / 4096) = {bits_be / 4096}")
        print(f"Q1.15_signed (signed int16 / 32768) = {signed_int16(bits_be) / 32768}")

    # --- INTERPRETAÇÃO 32 BITS ---
    elif len(entrada) == 8:
        b = bytes.fromhex(entrada)
        b_le = b[::-1]  # inverter para little endian

        u32_be = int.from_bytes(b, 'big')
        i32_be = int.from_bytes(b, 'big', signed=True)
        u32_le = int.from_bytes(b_le, 'big')
        i32_le = int.from_bytes(b_le, 'big', signed=True)

        print("\n📗 Interpretações para 32 bits (4 bytes):")
        print(f"Bytes: {' '.join(f'0x{x:02X}' for x in b)}")
        print(f"uint32 (big-endian) = {u32_be}")
        print(f"int32 (big-endian) = {i32_be}")
        print(f"uint32 (little-endian) = {u32_le}")
        print(f"int32 (little-endian) = {i32_le}")

        try:
            f_be = struct.unpack('>f', b)[0]
            f_le = struct.unpack('<f', b)[0]
            print(f"float32 (big-endian) = {f_be}")
            print(f"float32 (little-endian) = {f_le}")
        except:
            print("Erro ao interpretar como float32.")

        print(f"Q16.16 (raw / 65536) = {u32_be / 65536}")
        print(f"Q8.24 (raw / 16777216) = {u32_be / 16777216}")
        print(f"Q1.31_signed (int32 / 2147483648) = {i32_be / 2147483648}")

if __name__ == "__main__":
    main()
