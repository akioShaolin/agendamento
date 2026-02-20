import json
import time
import hashlib
import requests
import signal
import sys

# =========================
# AJUSTES RÁPIDOS (TOPO)
# =========================

BASE_URL = "http://10.1.1.118"
USER = "admin"
PASSWORD = "Solar@123"
LANG = "enGB"

# ===== ALARME HARDWARE (futuro relé) =====
# Dispara se TARGET negativo e mesmo com GridExportLimit=0 a rede continua EXPORTANDO (fora de controle)
ENABLE_OOC_ALARM = True

OOC_EXPORT_THRESHOLD_W = 500     # exportação acima disso é "fora de controle" (ajuste)
OOC_TIME_S = 10.0                # precisa durar X s para disparar

# flag de alarme (mostra no terminal; futuramente aciona relé)

# ===== SAFE MODE (quando houver falha/violação) =====
ENABLE_SAFE_MODE = True

# Em safe mode, aplicar novamente o limite a cada X s (para garantir)
SAFE_APPLY_PERIOD_S = 5.0

# Margem opcional (W) quando target for positivo (evita ficar raspando)
SAFE_EXPORT_MARGIN_W = 0

# SAFE MODE sai quando ficar em deadband por X segundos
SAFE_EXIT_DEADBAND_TIME_S = 10.0

# Tempos (segundos)
READ_DATA_PERIOD_S   = 0.25   # leitura /sems/overview/data
READ_STATUS_PERIOD_S = 5.0    # leitura /sems/overview/status
WRITE_PERIOD_S       = 0.5    # escrita do setpoint (GridExportLimit)
RESYNC_CONFIG_PERIOD_S = 60.0 # (opcional) ressincroniza configJson de tempos em tempos

# Controle do alvo no medidor (W) — pode ser NEGATIVO
TARGET_METER_W = 25000

# Interpretação do sinal do meterPower
# True  -> meterPower positivo significa EXPORTAÇÃO (para rede)
# False -> meterPower positivo significa IMPORTAÇÃO (da rede)
METER_POSITIVE_IS_EXPORT = True

# Limites do setpoint (W)
EXPORT_LIMIT_MIN_W = 0
EXPORT_LIMIT_MAX_W = 200_000

# Deadband (W)
DEADBAND_W = 200

# Rampa (W por escrita)
RAMP_W_PER_STEP = 10000

# Violação persistente -> fault
VIOLATION_LIMIT_W = 3000
VIOLATION_TIME_S  = 10.0

# Comunicação
MAX_CONSECUTIVE_COMM_FAILS = 5

# ====== Novas flags / políticas ======
ENABLE_GRACEFUL_LOGOUT_ON_EXIT = True

# Se token cair (errno 40000 / HTML), reloga automaticamente
ENABLE_AUTO_RELOGIN = True

# Se ficar em condição de "relogin falhando" (ou servidor rejeitando),
# entra em modo OFFLINE, e tenta voltar só a cada OFFLINE_RETRY_S.
OFFLINE_RETRY_S = 30.0

# Mostrar idade do token (tempo desde login) no console
SHOW_TOKEN_AGE = True

# Mostrar aviso quando entrar/sair de OFFLINE
SHOW_OFFLINE_WARNINGS = True

# =========================
# FIM DOS AJUSTES
# =========================

def read_meter_w_from_your_source() -> float:
    # FUTURO: aqui entra Modbus RTU/TCP do seu medidor real
    # Por enquanto, usa last_data["meterPower"] (EMBOX)
    return to_signed_meter_w(last_data.get("meterPower", 0))

def md5_hex(s: str) -> str:
    return hashlib.md5(s.encode("utf-8")).hexdigest()

def is_json_response(r: requests.Response) -> bool:
    return "application/json" in (r.headers.get("Content-Type") or "")

def clamp(x: float, lo: float, hi: float) -> float:
    return lo if x < lo else hi if x > hi else x

def any_nonzero(arr) -> bool:
    try:
        return any(int(x) != 0 for x in arr)
    except Exception:
        return False

def to_signed_meter_w(raw_meter) -> float:
    """+ = exportação; - = importação"""
    raw = float(raw_meter or 0.0)
    return raw if METER_POSITIVE_IS_EXPORT else -raw

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

