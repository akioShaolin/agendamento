import json
import time
import hashlib
import requests

# =========================
# AJUSTES RÁPIDOS (TOPO)
# =========================

BASE_URL = "http://10.1.1.118"
USER = "admin"
PASSWORD = "Solar@123"
LANG = "enGB"

# Tempos (segundos)
READ_DATA_PERIOD_S   = 1.0   # leitura /sems/overview/data
READ_STATUS_PERIOD_S = 5.0    # leitura /sems/overview/status
WRITE_PERIOD_S       = 0.25    # escrita do setpoint (GridExportLimit)

# Controle do alvo no medidor (W)
# Pode ser NEGATIVO (ex.: peak shaving / importar menos da rede)
TARGET_METER_W = -15000

# Interpretação do sinal do meterPower
# True  -> meterPower positivo significa EXPORTAÇÃO (para rede)
# False -> meterPower positivo significa IMPORTAÇÃO (da rede)
METER_POSITIVE_IS_EXPORT = True

# Limites do setpoint que vamos escrever (W)
EXPORT_LIMIT_MIN_W = 0
EXPORT_LIMIT_MAX_W = 200_000  # ajuste conforme seu sistema

# Deadband (W): se o erro estiver dentro disso, não mexe (evita ficar "caçando")
DEADBAND_W = 200

# Rampa (W por escrita): limita quanto o setpoint pode mudar a cada WRITE_PERIOD_S
RAMP_W_PER_STEP = 10000

# Violação: se o erro (fora da deadband) persistir por X segundos -> fault
VIOLATION_LIMIT_W = 3000        # erro acima disso conta como "violação séria"
VIOLATION_TIME_S  = 10.0        # quanto tempo contínuo para disparar fault

# Comunicação: quantas falhas seguidas antes de fault
MAX_CONSECUTIVE_COMM_FAILS = 5

# =========================
# FIM DOS AJUSTES
# =========================

def md5_hex(s: str) -> str:
    return hashlib.md5(s.encode("utf-8")).hexdigest()

def is_json_response(r: requests.Response) -> bool:
    return "application/json" in (r.headers.get("Content-Type") or "")

def login(session: requests.Session) -> str:
    url = f"{BASE_URL}/sems/user/login"
    body = json.dumps({"user": USER, "password": md5_hex(PASSWORD)})

    headers = {
        "accept": "application/json, text/plain, */*",
        "content-type": "application/x-www-form-urlencoded;charset=UTF-8",
        "x-requested-with": "XMLHttpRequest",
        "lang": LANG,
        "token": "none",
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
        "pragma": "no-cache",
        "cache-control": "no-cache",
    }

    r = session.post(url, headers=headers, data=body, timeout=8)
    r.raise_for_status()
    if not is_json_response(r):
        raise RuntimeError(f"Login não retornou JSON: {r.text[:200]}")
    j = r.json()
    if j.get("errno") != 0:
        raise RuntimeError(f"Login falhou: {j}")
    return j["result"]["token"]

def get_overview_data(session: requests.Session, token: str) -> dict:
    url = f"{BASE_URL}/sems/overview/data"
    headers = {
        "accept": "application/json, text/plain, */*",
        "lang": LANG,
        "token": token,
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
        "pragma": "no-cache",
        "cache-control": "no-cache",
    }
    r = session.get(url, headers=headers, timeout=8)
    r.raise_for_status()
    if not is_json_response(r):
        raise PermissionError("overview/data não retornou JSON (token inválido?)")
    return r.json()

def get_overview_status(session: requests.Session, token: str) -> dict:
    url = f"{BASE_URL}/sems/overview/status"
    headers = {
        "accept": "application/json, text/plain, */*",
        "lang": LANG,
        "token": token,
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
        "pragma": "no-cache",
        "cache-control": "no-cache",
    }
    r = session.get(url, headers=headers, timeout=8)
    r.raise_for_status()
    if not is_json_response(r):
        raise PermissionError("overview/status não retornou JSON (token inválido?)")
    return r.json()

