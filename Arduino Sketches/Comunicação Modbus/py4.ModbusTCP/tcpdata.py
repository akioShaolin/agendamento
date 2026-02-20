#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import time
from pymodbus.client import ModbusTcpClient

IP = "172.16.99.100"
PORT = 502
UNIT_ID = 1  # geralmente ignorado em TCP, mas deixe 1

REG_ID = 0x002B
ID_EXPECTED = 0x5348

BLOCKS = [
    ("A 0x150A x26", 0x150A, 26),
    ("B 0x181E x16", 0x181E, 16),
    ("C 0x150A x26", 0x150A, 26),
    ("D 0x2044 x2",  0x2044, 2),
]

PERIOD_S = 0.25

def read_holding(client, addr, count):
    """
    Atenção sobre endereçamento:
    - Em Modbus, o campo 'address' no PDU é 0-based.
    - No seu servidor, nós reservamos exatamente pelo número (0x150A etc).
    - Então aqui usamos o MESMO valor.
    Se você ler e vier tudo deslocado/errado, teste addr-1.
    """
    rr = client.read_holding_registers(address=addr, count=count)

    if rr.isError():
        return None, str(rr)
    return rr.registers, None

def fmt_regs(regs):
    return " ".join(f"{r:04X}" for r in regs)

def main():
    client = ModbusTcpClient(IP, port=PORT)
    if not client.connect():
        print("Falha ao conectar em", IP, PORT)
        return

    print(f"Conectado em {IP}:{PORT}")
    try:
        while True:
            # 1) lê ID
            regs, err = read_holding(client, REG_ID, 1)
            if err:
                print("[ID] ERRO:", err)
                time.sleep(1.0)
                continue

            dev_id = regs[0]
            ok = (dev_id == ID_EXPECTED)
            print(f"[ID] 0x{REG_ID:04X} = 0x{dev_id:04X} {'OK' if ok else 'DIFF'}")

            # se quiser ser rígido como sua lógica:
            if not ok:
                time.sleep(1.0)
                continue

            # 2) lê blocos
            for name, start, count in BLOCKS:
                regs, err = read_holding(client, start, count)
                if err:
                    print(f"[{name}] ERRO:", err)
                    break
                print(f"[{name}] {start:04X}..{start+count-1:04X} : {fmt_regs(regs)}")

            print("-" * 80)
            time.sleep(PERIOD_S)

    finally:
        client.close()

if __name__ == "__main__":
    main()
