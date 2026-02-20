#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import json
import hashlib
import requests

BASE_URL = "http://10.1.1.118"
USER = "admin"
PASSWORD = "Solar@123"
LANG = "enGB"

def md5_hex(s: str) -> str:
    return hashlib.md5(s.encode("utf-8")).hexdigest()

def login_get_token(session: requests.Session) -> str:
    url = f"{BASE_URL}/sems/user/login"

    # O navegador envia o body como JSON-string, mesmo com content-type "form-urlencoded"
    body = json.dumps({"user": USER, "password": md5_hex(PASSWORD)})

    headers = {
        "accept": "application/json, text/plain, */*",
        "content-type": "application/x-www-form-urlencoded;charset=UTF-8",
        "x-requested-with": "XMLHttpRequest",
        "lang": LANG,
        "token": "none",
        # Esses dois costumam ser o “pulo do gato” quando o backend valida origem:
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
        "cache-control": "no-cache",
        "pragma": "no-cache",
    }

    r = session.post(url, headers=headers, data=body, timeout=8)

    # Debug útil
    ct = r.headers.get("Content-Type", "")
    if "application/json" not in ct:
        raise RuntimeError(f"Resposta não-JSON no login. HTTP {r.status_code}. CT={ct}. Body: {r.text[:300]}")

    j = r.json()
    if j.get("errno") != 0:
        raise RuntimeError(f"Login falhou errno={j.get('errno')} msg={j.get('msg')!r} resp={j}")

    token = (j.get("result") or {}).get("token")
    if not token:
        raise RuntimeError(f"Token não veio na resposta: {j}")

    return token

def logout_best_effort(session: requests.Session, token: str) -> None:
    # Não sabemos o endpoint certo ainda; tentamos alguns e se não achar, ok.
    candidates = [
        "/sems/user/logout",
        "/sems/user/logOut",
        "/sems/logout",
    ]
    headers = {
        "accept": "application/json, text/plain, */*",
        "x-requested-with": "XMLHttpRequest",
        "lang": LANG,
        "token": token,
        "origin": BASE_URL,
        "referer": f"{BASE_URL}/",
    }
    for p in candidates:
        try:
            r = session.post(f"{BASE_URL}{p}", headers=headers, timeout=6)
            if r.status_code == 404:
                continue
            # Se existir, normalmente vem JSON errno=0
            print(f"Logout tentativa {p}: HTTP {r.status_code}")
            return
        except requests.RequestException:
            continue
    print("Logout: endpoint não encontrado (normal).")

def main() -> int:
    with requests.Session() as s:
        token = login_get_token(s)
        print("TOKEN:", token)
        logout_best_effort(s, token)
    return 0

if __name__ == "__main__":
    sys.exit(main())