def get_config(session: requests.Session, token: str) -> dict:
    url = f"{BASE_URL}/sems/operation/get/configJson"
    headers = {
        "accept": "application/json, text/plain, */*",
        "lang": LANG,
        "token": token,
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
        "pragma": "no-cache",
        "cache-control": "no-cache",
    }
    r = session.get(url, headers=headers, timeout=10)
    r.raise_for_status()
    if not is_json_response(r):
        raise PermissionError("get/configJson não retornou JSON (token inválido?)")
    j = r.json()
    if j.get("errno") != 0 or not isinstance(j.get("result"), dict):
        raise RuntimeError(f"get/configJson falhou: {j}")
    return j["result"]

def set_config(session: requests.Session, token: str, config_obj: dict) -> dict:
    url = f"{BASE_URL}/sems/operation/set/configJson"
    payload = json.dumps(config_obj)

    headers = {
        "accept": "application/json, text/plain, */*",
        "content-type": "application/x-www-form-urlencoded;charset=UTF-8",
        "x-requested-with": "XMLHttpRequest",
        "lang": LANG,
        "token": token,
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
        "pragma": "no-cache",
        "cache-control": "no-cache",
    }
    r = session.post(url, headers=headers, data=payload, timeout=12)
    r.raise_for_status()
    if not is_json_response(r):
        raise PermissionError("set/configJson não retornou JSON (token inválido?)")
    return r.json()

def clamp(x: float, lo: float, hi: float) -> float:
    return lo if x < lo else hi if x > hi else x

def to_signed_meter_w(raw_meter: float) -> float:
    """
    Converte meterPower para um sinal coerente:
      + = exportação (para rede)
      - = importação (da rede)
    """
    if raw_meter is None:
        return 0.0
    raw = float(raw_meter)
    return raw if METER_POSITIVE_IS_EXPORT else -raw

def any_nonzero(arr) -> bool:
    try:
        return any(int(x) != 0 for x in arr)
    except Exception:
        return False

