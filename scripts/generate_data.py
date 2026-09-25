#!/usr/bin/env python3
"""
Generates synthetic daily OHLCV CSV files for the backtester.

Usage:
    python3 generate_data.py SYN 750 data                # one 750-bar series
    python3 generate_data.py --bench data/bench           # benchmark suite
"""
import sys
import os
import random
import argparse
from datetime import date, timedelta


def gen_series(n_bars, start_price=100.0, seed=42):
    random.seed(seed)
    rows = []
    price = start_price
    d = date(2015, 1, 1)
    for _ in range(n_bars):
        drift = 0.0002
        shock = random.gauss(0, 0.012)
        ret = drift + shock
        open_p = price
        close_p = max(0.01, open_p * (1 + ret))
        high_p = max(open_p, close_p) * (1 + abs(random.gauss(0, 0.003)))
        low_p = min(open_p, close_p) * (1 - abs(random.gauss(0, 0.003)))
        volume = random.randint(100000, 2000000)
        rows.append((d.isoformat(), open_p, high_p, low_p, close_p, volume))
        price = close_p
        d += timedelta(days=1)
    return rows


def write_csv(path, rows):
    with open(path, "w") as f:
        f.write("timestamp,open,high,low,close,volume\n")
        for r in rows:
            f.write(f"{r[0]},{r[1]:.4f},{r[2]:.4f},{r[3]:.4f},{r[4]:.4f},{r[5]}\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("symbol_or_flag", nargs="?", default="SYN")
    parser.add_argument("n_bars_or_dir", nargs="?", default="750")
    parser.add_argument("data_dir", nargs="?", default="data")
    parser.add_argument("--bench", action="store_true")
    args, unknown = parser.parse_known_args()

    if "--bench" in sys.argv:
        out_dir = sys.argv[sys.argv.index("--bench") + 1] if len(sys.argv) > sys.argv.index("--bench") + 1 else "data/bench"
        os.makedirs(out_dir, exist_ok=True)
        for n in [1000, 10000, 100000, 500000]:
            rows = gen_series(n, seed=n)
            write_csv(os.path.join(out_dir, f"BENCH_{n}.csv"), rows)
            print(f"wrote {n} bars -> {out_dir}/BENCH_{n}.csv")
    else:
        symbol = args.symbol_or_flag
        n_bars = int(args.n_bars_or_dir)
        data_dir = args.data_dir
        os.makedirs(data_dir, exist_ok=True)
        rows = gen_series(n_bars)
        write_csv(os.path.join(data_dir, f"{symbol}.csv"), rows)
        print(f"wrote {n_bars} bars -> {data_dir}/{symbol}.csv")
