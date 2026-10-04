"""Roteiro de validação do gateway: casos de teste CT01-CT10 e tempo de resposta.

Uso:
    python3 executar_testes.py [saida.json] [etapas]

    etapas: lista separada por vírgulas (CT01,...,CT10,CT07,LAT). Sem o
    argumento, executa todos os casos, exceto o CT07, e a latência (LAT).
    O CT07 é interativo: aguarda o escravo deixar de responder (pare o
    simulador), coleta as amostras e aguarda a recuperação.

Requisitos: Python 3.9+ (somente biblioteca padrão); gateway acessível como
modbus-gateway.local; nenhuma assinatura WebSocket ativa (feche o app).
Escritas usam endereços fora do mapa da planta (>= 20) ou restauram o valor
original ao final. Os valores esperados do CT01 correspondem ao estado do
escravo EB-01 no simulador durante os testes de 3 out. 2026.
"""
import json, socket, statistics, sys, threading, time, http.client
from datetime import datetime

HOST = "modbus-gateway.local"
IP = socket.gethostbyname(HOST)          # resolve o mDNS uma única vez
OUT = sys.argv[1] if len(sys.argv) > 1 else "resultados.json"
R = {"inicio": datetime.now().isoformat(timespec="seconds"), "ip": IP, "casos": {}, "latencia": {}}


def req(method, path, body=None, timeout=10):
    """Uma requisição em conexão nova; devolve (status, json|texto, ms)."""
    c = http.client.HTTPConnection(IP, 80, timeout=timeout)
    data = None if body is None else (body if isinstance(body, (bytes, str)) else json.dumps(body))
    hdr = {"Host": HOST, "Connection": "close"}
    if data is not None:
        hdr["Content-Type"] = "application/json"
    t0 = time.perf_counter()
    c.request(method, path, body=data, headers=hdr)
    r = c.getresponse()
    raw = r.read()
    ms = (time.perf_counter() - t0) * 1000
    c.close()
    try:
        payload = json.loads(raw)
    except Exception:
        payload = raw.decode(errors="replace")
    return r.status, payload, ms


def health():
    return req("GET", "/api/health")[1]


def bus_counts():
    m = health()["modbus"]
    return m["okCount"] + m["errorCount"]


def caso(cid, desc, fn):
    print(f"== {cid}: {desc}", flush=True)
    antes = bus_counts()
    res = fn()
    depois = bus_counts()
    res["transacoes_no_barramento"] = depois - antes
    R["casos"][cid] = {"descricao": desc, **res}
    print(json.dumps(res, ensure_ascii=False)[:600], flush=True)


# ---------------------------------------------------------------- CT10
def ct10():
    a = health()["modbus"]; time.sleep(30); b = health()["modbus"]
    return {"janela_s": 30, "ok_antes": a["okCount"], "ok_depois": b["okCount"],
            "erros_antes": a["errorCount"], "erros_depois": b["errorCount"],
            "assinaturas": health()["stream"]["subscriptions"]}


# ---------------------------------------------------------------- CT01
ESPERADO = {  # estado do escravo exibido no Modbux durante os testes (print do autor)
    "holding": [450, 850, 250, 5],
    "input": [870, 10, 0, 320, 0, 0],
    "coils": [False, True, True, False],
    "discrete": [True, False, False, True, False, False],
}


def ct01():
    out = {}
    for t, esp in ESPERADO.items():
        s1, p1, _ = req("GET", f"/api/{t}/0")
        sb, pb, _ = req("GET", f"/api/{t}?start=0&count={len(esp)}")
        valores = [r["value"] for r in pb.get("registers", [])] if "registers" in pb else pb.get("states")
        out[t] = {"item_status": s1, "item": p1, "bloco_status": sb, "bloco_valores": valores,
                  "esperado": esp, "confere": valores == esp, "functionCode": pb.get("functionCode")}
    return out


# ---------------------------------------------------------------- CT02 / CT03
def ct02():
    out = {}
    s0, p0, _ = req("GET", "/api/holding/3"); orig = p0["value"]
    sw, pw, ms = req("PUT", "/api/holding/3", {"value": orig + 2})
    sr, pr, _ = req("GET", "/api/holding/3")
    sx, px, _ = req("PUT", "/api/holding/3", {"value": orig})
    out["holding_3"] = {"original": orig, "escrita_status": sw, "escrita": pw, "lido_depois": pr["value"],
                        "confere": pr["value"] == orig + 2, "restaurado_status": sx}
    sw, pw, _ = req("PUT", "/api/coils/20", {"state": True})
    sr, pr, _ = req("GET", "/api/coils/20")
    sx, _, _ = req("PUT", "/api/coils/20", {"state": False})
    out["coil_20"] = {"escrita_status": sw, "escrita": pw, "lido_depois": pr.get("state"),
                      "confere": pr.get("state") is True, "restaurado_status": sx}
    return out


