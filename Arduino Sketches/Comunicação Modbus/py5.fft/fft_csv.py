#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
fft_csv.py

Calcula espectro (FFT) de uma série temporal em CSV (pt-BR friendly) e ajuda a
detectar periodicidades bem baixas (minutos, horas, dias, semanas, meses).

Principais melhorias vs versão anterior:
- Remove duplicatas no tempo (agrega por média/último).
- Lida melhor com tempo irregular: reamostra/interpola para grade uniforme.
- Permite focar em baixas frequências via --resample e filtros por período.
- Lista os picos mais fortes (freq/periodo) para facilitar leitura.

Requisitos:
  pip install numpy pandas

Opcional para gráfico:
  pip install matplotlib

Exemplos:
  python fft_csv.py dados.csv --col-t "timestamp" --col-x "potencia_w" --resample 60 --min-period 10m --max-period 3d --peaks 15 --plot
  python fft_csv.py dados.csv --col-t "DataHora" --col-x "P" --dayfirst --dedup mean --resample 300 --max-period 30d
"""

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd

try:
    import matplotlib.pyplot as plt
except Exception:
    plt = None


# ---------------------------- utilidades ----------------------------

_TIME_UNITS = {
    "s": 1.0,
    "sec": 1.0,
    "m": 60.0,
    "min": 60.0,
    "h": 3600.0,
    "d": 86400.0,
    "w": 604800.0,
    "wk": 604800.0,
    # mês aproximado (30 dias) -> suficiente p/ análise exploratória
    "mo": 30.0 * 86400.0,
    "mon": 30.0 * 86400.0,
}

def parse_duration_to_seconds(s: str) -> float:
    """
    Aceita: "120", "60s", "10m", "2h", "7d", "4w", "1mo"
    """
    s = str(s).strip().lower()
    if not s:
        raise ValueError("duração vazia")
    # num puro -> segundos
    if all(ch.isdigit() or ch == "." for ch in s):
        return float(s)

    # separar parte numérica e sufixo
    num = ""
    unit = ""
    for ch in s:
        if (ch.isdigit() or ch == ".") and unit == "":
            num += ch
        else:
            unit += ch
    if num == "" or unit == "":
        raise ValueError(f"duração inválida: {s!r}")
    unit = unit.strip()
    if unit not in _TIME_UNITS:
        raise ValueError(f"unidade inválida em {s!r}. Use s,m,h,d,w,mo")
    return float(num) * _TIME_UNITS[unit]


def robust_read_csv(path: Path) -> pd.DataFrame:
    """
    Tenta ler CSV com separador ';' ou ',' automaticamente.
    Mantém decimal com vírgula se existir.
    """
    # engine='python' + sep=None tenta inferir separador
    df = pd.read_csv(path, sep=None, engine="python")
    return df


def to_time_seconds(col: pd.Series, dayfirst: bool) -> np.ndarray:
    """
    Converte coluna de tempo em segundos (float) relativo ao primeiro ponto.
    Aceita:
    - datetime (string) -> parse via pandas.to_datetime
    - números (segundos ou ms) -> usa como está (auto corrige ms se necessário)
    """
    # tenta datetime
    dt = pd.to_datetime(col, errors="coerce", dayfirst=dayfirst, utc=False)
    if dt.notna().sum() >= max(3, int(0.7 * len(col))):
        # usa datetime
        dt = pd.to_datetime(col, errors="coerce", dayfirst=dayfirst)
        if dt.isna().any():
            # remove linhas com tempo inválido
            mask = ~dt.isna()
            dt = dt[mask]
        t_ns = dt.view("int64").to_numpy()
        t_sec = (t_ns - t_ns[0]) / 1e9
        return t_sec.astype(float)

    # fallback numérico
    x = pd.to_numeric(col, errors="coerce").to_numpy(dtype=float)
    # remove NaNs
    x = x[~np.isnan(x)]
    if x.size < 2:
        raise ValueError("coluna de tempo não pôde ser interpretada (datetime nem numérica).")
    # heurística: se parece ms (valores grandes), converte para s
    # ex: epoch ms ~ 1.7e12; segundos ~ 1.7e9
    med = np.nanmedian(x)
    if med > 1e11:
        x = x / 1000.0
    # relativo ao primeiro
    x = x - x[0]
    return x


def dedup_time(t: np.ndarray, x: np.ndarray, how: str) -> tuple[np.ndarray, np.ndarray]:
    """
    Remove timestamps duplicados agregando os valores de x.
    how: 'mean' | 'last'
    """
    if t.size != x.size:
        raise ValueError("t e x com tamanhos diferentes")
    # ordena
    order = np.argsort(t)
    t = t[order]
    x = x[order]

    # agrupa por t
    # usar pandas para facilitar
    s = pd.Series(x, index=pd.Index(t, name="t"))
    if how == "mean":
        g = s.groupby(level=0).mean()
    elif how == "last":
        g = s.groupby(level=0).last()
    else:
        raise ValueError("how inválido para dedup (use mean ou last)")
    t2 = g.index.to_numpy(dtype=float)
    x2 = g.to_numpy(dtype=float)
    return t2, x2


def estimate_fs_from_uniform_grid(dt: float) -> float:
    if dt <= 0:
        raise ValueError("dt inválido")
    return 1.0 / dt


def resample_to_uniform(t: np.ndarray, x: np.ndarray, step_s: float) -> tuple[np.ndarray, np.ndarray]:
    """
    Cria uma grade uniforme e interpola x. Mantém o mesmo intervalo total.
    """
    if step_s <= 0:
        raise ValueError("step_s deve ser > 0")
    t0 = float(t[0])
    t1 = float(t[-1])
    if t1 <= t0:
        raise ValueError("tempo insuficiente (t1<=t0)")
    n = int(np.floor((t1 - t0) / step_s)) + 1
    tg = t0 + np.arange(n, dtype=float) * step_s
    # interpola linearmente
    xg = np.interp(tg, t, x)
    return tg, xg


def choose_auto_step(t: np.ndarray) -> float:
    """
    Escolhe step pela mediana dos deltas (robusto).
    """
    dt = np.diff(t)
    dt = dt[np.isfinite(dt) & (dt > 0)]
    if dt.size == 0:
        raise ValueError("não foi possível estimar dt")
    return float(np.median(dt))


def get_window(name: str, n: int) -> np.ndarray:
    name = (name or "hann").lower()
    if name in ("hann", "hanning"):
        return np.hanning(n)
    if name == "hamming":
        return np.hamming(n)
    if name in ("blackman",):
        return np.blackman(n)
    if name in ("rect", "box", "rectangular", "none"):
        return np.ones(n)
    raise ValueError(f"janela desconhecida: {name}")


def rfft_spectrum(x: np.ndarray, fs: float, window: np.ndarray, pad_to_pow2: bool) -> tuple[np.ndarray, np.ndarray]:
    """
    Retorna (freq_hz, mag) para o espectro de magnitude (single-sided).
    """
    x = np.asarray(x, dtype=float)
    n = x.size
    if window.size != n:
        raise ValueError("window e x com tamanhos diferentes")

    xw = x * window
    nfft = n
    if pad_to_pow2:
        nfft = int(2 ** np.ceil(np.log2(max(2, n))))
    X = np.fft.rfft(xw, n=nfft)
    f = np.fft.rfftfreq(nfft, d=1.0 / fs)

    # ganho coerente da janela p/ amplitude
    cg = np.sum(window) / n
    mag = np.abs(X) / (n * cg)

    if mag.size > 2:
        mag[1:-1] *= 2.0  # single-sided
    return f, mag


def simple_peak_pick(f: np.ndarray, mag: np.ndarray, k: int) -> list[tuple[float, float]]:
    """
    Retorna top-k picos (freq, mag). Sem scipy: aproxima usando máximos locais simples.
    """
    if f.size < 5:
        return []
    m = mag
    # máximos locais: m[i-1] < m[i] > m[i+1]
    idx = np.where((m[1:-1] > m[:-2]) & (m[1:-1] > m[2:]))[0] + 1
    if idx.size == 0:
        return []
    # ordena por magnitude desc
    idx = idx[np.argsort(m[idx])[::-1]]
    idx = idx[: max(1, k)]
    return [(float(f[i]), float(m[i])) for i in idx]


def hz_to_period_str(freq_hz: float) -> str:
    if freq_hz <= 0:
        return "∞"
    p = 1.0 / freq_hz
    if p < 120:
        return f"{p:.3g} s"
    if p < 7200:
        return f"{p/60:.3g} min"
    if p < 172800:
        return f"{p/3600:.3g} h"
    if p < 1209600:
        return f"{p/86400:.3g} d"
    return f"{p/604800:.3g} sem"


# ---------------------------- CLI ----------------------------

def parse_args():
    p = argparse.ArgumentParser(description="FFT de CSV + detecção de periodicidades baixas (min/horas/dias/semanas/meses).")

    p.add_argument("csv", help="Caminho do CSV de entrada.")
    p.add_argument("--col-t", default=None, help="Nome da coluna de tempo. Se omitido, tenta a 1ª coluna.")
    p.add_argument("--col-x", default=None, help="Nome da coluna do sinal (ex: potência). Se omitido, tenta a 2ª coluna.")
    p.add_argument("--dayfirst", action="store_true", help="Interpreta datas como dia/mês/ano (pt-BR).")

    p.add_argument("--dedup", choices=["mean", "last"], default="mean",
                   help="Como tratar timestamps duplicados (média ou último).")
    p.add_argument("--resample", default=None,
                   help="Força reamostragem para passo fixo (ex: 60, 60s, 1m, 5m, 1h). Recomendado p/ baixa freq.")
    p.add_argument("--auto-resample", action="store_true",
                   help="Se tempo for irregular, reamostra automaticamente usando a mediana de dt.")
    p.add_argument("--pad-pow2", action="store_true", help="Zero-pad para próximo 2^n (melhora interpolação do espectro).")

    p.add_argument("--window", default="hann", help="Janela: hann|hamming|blackman|rect.")
    p.add_argument("--detrend", action="store_true", help="Remove média do sinal antes da FFT.")
    p.add_argument("--max-freq", type=float, default=None, help="Corta o espectro acima desta frequência (Hz).")

    p.add_argument("--min-period", default=None,
                   help="Mostra/analisa apenas frequências com período >= isso (ex: 10m, 2h, 1d, 1w, 1mo).")
    p.add_argument("--max-period", default=None,
                   help="Mostra/analisa apenas frequências com período <= isso (ex: 2h, 3d, 30d).")

    p.add_argument("--peaks", type=int, default=12, help="Quantidade de picos a listar.")
    p.add_argument("--out", default=None, help="CSV de saída (padrão: <entrada>_fft.csv).")
    p.add_argument("--plot", action="store_true", help="Plota magnitude vs frequência.")
    return p.parse_args()


# ---------------------------- processamento ----------------------------

def load_and_prepare(path: Path, col_t: str | None, col_x: str | None, dayfirst: bool, dedup: str) -> tuple[np.ndarray, np.ndarray]:
    df = robust_read_csv(path)

    if df.shape[1] < 2:
        raise ValueError("CSV precisa ter pelo menos 2 colunas (tempo e sinal).")

    if col_t is None:
        col_t = df.columns[0]
    if col_x is None:
        # segunda coluna por padrão
        col_x = df.columns[1]

    if col_t not in df.columns:
        raise ValueError(f"Coluna de tempo não encontrada: {col_t!r}")
    if col_x not in df.columns:
        raise ValueError(f"Coluna do sinal não encontrada: {col_x!r}")

    # tempo
    dt_parsed = pd.to_datetime(df[col_t], errors="coerce", dayfirst=dayfirst)
    is_datetime = dt_parsed.notna().sum() >= max(3, int(0.7 * len(df)))
    if is_datetime:
        t = pd.to_datetime(df[col_t], errors="coerce", dayfirst=dayfirst)
        mask = ~t.isna()
        df = df.loc[mask].copy()
        t = t.loc[mask]
        t_ns = t.view("int64").to_numpy()
        t_sec = (t_ns - t_ns[0]) / 1e9
    else:
        t_num = pd.to_numeric(df[col_t], errors="coerce")
        mask = ~t_num.isna()
        df = df.loc[mask].copy()
        t_num = t_num.loc[mask]
        t = t_num.to_numpy(dtype=float)
        # heurística ms->s
        med = float(np.nanmedian(t))
        if med > 1e11:
            t = t / 1000.0
        t_sec = t - t[0]

    # sinal
    # tenta converter respeitando vírgula decimal
    x_raw = df[col_x].astype(str).str.replace(".", "", regex=False).str.replace(",", ".", regex=False)
    x = pd.to_numeric(x_raw, errors="coerce").to_numpy(dtype=float)
    ok = np.isfinite(t_sec) & np.isfinite(x)
    t_sec = t_sec[ok]
    x = x[ok]

    if t_sec.size < 8:
        raise ValueError("Poucos pontos válidos após limpeza (precisa de mais dados).")

    # dedup + sort
    t_sec, x = dedup_time(t_sec, x, dedup)
    if t_sec.size < 8:
        raise ValueError("Poucos pontos após remover duplicatas.")
    return t_sec, x


def apply_period_filter(f: np.ndarray, mag: np.ndarray, min_period_s: float | None, max_period_s: float | None) -> tuple[np.ndarray, np.ndarray]:
    mask = np.ones_like(f, dtype=bool)
    # f=0 (DC) não tem período finito
    mask &= f > 0

    if min_period_s is not None:
        # período >= min -> freq <= 1/min
        mask &= f <= (1.0 / min_period_s)
    if max_period_s is not None:
        # período <= max -> freq >= 1/max
        mask &= f >= (1.0 / max_period_s)

    return f[mask], mag[mask]


def main():
    args = parse_args()
    in_path = Path(args.csv)
    if not in_path.exists():
        print(f"Arquivo não encontrado: {in_path}", file=sys.stderr)
        sys.exit(1)

    try:
        t, x = load_and_prepare(in_path, args.col_t, args.col_x, args.dayfirst, args.dedup)
    except Exception as e:
        print(f"Erro ao ler/preparar CSV: {e}", file=sys.stderr)
        sys.exit(2)

    # resample
    used_step = None
    if args.resample is not None:
        try:
            used_step = parse_duration_to_seconds(args.resample)
        except Exception as e:
            print(f"--resample inválido: {e}", file=sys.stderr)
            sys.exit(2)
        t, x = resample_to_uniform(t, x, used_step)
    else:
        # auto-resample se irregular
        dt = np.diff(t)
        dt = dt[np.isfinite(dt) & (dt > 0)]
        if dt.size == 0:
            print("Erro: não foi possível calcular dt.", file=sys.stderr)
            sys.exit(2)
        med = float(np.median(dt))
        # mede irregularidade (p95/p50)
        p95 = float(np.percentile(dt, 95))
        irregular = (p95 / med) > 1.5
        if args.auto_resample and irregular:
            used_step = med
            t, x = resample_to_uniform(t, x, used_step)

    # fs
    if used_step is None:
        # assume aproximadamente uniforme
        used_step = choose_auto_step(t)
    fs = estimate_fs_from_uniform_grid(used_step)

    # prepara sinal
    if args.detrend:
        x = x - float(np.mean(x))

    n = x.size
    win = get_window(args.window, n)

    f, mag = rfft_spectrum(x, fs, win, args.pad_pow2)

    # cortes
    if args.max_freq is not None:
        m = f <= float(args.max_freq)
        f, mag = f[m], mag[m]

    # filtros por período p/ focar baixas freq
    min_period_s = parse_duration_to_seconds(args.min_period) if args.min_period else None
    max_period_s = parse_duration_to_seconds(args.max_period) if args.max_period else None
    f_filt, mag_filt = apply_period_filter(f, mag, min_period_s, max_period_s)

    # saída
    out_path = Path(args.out) if args.out else in_path.with_name(in_path.stem + "_fft.csv")
    out_df = pd.DataFrame({"freq_hz": f, "mag": mag})
    out_df.to_csv(out_path, sep=";", index=False, float_format="%.10g", decimal=",")

    duration_s = float(t[-1] - t[0])
    dfreq = 1.0 / duration_s if duration_s > 0 else float("nan")

    print(f"OK: FFT calculada.")
    print(f"  Janela total: {duration_s/3600:.3g} h  (resolução ~ {dfreq:.3g} Hz)")
    print(f"  Passo usado: {used_step:.6g} s  -> fs≈{fs:.6g} Hz")
    print(f"  N={n}  Saída: {out_path}")

    # picos
    if args.peaks and args.peaks > 0:
        peaks = simple_peak_pick(f_filt, mag_filt, args.peaks)
        if peaks:
            print("\nPicos (após filtros de período/freq):")
            print("  #   freq(Hz)        mag        período")
            for i, (ff, mm) in enumerate(peaks, 1):
                print(f"  {i:02d}  {ff:>11.6g}  {mm:>9.4g}   {hz_to_period_str(ff)}")
        else:
            print("\nSem picos detectáveis na faixa filtrada (tente ajustar --min-period/--max-period ou --resample).")

    # plot
    if args.plot:
        if plt is None:
            print("matplotlib não instalado. Instale com: pip install matplotlib", file=sys.stderr)
            sys.exit(3)
        plt.figure()
        plt.plot(f_filt if f_filt.size else f, mag_filt if mag_filt.size else mag)
        plt.xlabel("Frequência (Hz)")
        plt.ylabel("Magnitude")
        plt.title("Espectro (FFT)")
        plt.grid(True)
        plt.tight_layout()
        plt.show()


if __name__ == "__main__":
    main()
