"""Gera a tabela de tempo de resposta a partir dos dados brutos.

Uso: python3 analisar.py dados/2026-10-03-barramento-livre.json

Compara a mediana medida com o tempo teórico de transmissão no barramento
(9600 bit/s, 10 bits por caractere, intervalo de 5 ms entre transações) e
com a referência /api/health, que não acessa o barramento.
"""
import json, statistics, sys

CH = 10 / 9600 * 1000   # ms por caractere (8N1)
GAP = 5                 # ms entre transações de uma leitura dividida


def leitura(n, por_transacao, bytes_dados):
    t = k = 0
    while n > 0:
        q = min(por_transacao, n)
        t += (8 + 5 + bytes_dados(q)) * CH
        n -= q; k += 1
    return t + (k - 1) * GAP, k


def regs(n): return leitura(n, 64, lambda q: 2 * q)
def bits(n): return leitura(n, 1024, lambda q: (q + 7) // 8)


TEORICO = {
    "FC03 1 reg": regs(1), "FC03 10 reg": regs(10), "FC03 64 reg": regs(64),
    "FC03 125 reg": regs(125), "FC04 10 reg": regs(10),
    "FC01 16 bits": bits(16), "FC01 2000 bits": bits(2000), "FC02 16 bits": bits(16),
    "FC06 1 reg": ((8 + 8) * CH, 1), "FC05 1 coil": ((8 + 8) * CH, 1),
    "FC16 10 reg": ((9 + 20 + 8) * CH, 1), "FC15 16 coils": ((9 + 2 + 8) * CH, 1),
}

R = json.load(open(sys.argv[1]))
base = R["latencia"]["health (sem barramento)"]["mediana"]
print("| Operação | Transações | Mínimo | Mediana | P95 | Teórico (barramento) | Resíduo por transação |")
print("|---|---:|---:|---:|---:|---:|---:|")
print(f"| health (sem barramento) | 0 | {R['latencia']['health (sem barramento)']['min']} | {base} | "
      f"{R['latencia']['health (sem barramento)']['p95']} | - | - |")
for nome, (t, k) in TEORICO.items():
    v = R["latencia"][nome]
    resid = (v["mediana"] - base - t) / k
    print(f"| {nome} | {k} | {v['min']} | {v['mediana']} | {v['p95']} | {t:.1f} | {resid:.1f} |")
print(f"\nValores em ms; n = {R['latencia']['FC03 1 reg']['n']} amostras por operação. "
      "Resíduo = (mediana - mediana do health - teórico) / transações.")
