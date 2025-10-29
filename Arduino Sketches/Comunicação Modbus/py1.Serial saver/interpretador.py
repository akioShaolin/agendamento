#!/usr/bin/env python3
"""
parse_eeprom.py

Analisa um dump em formato hexdump (como o memory_dump.txt que você enviou),
reconstrói os bytes e divide em blocos de 70 bytes (conforme informado).
Para cada bloco:
 - extrai strings ASCII (>= minstr)
 - procura padrões de serial (hex/alfanum)
 - tenta interpretar palavras 32-bit little-endian e floats
 - marca offsets com 00 e FF
Gera um CSV resumido e um relatório TXT detalhado.

Uso:
  py parse_eeprom.py path/to/memory_dump.txt --blocksize 70 --minstr 4 --out report.csv
"""
import re
import argparse
import csv
import struct
from pathlib import Path

HEX_LINE_RE = re.compile(r'^[0-9A-Fa-f]+:\s*((?:[0-9A-Fa-f]{2}\s+)+)')

def parse_hexdump_lines(lines):
    ba = bytearray()
    for ln in lines:
        ln = ln.rstrip('\n')
        m = HEX_LINE_RE.match(ln)
        if not m:
            # tenta extrair bytes mesmo sem o offset (caso o formato seja diferente)
            parts = re.findall(r'\b[0-9A-Fa-f]{2}\b', ln)
            if parts:
                ba.extend(int(p,16) for p in parts)
            continue
        bytes_str = m.group(1)
        parts = re.findall(r'[0-9A-Fa-f]{2}', bytes_str)
        ba.extend(int(p, 16) for p in parts)
    return bytes(ba)

def ascii_strings_from_bytes(b, minlen=4):
    out = []
    cur = bytearray()
    start = None
    for i, c in enumerate(b):
        if 32 <= c <= 126:  # printable ASCII
            if start is None:
                start = i
            cur.append(c)
        else:
            if len(cur) >= minlen:
                out.append((start, cur.decode('ascii', errors='replace')))
            cur = bytearray()
            start = None
    if len(cur) >= minlen:
        out.append((start, cur.decode('ascii', errors='replace')))
    return out

# procura padrões típicos de serial (hex-like long; ou alfanum com 'T' etc.)
SERIAL_PATTERNS = [
    re.compile(r'\b[A-F0-9]{8,}\b'),        # sequências hex longas (ex: B7CF21CAEC6B)
    re.compile(r'\b[0-9A-Z]{6,}\b'),        # alfanum maiúsculo (fallback)
    re.compile(r'\b[0-9]{1,2}T[0-9A-Z]{4,}\b', re.I),  # ex: 6T21690...
]

def find_serial_candidates(s):
    found = set()
    for p in SERIAL_PATTERNS:
        for m in p.finditer(s):
            found.add(m.group(0))
    return sorted(found)

def scan_block_numeric_candidates(block_bytes):
    candidates = []
    L = len(block_bytes)
    for off in range(0, L - 3):
        # uint32 little-endian
        le = struct.unpack_from('<I', block_bytes, off)[0]
        # float32 little-endian
        f = struct.unpack_from('<f', block_bytes, off)[0]
        # consider useful ranges:
        is_timestamp = 1_500_000_000 <= le <= 2_200_000_000  # plausible unix timestamps (2017..2040)
        is_reasonable_uint = le != 0xFFFFFFFF and le < 10_000_000_000
        is_reasonable_float = not (f != f or abs(f) > 1e8)  # not NaN and not huge
        if is_timestamp or (is_reasonable_uint and le != 0) or is_reasonable_float:
            candidates.append({
                'off': off,
                'uint32_le': le,
                'float32_le': f,
                'is_ts': is_timestamp
            })
    return candidates

