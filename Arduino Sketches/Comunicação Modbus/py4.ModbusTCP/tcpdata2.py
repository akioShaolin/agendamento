#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from http import client
import os
import time
import struct
from datetime import datetime
from pymodbus.client import ModbusTcpClient

# --------------------- Config ---------------------
REG_SEQ = 0x3001
last_seq = None

IP = "172.16.99.100"
PORT = 502
UNIT_ID = 1  # no TCP geralmente não importa, mas ok manter

POLL_S = 1.0  # leitura a cada 1 segundo

# Registros base
REG_150A = 0x150A
LEN_150A = 26  # 13 floats

REG_181E = 0x181E
LEN_181E = 16  # 8 floats (mas vamos usar só 5 e ignorar os ?)

REG_2044 = 0x2044
LEN_2044 = 2   # você disse que é frequência e dividir por 100 (vamos usar regs[0])

# CSV
OUT_DIR = "logs"
OUT_FILE = "dtsu666_log.csv"
CSV_PATH = os.path.join(OUT_DIR, OUT_FILE)

# Mapeamento (ordem sequencial de floats)
FIELDS_150A = ["Uab", "Ubc", "Uca", "Ua", "Ub", "Uc", "Ia", "Ib", "Ic", "Pt", "Pa", "Pb", "Pc"]

# 0x181E: Impt, Impa, Impb, Impc, ?, Expt, ?, ?
# índices de float (0..7)
FIELDS_181E = [
    ("Impt", 0),
    ("Impa", 1),
    ("Impb", 2),
    ("Impc", 3),
    ("Expt", 5),
]

# Cabeçalho final do CSV (sem as interrogações)
CSV_FIELDS = ["DataHora"] + FIELDS_150A + [name for name, _ in FIELDS_181E] + ["Freq_Hz"]

# --------------------- Helpers ---------------------
def ensure_csv():
    os.makedirs(OUT_DIR, exist_ok=True)
    if not os.path.exists(CSV_PATH):
        with open(CSV_PATH, "a", encoding="utf-8", newline="") as f:
            f.write(";".join(CSV_FIELDS) + "\n")

def fmt_dt():
    return datetime.now().strftime("%d/%m/%Y %H:%M:%S")

def regs_to_float_be(reg_hi: int, reg_lo: int) -> float:
    """
    Converte 2 registradores (big-endian por palavra) para float IEEE754.
    Ex.: 0x42CA 0x999A => float
    """
    raw = struct.pack(">HH", reg_hi & 0xFFFF, reg_lo & 0xFFFF)  # 4 bytes BE
    return struct.unpack(">f", raw)[0]

def read_holding(client: ModbusTcpClient, addr: int, count: int):
    """
    Compatível com pymodbus 3.x: unit=...
    Se sua versão não aceitar unit, remova o parâmetro.
    """
    try:
        rr = client.read_holding_registers(address=addr, count=count, unit=UNIT_ID)
    except TypeError:
        rr = client.read_holding_registers(address=addr, count=count)

    if rr.isError():
        return None, str(rr)
    return rr.registers, None

def connect_with_retry():
    """
    Tenta conectar para sempre. Fica em standby até voltar.
    """
    while True:
        client = ModbusTcpClient(IP, port=PORT, timeout=2.0)
        if client.connect():
            return client
        print(f"[{fmt_dt()}] TCP offline ({IP}:{PORT}). Aguardando...")
        time.sleep(2.0)