def ct03():
    out = {}
    vals = [11, 22, 33, 44]
    sw, pw, _ = req("PUT", "/api/holding", {"startAddress": 20, "values": vals})
    sr, pr, _ = req("GET", "/api/holding?start=20&count=4")
    lido = [r["value"] for r in pr["registers"]]
    req("PUT", "/api/holding", {"startAddress": 20, "values": [0, 0, 0, 0]})
    out["holding_20_23"] = {"escrita_status": sw, "functionCode": pw.get("functionCode"),
                            "lido": lido, "confere": lido == vals}
    st = [True, False, True, True]
    sw, pw, _ = req("PUT", "/api/coils", {"startAddress": 20, "states": st})
    sr, pr, _ = req("GET", "/api/coils?start=20&count=4")
    req("PUT", "/api/coils", {"startAddress": 20, "states": [False] * 4})
    out["coils_20_23"] = {"escrita_status": sw, "functionCode": pw.get("functionCode"),
                          "lido": pr.get("states"), "confere": pr.get("states") == st}
    return out


# ---------------------------------------------------------------- CT04 / CT07
def ct04():
    s1, p1, m1 = req("GET", "/api/holding/0?slave=1")
    s2, p2, m2 = req("GET", "/api/holding/0?slave=2")
    return {"slave1": {"status": s1, "ms": round(m1, 1)},
            "slave2": {"status": s2, "corpo": p2, "ms": round(m2, 1)}}


# ---------------------------------------------------------------- CT05
def ct05():
    casos = [
        ("GET", "/api/holding?start=0&count=0", None),
        ("GET", "/api/holding?start=0&count=126", None),
        ("GET", "/api/coils?start=0&count=2001", None),
        ("GET", "/api/holding?count=5", None),
        ("GET", "/api/holding/0?slave=0", None),
        ("GET", "/api/holding/0?slave=248", None),
        ("GET", "/api/holding/abc", None),
        ("PUT", "/api/holding/0", "{nao-e-json"),
        ("PUT", "/api/holding/0", {"valor": 1}),
        ("PUT", "/api/holding", {"startAddress": 0, "values": list(range(65))}),
        ("PUT", "/api/input/0", {"value": 1}),
        ("PUT", "/api/discrete/0", {"state": True}),
        ("PUT", "/api/holding/0", "{\"value\": 1, \"x\": \"" + "a" * 3000 + "\"}"),
        ("GET", "/api/inexistente", None),
        ("DELETE", "/api/holding/0", None),
    ]
    out = []
    for m, p, b in casos:
        s, pl, _ = req(m, p, b)
        out.append({"metodo": m, "uri": p if len(p) < 60 else p[:60] + "...",
                    "corpo": None if b is None else (str(b)[:40] + ("..." if len(str(b)) > 40 else "")),
                    "status": s, "erro": pl.get("error") if isinstance(pl, dict) else str(pl)[:60]})
    return {"requisicoes": out}


# ---------------------------------------------------------------- CT06
def ct06():
    out = {}
    for t, a in [("holding", 1000), ("holding", 9999), ("holding", 65535), ("input", 65535), ("coils", 65535)]:
        s, p, _ = req("GET", f"/api/{t}/{a}")
        out[f"{t}_{a}"] = {"status": s, "corpo": p}
    return out


# ---------------------------------------------------------------- CT07
def ct07():
    """Interativo: pare o escravo no simulador; depois volte a ligá-lo."""
    print("   aguardando o escravo deixar de responder (pare o simulador)...", flush=True)
    while True:
        s, p, ms = req("GET", "/api/holding/0")
        if s == 504:
            break
        time.sleep(2)
    leituras = []
    for _ in range(10):
        s, p, ms = req("GET", "/api/holding/0"); leituras.append([s, round(ms, 1)])
    s, p, ms = req("PUT", "/api/holding/30", {"value": 7})
    escrita = {"status": s, "corpo": p, "ms": round(ms, 1)}
    print("   escravo sem resposta registrado; religue o simulador...", flush=True)
    t0 = time.time()
    while True:
        s, p, ms = req("GET", "/api/holding/0")
        if s == 200:
            break
        time.sleep(2)
    return {"amostras_leitura": leituras, "escrita": escrita,
            "recuperacao": {"status": s, "leitura": p, "aguardou_s": round(time.time() - t0)}}


# ---------------------------------------------------------------- CT08
def ct08():
    n = 20
    res = [None] * n
    barreira = threading.Barrier(n)

    def w(i):
        barreira.wait()
        s, p, ms = req("GET", f"/api/holding?start={100 + i}&count=125")
        res[i] = (s, p.get("error") if isinstance(p, dict) else None, round(ms, 1))
    th = [threading.Thread(target=w, args=(i,)) for i in range(n)]
    [t.start() for t in th]; [t.join() for t in th]
    cont = {}
    for s, _, _ in res:
        cont[s] = cont.get(s, 0) + 1
    return {"simultaneas": n, "contagem_por_status": cont, "detalhe": res}


