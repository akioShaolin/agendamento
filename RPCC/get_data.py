import json
import hashlib
import requests

BASE_URL = "http://10.1.1.118"
USER = "admin"
PASSWORD = "Solar@123"
LANG = "enGB"

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

def get_data(session: requests.Session, token: str) -> dict:
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
    return r.json()

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

        data = get_data(s, token)
        print("DATA JSON:")
        print(json.dumps(data, indent=2, ensure_ascii=False))

        logout(s, token)
        print("Logout: OK (tentado)")