def logout(session: requests.Session, token: str) -> None:
    url = f"{BASE_URL}/sems/user/logout"
    headers = {
        "accept": "application/json, text/plain, */*",
        "lang": LANG,
        "token": token,
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
    }
    try:
        session.post(url, headers=headers, timeout=6)
    except Exception:
        pass

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

def is_token_invalid_errno(j: dict) -> bool:
    return isinstance(j, dict) and j.get("errno") == 40000

# ============================================================
# ATUADOR (HOJE = GridExportLimit). TROCAR AQUI NO FUTURO.
# Objetivo futuro: em vez de escrever GridExportLimit, escrever
# um setpoint real de potência total do sistema (a descobrir).
# ============================================================

def actuator_get(cfg_cache: dict) -> float:
    """(ATUADOR ATUAL = GridExportLimit) TROCAR AQUI NO FUTURO."""
    plc = cfg_cache.get("POWER_LIMIT_CONTROL", {}) or {}
    return float(plc.get("GridExportLimit", 0))

def actuator_set(cfg_cache: dict, value_w: int) -> None:
    """(ATUADOR ATUAL = GridExportLimit) TROCAR AQUI NO FUTURO."""
    plc = cfg_cache.get("POWER_LIMIT_CONTROL", {}) or {}
    plc["PowerLimit_Enable"] = True
    plc["GridExportLimit"] = int(value_w)
    cfg_cache["POWER_LIMIT_CONTROL"] = plc

def compute_safe_export_limit(target_meter_w: float) -> int:
    """
    target_meter_w: + = exportar, - = importar (peak shaving)
    SAFE MODE:
      - se target >= 0: export limit = target (+ margem)
      - se target < 0:  export limit = 0 (corta exportação)
    """
    if target_meter_w is None:
        target_meter_w = 0.0

    if target_meter_w >= 0:
        val = int(target_meter_w + SAFE_EXPORT_MARGIN_W)
    else:
        val = 0

    return int(clamp(val, EXPORT_LIMIT_MIN_W, EXPORT_LIMIT_MAX_W))

# =========================
# LOOP PRINCIPAL
# =========================