# ---------------------------------------------------------------- CT09
def ct09():
    t0=time.perf_counter(); s, p, _ = req("GET", "/api/openapi.yaml", timeout=60); dur=round((time.perf_counter()-t0)*1000)
    txt = p if isinstance(p, str) else json.dumps(p)
    linhas = txt.splitlines()
    caminhos = [l.strip() for l in linhas if l.startswith("  /")]
    sd, pd, _ = req("GET", "/docs")
    return {"ms_download": dur, "status": s, "primeira_linha": linhas[0] if linhas else "", "bytes": len(txt.encode()),
            "caminhos": caminhos, "docs_status": sd, "docs_usa_cdn": "cdn.jsdelivr.net" in str(pd)}


# ---------------------------------------------------------------- latência
def medir(nome, method, path, body=None, n=50):
    amostras, falhas = [], {}
    for _ in range(n):
        s, p, ms = req(method, path, body)
        if s == 200 and not (isinstance(p, dict) and p.get("cached")):
            amostras.append(ms)
        else:
            falhas[s] = falhas.get(s, 0) + 1
        time.sleep(0.05)
    a = sorted(amostras)
    R["latencia"][nome] = {
        "metodo": method, "uri": path, "n": len(a), "falhas": falhas,
        "min": round(a[0], 1), "mediana": round(statistics.median(a), 1),
        "media": round(statistics.mean(a), 1), "dp": round(statistics.pstdev(a), 1),
        "p95": round(a[int(0.95 * (len(a) - 1))], 1), "max": round(a[-1], 1), "amostras": [round(x, 1) for x in amostras]}
    print(f"   {nome}: mediana {R['latencia'][nome]['mediana']} ms, p95 {R['latencia'][nome]['p95']} ms, n={len(a)} falhas={falhas}", flush=True)


def latencia():
    print("== Latência", flush=True)
    medir("health (sem barramento)", "GET", "/api/health")
    medir("FC03 1 reg", "GET", "/api/holding/0")
    medir("FC03 10 reg", "GET", "/api/holding?start=0&count=10")
    medir("FC03 64 reg", "GET", "/api/holding?start=0&count=64")
    medir("FC03 125 reg", "GET", "/api/holding?start=0&count=125")
    medir("FC04 10 reg", "GET", "/api/input?start=0&count=10")
    medir("FC01 16 bits", "GET", "/api/coils?start=0&count=16")
    medir("FC01 2000 bits", "GET", "/api/coils?start=0&count=2000")
    medir("FC02 16 bits", "GET", "/api/discrete?start=0&count=16")
    medir("FC06 1 reg", "PUT", "/api/holding/30", {"value": 123})
    medir("FC05 1 coil", "PUT", "/api/coils/30", {"state": True})
    medir("FC16 10 reg", "PUT", "/api/holding", {"startAddress": 30, "values": list(range(10))})
    medir("FC15 16 coils", "PUT", "/api/coils", {"startAddress": 30, "states": [True, False] * 8})
    req("PUT", "/api/holding", {"startAddress": 30, "values": [0] * 10})
    req("PUT", "/api/coils", {"startAddress": 30, "states": [False] * 16})
    # escravo 2: o simulador responde com a exceção 02 (não há escravo mudo aqui; ver CT07)
    am = []
    for _ in range(10):
        s, p, ms = req("GET", "/api/holding/0?slave=2"); am.append((s, round(ms, 1)))
    R["latencia"]["escravo 2 (exceção 02)"] = {"amostras": am, "mediana": round(statistics.median([m for _, m in am]), 1)}
    print(f"   escravo 2 (exceção 02): {am}", flush=True)


if __name__ == "__main__":
    etapas = sys.argv[2].split(",") if len(sys.argv) > 2 else None
    import os
    if os.path.exists(OUT):
        R.update(json.load(open(OUT)))
    h = health()
    R["gateway"] = {"firmware": h["firmware"], "modbus_config": h["modbus"]["config"],
                    "rssi_dbm": h["wifiRssi"], "assinaturas": h["stream"]["subscriptions"]}
    assert h["stream"]["subscriptions"] == 0, "há assinaturas WebSocket ativas: feche o app"
    tabela = [("CT10", "Barramento sem requisições nem assinaturas", ct10),
              ("CT01", "Leitura das quatro tabelas", ct01),
              ("CT02", "Escrita de um item e leitura de volta", ct02),
              ("CT03", "Escrita em bloco e leitura de volta", ct03),
              ("CT04", "Seleção do escravo / escravo sem resposta", ct04),
              ("CT05", "Requisições inválidas", ct05),
              ("CT06", "Endereços fora do mapa do escravo", ct06),
              ("CT07", "Escravo sem resposta (simulador parado)", ct07),
              ("CT08", "Requisições simultâneas acima da capacidade da fila", ct08),
              ("CT09", "Descrição OpenAPI no dispositivo", ct09)]
    for cid, d, fn in tabela:
        if (etapas is None and cid != "CT07") or (etapas and cid in etapas):
            caso(cid, d, fn)
    if etapas is None or "LAT" in etapas:
        latencia()
    h = health()
    R["fim"] = datetime.now().isoformat(timespec="seconds")
    R["health_final"] = h
    json.dump(R, open(OUT, "w"), ensure_ascii=False, indent=1)
    print("gravado:", OUT)
