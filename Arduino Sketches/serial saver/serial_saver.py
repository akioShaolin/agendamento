#!/usr/bin/env python3
#Script para receber os dados da porta COM. O datalogger deve ser conectado com o sketch do memory dump para coletar os dados do chip
"""
com_saver.py

Lê dados de uma porta COM/serial continuamente.  
No terminal, digite:
  save               -> salva em um arquivo com nome timestamp (ex: data_20251010_093012.txt)
  save nome.txt      -> salva no arquivo nome.txt
  exit | quit | q    -> encerra o programa

Após salvar, o buffer é limpo e a leitura continua para um novo ciclo.
"""

import sys
import threading
import time
import datetime
import argparse
from collections import deque

try:
    import serial
    import serial.tools.list_ports
except Exception as e:
    print("Erro: biblioteca 'pyserial' não encontrada. Instale com: pip install pyserial")
    raise

# Configurações default
READ_SLEEP = 0.05  # intervalo de leitura principal (s)

def list_ports():
    ports = list(serial.tools.list_ports.comports())
    return ports

def choose_port_interactive():
    ports = list_ports()
    if not ports:
        print("Nenhuma porta serial detectada.")
        return None
    print("Portas seriais detectadas:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device} - {p.description}")
    while True:
        sel = input("Escolha o índice da porta (ou 'q' para sair): ").strip()
        if sel.lower() in ('q','quit','exit'):
            return None
        if sel.isdigit() and 0 <= int(sel) < len(ports):
            return ports[int(sel)].device
        print("Seleção inválida.")

class SerialReader(threading.Thread):
    def __init__(self, ser, buffer, stop_event):
        super().__init__(daemon=True)
        self.ser = ser
        self.buffer = buffer  # deque para evitar realocação excessiva
        self.stop_event = stop_event

    def run(self):
        # Lê em loop até stop_event ser setado
        while not self.stop_event.is_set():
            try:
                # lê bytes disponíveis
                waiting = self.ser.in_waiting if hasattr(self.ser, 'in_waiting') else 0
                if waiting:
                    data = self.ser.read(waiting)
                    try:
                        text = data.decode('utf-8', errors='replace')
                    except Exception:
                        # fallback: representar bytes hex
                        text = data.hex()
                    # append ao buffer (deque de strings)
                    self.buffer.append(text)
                else:
                    # tenta ler um byte para não travar em algumas portas
                    b = self.ser.read(1)
                    if b:
                        try:
                            t = b.decode('utf-8', errors='replace')
                        except Exception:
                            t = b.hex()
                        self.buffer.append(t)
                time.sleep(READ_SLEEP)
            except serial.SerialException as e:
                print(f"\nErro de comunicação serial: {e}")
                self.stop_event.set()
                break
            except Exception as e:
                # Erro inesperado, mas não mata o programa imediatamente
                print(f"\nErro na thread de leitura: {e}")
                time.sleep(0.5)
        # fim do loop
        # tenta fechar a porta (se ainda aberta)
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
        except Exception:
            pass

def buffer_to_string(buffer):
    # Junta e limpa o deque
    if not buffer:
        return ""
    return "".join(buffer)

def clear_buffer(buffer):
    buffer.clear()

def timestamp_filename(prefix="data", ext="txt"):
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"{prefix}_{ts}.{ext}"

def main():
    parser = argparse.ArgumentParser(description="Ler porta COM e salvar buffer via comando.")
    parser.add_argument("port", nargs="?", help="Porta serial (ex: COM6 ou /dev/ttyUSB0). Se omitido, será listado.")
    parser.add_argument("baud", nargs="?", type=int, default=9600, help="Baud rate (padrão 9600).")
    parser.add_argument("--timeout", type=float, default=0.1, help="Timeout de leitura (segundos).")
    args = parser.parse_args()

    port = args.port
    baud = args.baud

    if not port:
        port = choose_port_interactive()
        if not port:
            print("Nenhuma porta selecionada. Encerrando.")
            return

    try:
        ser = serial.Serial(port=port, baudrate=baud, timeout=args.timeout)
    except serial.SerialException as e:
        print(f"Não foi possível abrir a porta {port}: {e}")
        return

    print(f"Abrindo {port} @ {baud} bps. Aguarde... (Ctrl+C para sair)\n")
    stop_event = threading.Event()
    buffer = deque()  # armazenamento em memória (lista de strings)

    reader = SerialReader(ser, buffer, stop_event)
    reader.start()

    try:
        while not stop_event.is_set():
            cmd = input("Comando (save [nome], show, clear, exit): ").strip()
            if not cmd:
                continue
            parts = cmd.split()
            c = parts[0].lower()

            if c == "save":
                if len(parts) >= 2:
                    fname = " ".join(parts[1:])
                else:
                    fname = timestamp_filename("data", "txt")
                content = buffer_to_string(buffer)
                try:
                    with open(fname, "w", encoding="utf-8") as f:
                        f.write(content)
                    print(f"Salvo {len(content)} bytes em '{fname}'.")
                    clear_buffer(buffer)
                except Exception as e:
                    print(f"Erro ao salvar arquivo: {e}")

            elif c in ("show", "dump"):
                # mostra um resumo do buffer (últimos 1000 chars)
                content = buffer_to_string(buffer)
                if not content:
                    print("[buffer vazio]")
                else:
                    preview = content[-1000:]
                    print("---- início do preview (últimos 1000 chars) ----")
                    print(preview)
                    print("---- fim do preview ----")
                    print(f"Tamanho total em memória: {len(content)} bytes")

            elif c == "clear":
                clear_buffer(buffer)
                print("Buffer limpo.")

            elif c in ("exit", "quit", "q"):
                print("Encerrando...")
                stop_event.set()
                break

            else:
                print("Comando não reconhecido. Use: save [nome], show, clear, exit")

    except KeyboardInterrupt:
        print("\nRecebido Ctrl+C. Encerrando...")
        stop_event.set()

    # Aguarda thread terminar
    reader.join(timeout=2.0)
    print("Feito. Porta fechada e programa finalizado.")

if __name__ == "__main__":
    main()