if __name__ == "__main__":
    stop_requested = False

    def _handle_sigint(sig, frame):
        global stop_requested
        stop_requested = True
        print("\n[EXIT] Ctrl+C recebido. Encerrando com segurança...")

    signal.signal(signal.SIGINT, _handle_sigint)

    with requests.Session() as s:
        token = None
        token_issued_at = None

        # Estado OFFLINE e retry de 30s
        offline = False
        offline_since = None
        next_offline_retry = 0.0

        # out of control
        ooc_alarm = False
        ooc_accum_s = 0.0
        ooc_clear_accum_s = 0.0

        # safe mode
        safe_mode = False
        safe_reason = ""
        t_next_safe_apply = 0.0
        safe_clear_accum_s = 0.0

        comm_fails = 0
        fault = False

        last_data = {}
        last_status = {}
        cfg_cache = None
        last_cfg_sync = 0.0

        last_write_limit = None
        violation_accum_s = 0.0

        t_next_data = 0.0
        t_next_status = 0.0
        t_next_write = 0.0

        current_limit = 0

        def enter_offline(reason: str):
            global offline, offline_since, next_offline_retry
            if not offline:
                offline = True
                offline_since = time.time()
                next_offline_retry = offline_since  # tenta já na próxima oportunidade
                if SHOW_OFFLINE_WARNINGS:
                    print(f"\n[OFFLINE] Entrou em OFFLINE: {reason}\n")

        def exit_offline():
            global offline, offline_since
            if offline:
                offline = False
                if SHOW_OFFLINE_WARNINGS:
                    dur = time.time() - (offline_since or time.time())
                    print(f"\n[OFFLINE] Saiu de OFFLINE (ficou {dur:.0f}s).\n")
                offline_since = None

        def ensure_login():
            global token, token_issued_at, cfg_cache, last_cfg_sync, comm_fails
            if token is not None:
                return True

            if not ENABLE_AUTO_RELOGIN:
                return False

            try:
                token = login(s)
                token_issued_at = time.time()
                comm_fails = 0
                cfg_cache = None
                last_cfg_sync = 0.0
                print("[AUTH] Logado. TOKEN:", token)
                return True
            except Exception as e:
                comm_fails += 1
                enter_offline(f"Falha ao logar: {e}")
                return False

        # Login inicial
        ensure_login()

        now = time.time()
        t_next_data = now
        t_next_status = now
        t_next_write = now

        print("Control loop iniciado.")

        while not stop_requested:
            now = time.time()

            # Se em OFFLINE, só tenta recuperar a cada 30s
            if offline:
                if now >= next_offline_retry:
                    next_offline_retry = now + OFFLINE_RETRY_S
                    print(f"[OFFLINE] Tentando reconectar (a cada {OFFLINE_RETRY_S}s)...")
                    token = None
                    if ensure_login():
                        exit_offline()
                time.sleep(0.05)
                continue

            # Mostrar idade do token (periodicamente)
            if SHOW_TOKEN_AGE and token_issued_at is not None:
                # imprime a cada ~60s
                if int(now) % 60 == 0:
                    age_min = (now - token_issued_at) / 60.0
                    # evita spam imprimindo repetido no mesmo segundo
                    if getattr(sys, "_last_age_print", None) != int(now):
                        sys._last_age_print = int(now)
                        print(f"[AUTH] Token age: {age_min:.1f} min")

            # --------- DATA ----------
            if now >= t_next_data and not fault:
                t_next_data += READ_DATA_PERIOD_S
                if token is None and not ensure_login():
                    time.sleep(0.02)
                    continue

                try:
                    j = get_overview_data(s, token)
                    if is_token_invalid_errno(j):
                        raise PermissionError("errno 40000 (token inválido)")
                    last_data = (j.get("result") or {}).get("data") or {}
                    comm_fails = 0
                except PermissionError:
                    print("[DATA] Token inválido. Re-logando...")
                    token = None
                    if not ensure_login():
                        # entra em OFFLINE e tenta voltar em 30s
                        next_offline_retry = time.time() + OFFLINE_RETRY_S
                except Exception as e:
                    comm_fails += 1
                    print(f"[DATA] Falha: {e} (fails={comm_fails})")

            # --------- STATUS ----------
            if now >= t_next_status and not fault:
                t_next_status += READ_STATUS_PERIOD_S
                if token is None and not ensure_login():
                    time.sleep(0.02)
                    continue

                try:
                    j = get_overview_status(s, token)
                    if is_token_invalid_errno(j):
                        raise PermissionError("errno 40000 (token inválido)")
                    last_status = (j.get("result") or {}).get("data") or {}

                    if any_nonzero(last_status.get("error1", [])) or any_nonzero(last_status.get("error3", [])):
                        print("[STATUS] Alarme reportado (error1/error3 != 0).")

                    comm_fails = 0
                except PermissionError:
                    print("[STATUS] Token inválido. Re-logando...")
                    token = None
                    if not ensure_login():
                        next_offline_retry = time.time() + OFFLINE_RETRY_S
                except Exception as e:
                    comm_fails += 1
                    print(f"[STATUS] Falha: {e} (fails={comm_fails})")

            # --------- Fault por comunicação ----------
            if comm_fails >= MAX_CONSECUTIVE_COMM_FAILS and not fault:
                fault = True
                print("\n*** FAULT: Comunicação instável (muitas falhas seguidas). ***\n")
                enter_offline("FAULT por comunicação (aguardando retry).")
                next_offline_retry = time.time() + OFFLINE_RETRY_S
                continue

            # -------------- Safemode ----------------
            if safe_mode:
                # 1) checar se já voltou ao normal (deadband) para sair do SAFE MODE
                meter_w = read_meter_w_from_your_source()
                error_w = meter_w - float(TARGET_METER_W)

                if abs(error_w) <= DEADBAND_W:
                    safe_clear_accum_s += READ_DATA_PERIOD_S
                else:
                    safe_clear_accum_s = 0.0

                if safe_clear_accum_s >= SAFE_EXIT_DEADBAND_TIME_S:
                    print(f"\n[SAFE] Saindo do SAFE MODE (deadband por {SAFE_EXIT_DEADBAND_TIME_S}s).\n")
                    safe_mode = False
                    safe_reason = ""
                    safe_clear_accum_s = 0.0
                    # volta pro controle normal

                # Se saiu do SAFE MODE, NÃO deve continuar pulando o controle normal
                if not safe_mode:
                    # cai fora deste bloco e permite o controle normal rodar no mesmo ciclo
                    # (não dá continue)
                    pass
                else:
                    if now >= t_next_safe_apply:
                        t_next_safe_apply = now + SAFE_APPLY_PERIOD_S

                        # garante token
                        if token is None and not ensure_login():
                            # se não conseguir logar, só tenta de novo no próximo SAFE_APPLY_PERIOD_S
                            continue

                        try:
                            # usa config cache (ou get_config) e aplica limite seguro
                            if cfg_cache is None or (now - last_cfg_sync) >= RESYNC_CONFIG_PERIOD_S:
                                cfg_cache = get_config(s, token)
                                last_cfg_sync = now

                            current_limit = actuator_get(cfg_cache)  # ATUADOR ATUAL (trocar no futuro)

                            safe_limit = compute_safe_export_limit(float(TARGET_METER_W))

                            # só escreve se diferente
                            if abs(current_limit - safe_limit) >= 1:
                                actuator_set(cfg_cache, safe_limit)  # ATUADOR ATUAL (trocar no futuro)

                                resp = set_config(s, token, cfg_cache)
                                if resp.get("errno") != 0:
                                    raise RuntimeError(f"set/configJson falhou: {resp}")

                                print(f"[SAFE] Aplicado GridExportLimit {current_limit:.0f} -> {safe_limit} W (target={TARGET_METER_W}W)")

                            # continua monitorando o medidor (leitura já está acontecendo no loop)
                            # Saída do SAFE MODE (opcional): se o erro voltar pra dentro do limite por um tempo
                            # (se você quiser, a gente implementa depois)

                        except Exception as e:
                            print("[SAFE] Falha ao aplicar safe limit:", e)

                # Em SAFE MODE a gente NÃO roda o controle normal
                if safe_mode:
                    time.sleep(0.02)
                    continue

            # --------- Escrita / Controle ----------
            if now >= t_next_write and not fault:
                t_next_write += WRITE_PERIOD_S          
                if token is None and not ensure_login():
                    time.sleep(0.02)
                    continue

                # sinal coerente do medidor
                meter_w = read_meter_w_from_your_source()
                error_w = meter_w - float(TARGET_METER_W)

                # --- carregar config/cache para ter current_limit válido neste ciclo ---
                if cfg_cache is None or (now - last_cfg_sync) >= RESYNC_CONFIG_PERIOD_S:
                    cfg_cache = get_config(s, token)
                    last_cfg_sync = now

                current_limit = actuator_get(cfg_cache)  # ATUADOR ATUAL (trocar no futuro)

                # --- SATURAÇÃO DO ATUADOR (não é falha) ---
                need_increase = (error_w < 0)  # meter abaixo do target -> tenta aumentar limite
                need_decrease = (error_w > 0)  # meter acima do target -> tenta reduzir limite

                at_max = current_limit >= (EXPORT_LIMIT_MAX_W - 0.5)
                at_min = current_limit <= (EXPORT_LIMIT_MIN_W + 0.5)

                if need_increase and at_max:
                    # Não dá pra aumentar mais. Não conta violação.
                    violation_accum_s = 0.0
                    print(f"[SAT] limit=MAX ({current_limit:.0f}W) | meter={meter_w:.0f}W target={TARGET_METER_W}W err={error_w:.0f}W -> saturado, aguardando")
                    continue

                if need_decrease and at_min:
                    violation_accum_s = 0.0
                    print(f"[SAT] limit=MIN ({current_limit:.0f}W) | meter={meter_w:.0f}W target={TARGET_METER_W}W err={error_w:.0f}W -> saturado, aguardando")
                    continue

                # ============================================================
                # CASO ESPECIAL: TARGET negativo + atuador atual (GridExportLimit) em 0
                # - Não tenta "perseguir" target negativo com GridExportLimit (não controla carga)
                # - Mas: se mesmo assim estiver EXPORTANDO, é geração fora de controle (terceiros)
                #   => levantar ooc_alarm (futuro relé) e NÃO entrar em Safe Mode
                # ============================================================

                if float(TARGET_METER_W) < 0 and current_limit <= (EXPORT_LIMIT_MIN_W + 0.5):
                    # Se houver exportação (meter_w > 0), acumula tempo de condição fora de controle
                    if ENABLE_OOC_ALARM and meter_w > OOC_EXPORT_THRESHOLD_W:
                        ooc_accum_s += WRITE_PERIOD_S
                    else:
                        ooc_accum_s = 0.0

                    if ENABLE_OOC_ALARM and (ooc_accum_s >= OOC_TIME_S):
                        if not ooc_alarm:
                            ooc_alarm = True
                            print(f"\n[ALARM] OOC_ALARM=ON (exportando {meter_w:.0f}W com GridExportLimit=0 por {OOC_TIME_S}s). "
                                f"Isso indica geração fora de controle.\n")

                    # Não é falha “de controle” neste modo; não entra em Safe Mode; zera violação
                    violation_accum_s = 0.0

                    print(f"[CTRL] OOC_ALARM={'ON' if ooc_alarm else 'OFF'} | "
                        f"target={TARGET_METER_W}W | GridExportLimit=0 | meter={meter_w:.0f}W -> monitorando (sem ação)")
                    continue

                # Deadband
                if abs(error_w) <= DEADBAND_W:
                    violation_accum_s = 0.0
                    print(f"[CTRL] meter={meter_w:.0f}W target={TARGET_METER_W}W err={error_w:.0f}W (deadband)")
                    continue

                # Temporizador de violação (persistência)
                if abs(error_w) >= VIOLATION_LIMIT_W:
                    violation_accum_s += WRITE_PERIOD_S
                else:
                    violation_accum_s = 0.0

                if violation_accum_s >= VIOLATION_TIME_S:
                    if ENABLE_SAFE_MODE:
                        safe_mode = True
                        safe_reason = f"violação persistente (err={error_w:.0f}W por {VIOLATION_TIME_S}s)"
                        print(f"\n[SAFE] Entrando em SAFE MODE: {safe_reason}\n")
                        # zera contador pra não ficar reentrando toda hora
                        violation_accum_s = 0.0
                        # força aplicar já
                        t_next_safe_apply = 0.0
                        continue
                    else:
                        fault = True
                        print(f"\n*** FAULT: erro >= {VIOLATION_LIMIT_W}W por {VIOLATION_TIME_S}s (err={error_w:.0f}W). ***\n")
                        continue

                # Resync config cache (para não dar drift ou se houver mudanças externas)
                try:
                    if cfg_cache is None or (now - last_cfg_sync) >= RESYNC_CONFIG_PERIOD_S:
                        cfg_cache = get_config(s, token)
                        last_cfg_sync = now

                    current_limit = actuator_get(cfg_cache)  # ATUADOR ATUAL (trocar no futuro)

                    # Rampa aplicada no ajuste
                    step = clamp(error_w, -RAMP_W_PER_STEP, RAMP_W_PER_STEP)

                    # Regra: erro positivo (exporta mais que alvo) -> reduzir limite.
                    desired_limit = current_limit - step
                    desired_limit = clamp(desired_limit, EXPORT_LIMIT_MIN_W, EXPORT_LIMIT_MAX_W)

                    # Evitar escrita por mudança mínima
                    if last_write_limit is not None and abs(desired_limit - last_write_limit) < 1:
                        print(f"[CTRL] meter={meter_w:.0f}W err={error_w:.0f}W -> lim ~{desired_limit:.0f}W (skip)")
                        continue

                    actuator_set(cfg_cache, int(desired_limit))  # ATUADOR ATUAL (trocar no futuro)

                    resp = set_config(s, token, cfg_cache)
                    if is_token_invalid_errno(resp):
                        raise PermissionError("errno 40000 (token inválido) em set")
                    if resp.get("errno") != 0:
                        raise RuntimeError(f"set/configJson errno != 0: {resp}")

                    last_write_limit = desired_limit

                    print(f"[SET] meter={meter_w:.0f}W target={TARGET_METER_W}W err={error_w:.0f}W "
                          f"limit: {current_limit:.0f}->{desired_limit:.0f}W  viol={violation_accum_s:.1f}s")

                    comm_fails = 0

                except PermissionError:
                    print("[SET] Token inválido. Re-logando...")
                    token = None
                    if not ensure_login():
                        next_offline_retry = time.time() + OFFLINE_RETRY_S
                except Exception as e:
                    comm_fails += 1
                    print(f"[SET] Falha: {e} (fails={comm_fails})")

            time.sleep(0.02)

        # ======= Encerramento seguro =======
        if ENABLE_GRACEFUL_LOGOUT_ON_EXIT and token is not None:
            print("[EXIT] Fazendo logout...")
            logout(s, token)
            print("[EXIT] Logout enviado.")
        print("[EXIT] Encerrado.")