def analyze_blocks(all_bytes, block_size=70, minstr=4, out_csv=None, out_txt=None):
    nblocks = (len(all_bytes) + block_size - 1) // block_size
    rows = []
    txt_lines = []
    for i in range(nblocks):
        start = i * block_size
        block = all_bytes[start:start+block_size]
        txt_lines.append(f"=== Bloco {i} (offset {start}, tamanho {len(block)}) ===")
        # ascii strings
        strs = ascii_strings_from_bytes(block, minlen=minstr)
        txt_lines.append("Strings ASCII encontradas (offset_in_block, string):")
        for off, s in strs:
            txt_lines.append(f"  - +{off:03d}: {s}")
        # serial candidates (from joined ascii strings and from whole block hex)
        joined_ascii = "".join(s for _, s in strs)
        serials = find_serial_candidates(joined_ascii)
        # also check block hex-as-ascii (some serials stored as ASCII hex)
        try:
            hexlike_ascii = ''.join(chr(b) if 32<=b<=126 else '.' for b in block)
        except:
            hexlike_ascii = ''
        serials += find_serial_candidates(hexlike_ascii)
        serials = sorted(set(serials))
        txt_lines.append("Candidatos a nº de série / IDs:")
        if serials:
            for s in serials:
                txt_lines.append(f"  - {s}")
        else:
            txt_lines.append("  - (nenhum detectado)")
        # numeric candidates
        nums = scan_block_numeric_candidates(block)
        txt_lines.append(f"Candidatos numéricos (mostrar até 10): {len(nums)} encontrados")
        for c in nums[:10]:
            ts_tag = " (timestamp?)" if c['is_ts'] else ""
            txt_lines.append(f"  - off +{c['off']:02d}: uint32={c['uint32_le']} float32={c['float32_le']}{ts_tag}")
        # FF/00 summary
        zeros = sum(1 for b in block if b == 0x00)
        ffs = sum(1 for b in block if b == 0xFF)
        txt_lines.append(f"Bytes nulos: {zeros}, bytes 0xFF: {ffs}")
        # prepare CSV row
        rows.append({
            'block_index': i,
            'offset': start,
            'size': len(block),
            'ascii_count': len(strs),
            'serial_candidates': ";".join(serials) if serials else "",
            'zero_count': zeros,
            'ff_count': ffs,
            'numeric_candidates_count': len(nums)
        })
    # escreve CSV
    if out_csv:
        with open(out_csv, 'w', newline='', encoding='utf-8') as f:
            fieldnames = ['block_index','offset','size','ascii_count','serial_candidates','zero_count','ff_count','numeric_candidates_count']
            w = csv.DictWriter(f, fieldnames=fieldnames)
            w.writeheader()
            for r in rows:
                w.writerow(r)
    # escreve TXT
    if out_txt:
        with open(out_txt, 'w', encoding='utf-8') as f:
            for ln in txt_lines:
                f.write(ln + '\n')
    return rows, txt_lines

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dumpfile', help='Arquivo hexdump (texto) a analisar')
    ap.add_argument('--blocksize', type=int, default=70, help='Tamanho do bloco em bytes (padrão 70)')
    ap.add_argument('--minstr', type=int, default=4, help='Min tamanho de string ASCII para extrair')
    ap.add_argument('--out', default='report.csv', help='CSV de saída resumido')
    ap.add_argument('--detail', default='report_detail.txt', help='Relatório detalhado TXT')
    args = ap.parse_args()

    p = Path(args.dumpfile)
    if not p.exists():
        print("Arquivo não encontrado:", args.dumpfile)
        return

    with p.open('r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()

    all_bytes = parse_hexdump_lines(lines)
    print(f"Total de bytes reconstruídos: {len(all_bytes)}")
    rows, txt_lines = analyze_blocks(all_bytes, block_size=args.blocksize, minstr=args.minstr,
                                     out_csv=args.out, out_txt=args.detail)
    print(f"Análise completa. CSV: {args.out}, Relatório: {args.detail}")
    # mostra resumo inicial
    print("Resumo por bloco (primeiras 10 linhas):")
    for r in rows[:10]:
        print(r)

if __name__ == '__main__':
    main()