if __name__ == "__main__":
    with requests.Session() as s:
        token = None
        comm_fails = 0

        last_data = {}
        last_status = {}
        last_write_limit = None

        violation_accum_s = 0.0
        fault = False

        t_next_data = 0.0
        t_next_status = 0.0
        t_next_write = 0.0

        # Login inicial
        while token is None:
            try:
                token = login(s)
                print("TOKEN:", token)
            except Exception as e:
                print("Login falhou, tentando de novo em 2s:", e)
                time.sleep(2.0)

        print("Control loop iniciado.")
        t0 = time.time()
        t_next_data = t0
        t_next_status = t0
        t_next_write = t0

        while True:
            now = time.time()

            # --------- DATA (rápido) ----------
            if now >= t_next_data and not fault:
                t_next_data += READ_DATA_PERIOD_S
                try:
                    j = get_overview_data(s, token)
                    if j.get("errno") == 40000:
                        raise PermissionError("errno 40000 (token inexistente)")

                    d = (j.get("result") or {}).get("data") or {}
                    last_data = d
                    comm_fails = 0
                except PermissionError:
                    print("[DATA] Token inválido, relogando...")
                    token = None
                except Exception as e:
                    comm_fails += 1
                    print(f"[DATA] Falha: {e} (fails={comm_fails})")

            # --------- STATUS (lento) ----------
            if now >= t_next_status and not fault:
                t_next_status += READ_STATUS_PERIOD_S
                try:
                    j = get_overview_status(s, token)
                    if j.get("errno") == 40000:
                        raise PermissionError("errno 40000 (token inexistente)")

                    sd = (j.get("result") or {}).get("data") or {}
                    last_status = sd

                    # alarme simples por bits (sem mapear ainda)
                    if any_nonzero(sd.get("error1", [])) or any_nonzero(sd.get("error3", [])):
                        print("[STATUS] Alarme reportado pela EMBOX (error1/error3 != 0).")

                    comm_fails = 0
                except PermissionError:
                    print("[STATUS] Token inválido, relogando...")
                    token = None
                except Exception as e:
                    comm_fails += 1
                    print(f"[STATUS] Falha: {e} (fails={comm_fails})")

            # --------- RELOGIN se token caiu ----------
            if token is None and not fault:
                try:
                    token = login(s)
                    print("[AUTH] Relogado. TOKEN:", token)
                    comm_fails = 0
                except Exception as e:
                    comm_fails += 1
                    print("[AUTH] Falha ao relogar:", e)
                    time.sleep(1.0)
                    continue

            # --------- FAULT por comunicação ----------
            if comm_fails >= MAX_CONSECUTIVE_COMM_FAILS and not fault:
                fault = True
                print("\n*** FAULT: Comunicação instável (muitas falhas seguidas). ***\n")

            # --------- CONTROLE + ESCRITA ----------
            if now >= t_next_write and not fault and token is not None:
                t_next_write += WRITE_PERIOD_S

                # 1) Medição (converter sinal)
                raw_meter = last_data.get("meterPower", 0)
                meter_w = to_signed_meter_w(raw_meter)

                # 2) Erro vs alvo (positivo = está exportando mais do que o desejado)
                error_w = meter_w - float(TARGET_METER_W)

                # 3) Deadband
                if abs(error_w) <= DEADBAND_W:
                    # dentro da banda: não mexe; e também não acumula violação
                    violation_accum_s = 0.0
                    print(f"[CTRL] meter={meter_w:.0f}W target={TARGET_METER_W}W err={error_w:.0f}W (deadband)")
                    continue

                # 4) Temporizador de violação (persistência)
                if abs(error_w) >= VIOLATION_LIMIT_W:
                    violation_accum_s += WRITE_PERIOD_S
                else:
                    violation_accum_s = 0.0

                if violation_accum_s >= VIOLATION_TIME_S:
                    fault = True
                    print(f"\n*** FAULT: erro >= {VIOLATION_LIMIT_W}W por {VIOLATION_TIME_S}s (err={error_w:.0f}W). ***\n")
                    continue

                # 5) Calcular novo setpoint (heurística simples)
                # Ideia: se está exportando demais (error positivo), reduz o GridExportLimit.
                # se está importando demais (error negativo), pode aumentar o GridExportLimit (permitir mais geração).
                #
                # Como GridExportLimit é "limite de exportação", aumentar demais não força exportação,
                # só permite (teto). Ainda assim, isso influencia a lógica interna da EMBOX.
                step = clamp(error_w, -RAMP_W_PER_STEP, RAMP_W_PER_STEP)

                try:
                    cfg = get_config(s, token)
                    plc = cfg.get("POWER_LIMIT_CONTROL", {}) or {}
                    current_limit = float(plc.get("GridExportLimit", 0))

                    desired_limit = current_limit - step  # note o sinal: error positivo -> diminui limit
                    desired_limit = clamp(desired_limit, EXPORT_LIMIT_MIN_W, EXPORT_LIMIT_MAX_W)

                    # Evita escrita se mudança muito pequena
                    if last_write_limit is not None and abs(desired_limit - last_write_limit) < 1:
                        print(f"[CTRL] meter={meter_w:.0f}W err={error_w:.0f}W -> lim ~{desired_limit:.0f}W (skip)")
                        continue

                    plc["PowerLimit_Enable"] = True
                    plc["GridExportLimit"] = int(desired_limit)
                    cfg["POWER_LIMIT_CONTROL"] = plc

                    resp = set_config(s, token, cfg)
                    if resp.get("errno") != 0:
                        raise RuntimeError(f"set/configJson errno != 0: {resp}")

                    last_write_limit = desired_limit

                    print(f"[SET] meter={meter_w:.0f}W target={TARGET_METER_W}W err={error_w:.0f}W "
                          f"limit: {current_limit:.0f}->{desired_limit:.0f}W  viol={violation_accum_s:.1f}s")

                except PermissionError:
                    print("[SET] Token inválido, relogando...")
                    token = None
                except Exception as e:
                    comm_fails += 1
                    print(f"[SET] Falha: {e} (fails={comm_fails})")

            # Pequeno sleep para não 100% CPU
            time.sleep(0.02)
