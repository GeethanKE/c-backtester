#!/usr/bin/env python3
"""
Independent pandas re-implementation of the moving-average crossover
strategy + NaivePortfolio cost model, used to validate the C++ engine.

It deliberately mirrors the C++ engine's exact sequencing (signal is
evaluated and filled on the same bar's close) rather than being "more
correct" — the goal here is a second, independently-written implementation
that should agree with the C++ output to the penny, not a different model.

Usage:
    python3 reference.py <csv_path> [--short 10] [--long 30] [--capital 100000]
"""
import sys
import argparse
import pandas as pd
import numpy as np


def run(csv_path, short_window=10, long_window=30, capital=100000.0,
        position_size_pct=0.95, commission_per_share=0.005,
        commission_flat=1.0, slippage_bps=1.0):

    df = pd.read_csv(csv_path)
    df["sma_short"] = df["close"].rolling(short_window).mean()
    df["sma_long"] = df["close"].rolling(long_window).mean()

    cash = capital
    position = 0
    invested = False
    n_fills = 0
    total_commission = 0.0
    total_slippage = 0.0
    equity_curve = []

    for i in range(len(df)):
        row = df.iloc[i]
        if i + 1 < long_window:  # not enough history yet (matches C++ get_latest_bars check)
            equity_curve.append(cash + position * row["close"])
            continue

        bullish = row["sma_short"] > row["sma_long"]
        price = row["close"]

        if bullish and not invested:
            alloc = cash * position_size_pct
            qty = int(alloc // price)
            if qty > 0:
                slip = price * (slippage_bps / 10000.0)
                fill_price = price + slip
                commission = commission_flat + commission_per_share * qty
                slippage_cost = abs(fill_price - price) * qty
                cash -= qty * fill_price
                cash -= commission
                cash -= slippage_cost
                position += qty
                invested = True
                n_fills += 1
                total_commission += commission
                total_slippage += slippage_cost
        elif not bullish and invested and position > 0:
            qty = position
            slip = price * (slippage_bps / 10000.0)
            fill_price = price - slip
            commission = commission_flat + commission_per_share * qty
            slippage_cost = abs(fill_price - price) * qty
            cash += qty * fill_price
            cash -= commission
            cash -= slippage_cost
            position = 0
            invested = False
            n_fills += 1
            total_commission += commission
            total_slippage += slippage_cost

        equity_curve.append(cash + position * price)

    equity = pd.Series(equity_curve)
    rets = equity.pct_change().dropna()
    sharpe = (rets.mean() / rets.std()) * np.sqrt(252) if rets.std() > 0 else 0.0
    running_max = equity.cummax()
    drawdown = (running_max - equity) / running_max
    max_dd = drawdown.max()

    final_equity = equity.iloc[-1]
    print("===== Python Reference Report =====")
    print(f"Bars processed:     {len(df)}")
    print(f"Initial capital:    ${capital:.2f}")
    print(f"Final equity:       ${final_equity:.2f}")
    print(f"Total return:       {100 * (final_equity - capital) / capital:.4f} %")
    print(f"Sharpe ratio:       {sharpe:.4f}")
    print(f"Max drawdown:       {100 * max_dd:.4f} %")
    print(f"Fills executed:     {n_fills}")
    print(f"Total commission:   ${total_commission:.2f}")
    print(f"Total slippage:     ${total_slippage:.2f}")
    return {
        "final_equity": final_equity,
        "total_return_pct": 100 * (final_equity - capital) / capital,
        "sharpe": sharpe,
        "max_dd_pct": 100 * max_dd,
        "n_fills": n_fills,
        "total_commission": total_commission,
        "total_slippage": total_slippage,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_path")
    parser.add_argument("--short", type=int, default=10)
    parser.add_argument("--long", type=int, default=30)
    parser.add_argument("--capital", type=float, default=100000.0)
    args = parser.parse_args()
    run(args.csv_path, args.short, args.long, args.capital)
