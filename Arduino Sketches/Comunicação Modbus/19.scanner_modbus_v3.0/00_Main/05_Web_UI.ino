#include <Arduino.h>

const char HTML_PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Scanner Modbus</title>
<style>
  :root{
    --bg:#0b1220; --text:#e9eefc; --muted:#a9b6d3;
    --primary:#4ea1ff; --primary2:#2b74ff; --border:rgba(255,255,255,.10);
    --ok:#00e676; --warn:#ffca28; --bad:#ff5252; --console:#05070d; --radius:16px;
  }
  *{ box-sizing:border-box; }
  body{ margin:0; font-family:system-ui,Arial; color:var(--text);
        background: radial-gradient(1200px 700px at 20% 0%, #152a55 0%, var(--bg) 50%, #070b14 100%);
        padding:18px; }
  .wrap{ max-width:980px; margin:0 auto; display:grid; grid-template-columns:1.15fr .85fr; gap:14px; }
  @media(max-width:900px){ .wrap{ grid-template-columns:1fr; } }
  .topbar{ grid-column:1/-1; display:flex; justify-content:space-between; gap:12px; align-items:center;
           padding:14px 16px; background:rgba(255,255,255,.05); border:1px solid var(--border); border-radius:var(--radius); }
  .title h1{ margin:0; font-size:18px; } .title p{ margin:0; font-size:12px; color:var(--muted); }
  .pill{ padding:6px 10px; border-radius:999px; border:1px solid var(--border); color:var(--muted); font-size:12px; }
  .card{ background:rgba(255,255,255,.04); border:1px solid var(--border); border-radius:var(--radius); overflow:hidden; }
  .cardHead{ padding:12px 14px; border-bottom:1px solid var(--border); display:flex; justify-content:space-between; gap:10px; align-items:center; }
  .cardHead h2{ margin:0; font-size:14px; }
  .cardBody{ padding:14px; }
  .tabs{ display:flex; gap:8px; flex-wrap:wrap; }
  .tabBtn{ border:1px solid var(--border); background:rgba(255,255,255,.03); color:var(--muted);
           padding:9px 12px; border-radius:12px; cursor:pointer; font-size:13px; }
  .tabBtn.active{ background:linear-gradient(180deg, rgba(78,161,255,.25), rgba(43,116,255,.15)); color:var(--text); }
  .grid2{ display:grid; grid-template-columns:1fr 1fr; gap:10px; }
  @media(max-width:520px){ .grid2{ grid-template-columns:1fr; } }
  label{ font-size:12px; color:var(--muted); display:block; margin-bottom:6px; }
  input,select{ width:100%; padding:10px; border-radius:12px; border:1px solid var(--border); background:rgba(0,0,0,.25); color:var(--text); }
  .btnRow{ display:flex; gap:10px; flex-wrap:wrap; margin-top:12px; }
  .btn{ border:0; cursor:pointer; padding:10px 14px; border-radius:12px; font-weight:700; font-size:13px;
        color:#08101f; background:linear-gradient(180deg,var(--primary),var(--primary2)); }
  .btn.secondary{ color:var(--text); background:rgba(255,255,255,.06); border:1px solid var(--border); }
  .hint{ font-size:12px; color:var(--muted); margin-top:10px; line-height:1.35; }
  .mono{ font-family: ui-monospace, Menlo, Consolas, monospace; }
  table{ width:100%; border-collapse:collapse; border:1px solid var(--border); border-radius:12px; overflow:hidden; background:rgba(0,0,0,.22); }
  th,td{ padding:10px; border-bottom:1px solid rgba(255,255,255,.08); font-size:13px; text-align:left; }
  th{ color:var(--muted); font-size:12px; }
  tr:last-child td{ border-bottom:none; }
  .badge{ display:inline-flex; padding:4px 8px; border-radius:999px; border:1px solid var(--border); font-size:12px; }
  .badge.ok{ color:var(--ok); border-color:rgba(0,230,118,.35); background:rgba(0,230,118,.08); }
  .badge.warn{ color:var(--warn); border-color:rgba(255,202,40,.35); background:rgba(255,202,40,.08); }
  .badge.bad{ color:var(--bad); border-color:rgba(255,82,82,.35); background:rgba(255,82,82,.08); }
  #console{ height:320px; overflow:auto; white-space:pre-wrap; background:var(--console);
            border:1px solid rgba(255,255,255,.10); border-radius:12px; padding:10px; font-size:12px; color:#cfe3ff; }
  .lastOut{ padding:10px 12px; border-radius:12px; border:1px solid var(--border); background:rgba(0,0,0,.22); min-height:130px; }
</style>
</head>
<body>
  <div class="wrap">
    <div class="topbar">
      <div class="title">
        <h1>Scanner Modbus</h1>
        <p>AP local • WebSocket • <a href="/wifi" style="color:var(--muted)">WiFi</a></p>
      </div>
      <div class="pill mono" id="wsStatus">WS: desconectado</div>
    </div>

    <div class="card">
      <div class="cardHead">
        <h2>Operações</h2>
        <div class="tabs">
          <button class="tabBtn active" data-tab="tabScan">Procurar Slaves</button>
          <button class="tabBtn" data-tab="tabRead">Ler (1 item)</button>
          <button class="tabBtn" data-tab="tabWrite">Escrever</button>
        </div>
      </div>

      <div class="cardBody">

        <!-- TAB 1: Scan -->
        <div id="tabScan" class="tab">
          <div class="grid2">
            <div><label>ID inicial</label><input id="scanIdStart" type="number" min="1" max="247" value="1"></div>
            <div><label>ID final</label><input id="scanIdEnd" type="number" min="1" max="247" value="247"></div>
            <div><label>Registrador teste (ping)</label><input id="scanTestReg" type="number" min="0" max="65535" value="0"></div>
            <div>
              <label>Função teste</label>
              <select id="scanTestFn">
                <option value="3" selected>03 - Holding Register</option>
                <option value="4">04 - Input Register</option>
                <option value="1">01 - Coils</option>
                <option value="2">02 - Discrete Inputs</option>
              </select>
            </div>
          </div>

          <div class="btnRow">
            <button class="btn" onclick="scanStart()">Iniciar busca</button>
            <button class="btn secondary" onclick="scanStop()">Parar</button>
          </div>

          <div class="hint" id="scanStatus">Aguardando...</div>

          <div style="margin-top:14px;">
            <table>
              <thead><tr><th style="width:80px;">ID</th><th>Status</th><th>Detalhe</th></tr></thead>
              <tbody id="foundTable">
                <tr><td class="mono">-</td><td><span class="badge warn">vazio</span></td><td class="mono">---</td></tr>
              </tbody>
            </table>
          </div>
        </div>

        <!-- TAB 2: Read once -->
        <div id="tabRead" class="tab" style="display:none;">
          <div class="grid2">
            <div><label>ID do escravo</label><input id="readSlaveId" type="number" min="1" max="247" value="1"></div>
            <div>
              <label>Função</label>
              <select id="readFn">
                <option value="1">01 - Coils</option>
                <option value="2">02 - Discrete Inputs</option>
                <option value="3" selected>03 - Holding Register</option>
                <option value="4">04 - Input Register</option>
              </select>
            </div>
            <div><label>Endereço (offset)</label><input id="readAddr" type="number" min="0" max="65535" value="0"></div>
            <div>
              <label>&nbsp;</label>
              <button class="btn" onclick="readOnce()">Ler agora</button>
            </div>
          </div>

          <div class="btnRow">
            <button class="btn secondary" onclick="readStop()">Parar</button>
          </div>

          <div class="hint" id="readStatus">Aguardando...</div>

          <div style="margin-top:14px;">
            <table>
              <thead><tr><th>Tipo</th><th>Addr</th><th>Valor</th><th>Hex</th></tr></thead>
              <tbody id="readTable">
                <tr><td class="mono">-</td><td class="mono">-</td><td class="mono">-</td><td class="mono">-</td></tr>
              </tbody>
            </table>
          </div>
        </div>

        <!-- TAB 3: Write placeholder -->
        <div id="tabWrite" class="tab" style="display:none;">
          <div class="hint">Parte 3 depois 🙂</div>
        </div>

      </div>
    </div>

    <div class="card">
      <div class="cardHead">
        <h2>Saídas</h2>
        <button class="btn secondary" onclick="clearConsole()">Limpar console</button>
      </div>
      <div class="cardBody">
        <div class="lastOut mono" id="lastOutput">Nenhuma operação executada.</div>
        <div style="margin-top:12px;" id="console" class="mono"></div>
      </div>
    </div>
  </div>

<script>
  // tabs
  document.querySelectorAll('.tabBtn').forEach(btn=>{
    btn.addEventListener('click', ()=>{
      document.querySelectorAll('.tabBtn').forEach(b=>b.classList.remove('active'));
      btn.classList.add('active');
      const tabId = btn.dataset.tab;
      document.querySelectorAll('.tab').forEach(t=>t.style.display='none');
      document.getElementById(tabId).style.display='block';
    });
  });

  // console
  const consoleDiv = document.getElementById('console');
  function addLine(text){ const d=document.createElement('div'); d.textContent=text; consoleDiv.appendChild(d); consoleDiv.scrollTop=consoleDiv.scrollHeight; }
  function clearConsole(){ consoleDiv.innerHTML=""; }

  function setLastOutput(t){ document.getElementById('lastOutput').textContent = t; }

  // found table
  const foundTable = document.getElementById('foundTable');
  let found = {};
  function renderFound(){
    foundTable.innerHTML="";
    const ids = Object.keys(found).map(n=>parseInt(n)).sort((a,b)=>a-b);
    if(ids.length===0){
      foundTable.innerHTML = `<tr><td class="mono">-</td><td><span class="badge warn">vazio</span></td><td class="mono">---</td></tr>`;
      return;
    }
    for(const id of ids){
      const item = found[id];
      const cls = item.status==="RESP" ? "ok" : (item.status==="EXC" ? "warn" : "bad");
      const tr = document.createElement('tr');
      tr.innerHTML = `<td class="mono">${id}</td><td><span class="badge ${cls}">${item.status}</span></td><td class="mono">${item.detail||""}</td>`;
      foundTable.appendChild(tr);
    }
  }

  // read table
  const readTable = document.getElementById('readTable');
  function renderReadResult(kind, addr, value, hex){
    readTable.innerHTML = `<tr>
      <td class="mono">${kind}</td>
      <td class="mono">${addr}</td>
      <td class="mono">${value}</td>
      <td class="mono">${hex||"-"}</td>
    </tr>`;
  }

  // WS (host atual)
  const ws = new WebSocket(`ws://${location.hostname}:81/`);
  const wsStatus = document.getElementById('wsStatus');

  ws.onopen  = () => { wsStatus.textContent="WS: conectado"; addLine("[browser] WS conectado"); };
  ws.onerror = () => { wsStatus.textContent="WS: erro"; addLine("[browser] WS erro"); };
  ws.onclose = () => { wsStatus.textContent="WS: desconectado"; addLine("[browser] WS desconectado"); };

  ws.onmessage = (e) => {
    const msg = String(e.data);

    if(msg.startsWith("LOG: ")){ addLine(msg.substring(5)); return; }
    if(msg.startsWith("ERR: ")){ addLine(msg.substring(5)); return; }

    if(msg.startsWith("EVT: ")){
      let d=null;
      try { d=JSON.parse(msg.substring(5)); } catch(err){ addLine("EVT JSON inválido"); return; }

      if(d.type==="scan-status"){
        document.getElementById('scanStatus').textContent = d.text || "status";
        setLastOutput(d.lastOutput || "scan...");
      }
      if(d.type==="slave-found"){
        found[d.id] = { status: d.status, detail: d.detail };
        renderFound();
      }

      if(d.type==="read-status"){
        document.getElementById('readStatus').textContent = d.text || "status";
        setLastOutput(d.lastOutput || "read...");
      }
      if(d.type==="read-result"){
        if(d.kind==="bit") renderReadResult("bit", d.addr, d.value, "-");
        else renderReadResult("reg", d.addr, d.value, d.hex || "-");
        setLastOutput(`Read OK: id=${d.id} fn=${d.fn} addr=${d.addr}`);
      }
    }
  };

  // actions
  function scanStart(){
    const idStart = parseInt(document.getElementById('scanIdStart').value)||1;
    const idEnd   = parseInt(document.getElementById('scanIdEnd').value)||247;
    const testReg = parseInt(document.getElementById('scanTestReg').value)||0;
    const testFn  = parseInt(document.getElementById('scanTestFn').value)||3;

    found = {}; renderFound();
    ws.send(JSON.stringify({action:"scan-slaves-start", idStart, idEnd, testReg, testFn}));
  }
  function scanStop(){ ws.send(JSON.stringify({action:"scan-slaves-stop"})); }

  function readOnce(){
    const slaveId = parseInt(document.getElementById('readSlaveId').value)||1;
    const fn      = parseInt(document.getElementById('readFn').value)||3;
    const addr    = parseInt(document.getElementById('readAddr').value)||0;
    ws.send(JSON.stringify({action:"read-once", slaveId, fn, addr}));
  }
  function readStop(){ ws.send(JSON.stringify({action:"read-stop"})); }
</script>
</body>
</html>
)HTMLPAGE";