def acquire_once(client: ModbusTcpClient):
    seq_regs, err = read_holding(client, REG_SEQ, 1)
    if err:
        return None, f"Erro lendo SEQ: {err}"
    seq = seq_regs[0]

    # 1) bloco 150A (26 regs => 13 floats)
    regs150a, err = read_holding(client, REG_150A, LEN_150A)
    if err:
        return None, f"Erro lendo 0x150A: {err}"

    if len(regs150a) != LEN_150A:
        return None, f"Leitura incompleta 0x150A: {len(regs150a)} regs"

    vals_150a = {}
    for i, name in enumerate(FIELDS_150A):
        hi = regs150a[2*i]
        lo = regs150a[2*i + 1]
        vals_150a[name] = regs_to_float_be(hi, lo)

    # 2) bloco 181E (16 regs => 8 floats)
    regs181e, err = read_holding(client, REG_181E, LEN_181E)
    if err:
        return None, f"Erro lendo 0x181E: {err}"

    if len(regs181e) != LEN_181E:
        return None, f"Leitura incompleta 0x181E: {len(regs181e)} regs"

    # decodifica todos os 8 floats, mas usa só os que interessam
    floats181e = []
    for i in range(8):
        hi = regs181e[2*i]
        lo = regs181e[2*i + 1]
        floats181e.append(regs_to_float_be(hi, lo))

    vals_181e = {name: floats181e[idx] for (name, idx) in FIELDS_181E}

    # 3) frequência em 2044: float (2 regs)
    regs2044, err = read_holding(client, REG_2044, 2)
    if err:
        return None, f"Erro lendo 0x2044: {err}"

    if len(regs2044) != 2:
        return None, f"Leitura incompleta 0x2044: {len(regs2044)} regs"

    freq_hz = regs_to_float_be(regs2044[0], regs2044[1]) / 100.0


    # Monta registro final (na ordem do CSV)
    row = [fmt_dt()]
    row += [vals_150a[k] for k in FIELDS_150A]
    row += [vals_181e[k] for (k, _) in FIELDS_181E]
    row += [freq_hz]

    return (seq, row), None


def write_csv_row(row):
    # separador ";" (bom no Excel PT-BR)    
    def fmt(v):
        if isinstance(v, float):
            s = f"{v:.2f}"      # 2 casas decimais
            return s.replace(".", ",")   # <<< troca ponto por vírgula
        return str(v)

    with open(CSV_PATH, "a", encoding="utf-8", newline="") as f:
        f.write(";".join(fmt(v) for v in row) + "\n")

POWER_FIELDS = {"Pt","Pa","Pb","Pc","Impt","Impa","Impb","Impc","Expt"}

def print_values(row):
    # imprime somente grandezas (sem hex, sem registradores)
    # row: [DataHora] + ...
    dt = row[0]
    labels = CSV_FIELDS[1:]
    values = row[1:]

    parts = []
    for lab, val in zip(labels, values):
        if isinstance(val, float):
            if lab in POWER_FIELDS:
                s = f"{val:9.2f}"   # 6 inteiros mín., + . + 2 dec
            else:
                s = f"{val:8.2f}"
        else:
            s = str(val)
        parts.append(f"{lab}={s}")

    print(f"[{dt}] " + " | ".join(parts))

# --------------------- Main ---------------------
def main():
    global last_seq
    ensure_csv()

    client = None
    next_t = time.time()

    while True:
        if client is None:
            client = connect_with_retry()
            print(f"[{fmt_dt()}] Conectado em {IP}:{PORT}")
            next_t = time.time() + POLL_S   # << descarta backlog

        # ritmo de 1s
        now = time.time()
        if now < next_t:
            time.sleep(0.01)
            continue

        # se atrasou muito (ex.: ficou offline), pula pra frente
        if now - next_t > 2 * POLL_S:
            next_t = now + POLL_S
        else:
            next_t += POLL_S

        try:
            data, err = acquire_once(client)

            seq, row = data

            if last_seq is not None and seq == last_seq:
                continue

            last_seq = seq

            if err:
                print(f"[{fmt_dt()}] {err}")
                # derruba conexão e volta standby
                try:
                    client.close()
                except Exception:
                    pass
                client = None
                continue

            # ok: imprime grandezas + grava CSV
            print_values(row)
            write_csv_row(row)

        except Exception as e:
            print(f"[{fmt_dt()}] Exceção: {e}")
            try:
                client.close()
            except Exception:
                pass
            client = None
            time.sleep(1.0)

if __name__ == "__main__":
    main()
