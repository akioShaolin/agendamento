import json
import hashlib
import requests

BASE_URL = "http://10.1.1.118"
USER = "admin"
PASSWORD = "Solar@123"
LANG = "enGB"

EXPORT_LIMIT_W = 0  # <-- ajuste aqui (em W). Ex: 74000 = 74 kW

def md5_hex(s: str) -> str:
    return hashlib.md5(s.encode("utf-8")).hexdigest()

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
    }

    r = session.post(url, headers=headers, data=body, timeout=8)
    r.raise_for_status()
    j = r.json()
    if j.get("errno") != 0:
        raise RuntimeError(f"Login falhou: {j}")
    return j["result"]["token"]

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
    r = session.get(url, headers=headers, timeout=8)
    r.raise_for_status()
    j = r.json()
    if j.get("errno") != 0:
        raise RuntimeError(f"get/configJson falhou: {j}")
    if not isinstance(j.get("result"), dict):
        raise RuntimeError(f"Formato inesperado em get/configJson: {j}")
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

    r = session.post(url, headers=headers, data=payload, timeout=10)
    r.raise_for_status()
    j = r.json()
    return j

def logout(session: requests.Session, token: str) -> None:
    url = f"{BASE_URL}/sems/user/logout"
    headers = {
        "accept": "application/json, text/plain, */*",
        "lang": LANG,
        "token": token,
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
    }
    session.post(url, headers=headers, timeout=8)

if __name__ == "__main__":
    with requests.Session() as s:
        token = login(s)
        print("TOKEN:", token)

        cfg = get_config(s, token)
        plc = cfg.get("POWER_LIMIT_CONTROL", {})
        old = plc.get("GridExportLimit")

        plc["PowerLimit_Enable"] = True
        plc["GridExportLimit"] = int(EXPORT_LIMIT_W)
        cfg["POWER_LIMIT_CONTROL"] = plc

        resp = set_config(s, token, cfg)
        print("SET response:", json.dumps(resp, indent=2, ensure_ascii=False))

        # Confirmação
        cfg2 = get_config(s, token)
        new = (cfg2.get("POWER_LIMIT_CONTROL", {}) or {}).get("GridExportLimit")
        print(f"CONFIRM: GridExportLimit {old} -> {new}")

        logout(s, token)
        print("Logout: OK (tentado)")
