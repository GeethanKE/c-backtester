# C++ Event-Driven Backtesting Engine

A from-scratch event-driven backtesting engine in modern C++17: historical
market-data ingestion, strategy execution, simulated order handling, and
portfolio accounting, wired together through a single event queue.

## Why event-driven

A vectorized (pandas-style) backtest computes signals over the whole
DataFrame at once, which makes it easy to accidentally use information from
the future (e.g. a rolling window that peeks past the current row). An
event-driven design processes one bar at a time — market data arrives,
the strategy reacts, orders are sized and filled, the portfolio updates —
the same sequence a live trading system would follow. That structural
similarity to live trading is the main reason to build it this way.

## Architecture

```
DataHandler --MarketEvent--> Strategy --SignalEvent--> Portfolio
                                                            |
                                                       OrderEvent
                                                            v
Portfolio <--FillEvent-- ExecutionHandler <----------------+
```

All five stages communicate only through `Event` objects on a shared
`std::deque<std::unique_ptr<Event>>` queue (`include/Event.h`), so each
component only knows about the interface of its neighbor:

| Component | File | Responsibility |
|---|---|---|
| `DataHandler` | `include/DataHandler.h` | Reads OHLCV CSVs, streams `MarketEvent`s, exposes lookback windows |
| `Strategy` | `include/Strategy.h` | Pure signal logic; here, an SMA crossover — no sizing, no cash |
| `Portfolio` | `include/Portfolio.h` | Position sizing, cash/commission/slippage accounting, equity curve, Sharpe/drawdown |
| `ExecutionHandler` | `include/ExecutionHandler.h` | Simulates fills with a commission + slippage model |
| `Backtest` | `include/Backtest.h` | The event loop that drives everything and reports results |

Each interface is abstract, so any stage can be swapped independently —
a live broker `ExecutionHandler`, a different `Strategy`, a database-backed
`DataHandler` — without touching the rest of the engine.

**A subtlety worth knowing about:** on each `MarketEvent`, the `Strategy`
runs first and may push a `SignalEvent`, then the `Portfolio` refreshes its
price cache for that bar. Both finish before the queue's next entry (the
signal, if any) is dequeued — so by the time an order is sized, the
Portfolio is pricing it off the *current* bar's close, not the previous
one. Getting this ordering wrong is an easy way to introduce a silent
look-ahead/lag bug; see "Validation" below for how it was caught.

## Build

Requires a C++17 compiler. No external dependencies.

```bash
g++ -std=c++17 -O3 -Iinclude src/main.cpp -o build/backtest
g++ -std=c++17 -O3 -Iinclude src/benchmark.cpp -o build/benchmark
```

(A `CMakeLists.txt` is included too, if you have CMake: `cmake -S . -B build && cmake --build build`.)

## Run

```bash
# Generate a synthetic 750-bar daily series
python3 scripts/generate_data.py SYN 750 data

# Run the engine (SMA(10)/SMA(30) crossover on $100k)
./build/backtest --symbol SYN --data-dir data --short 10 --long 30 --capital 100000
```

```
===== Backtest Report =====
Bars processed:     750
Elapsed time:       0.27 ms
Throughput:         2.78e+06 bars/sec
----------------------------
Initial capital:    $100000
Final equity:       $98166
Total return:       -1.834 %
Sharpe ratio:       0.0184
Max drawdown:       16.91 %
Fills executed:     33
Total commission:   $155.66
Total slippage:     $307.16
============================
```

CLI flags: `--symbol`, `--data-dir`, `--short`, `--long`, `--capital`,
`--commission`, `--commission-flat`, `--slippage-bps`, `--quiet`.

## Validation against a Python reference

`scripts/reference.py` is an independently-written pandas implementation
of the same strategy and cost model. Run it against the same CSV and
compare:

```bash
python3 scripts/reference.py data/SYN.csv --short 10 --long 30 --capital 100000
```

On the bundled sample data, both implementations agree to the penny:
final equity **$98,166.0** (C++) vs **$98,166.03** (Python), same 33
fills, same Sharpe ratio and max drawdown. This kind of two-implementation
cross-check is genuinely useful, not a formality — building this project,
it caught a real bug: the Portfolio was initially sizing orders off the
*previous* bar's close while the ExecutionHandler filled at the *current*
bar's close, a one-bar lag that silently underweighted every trade by a
few percent. The fix is the event-ordering note above.

## Benchmark

`src/benchmark.cpp` runs the engine against synthetic datasets of
increasing size:

```bash
python3 scripts/generate_data.py --bench data/bench
./build/benchmark data/bench
```

| Bars | Elapsed | Throughput |
|---|---|---|
| 1,000 | 0.35 ms | 2.82M bars/sec |
| 10,000 | 3.68 ms | 2.72M bars/sec |
| 100,000 | 37.2 ms | 2.69M bars/sec |
| 500,000 | 194.6 ms | 2.57M bars/sec |

Throughput is roughly flat as dataset size grows — the engine is O(n) in
the number of bars, as expected for a single-pass event loop with O(1)
per-event work (the SMA strategy's lookback is a bounded window, not a
full history scan).

## Extending it

- **New strategy:** implement `Strategy::calculate_signals`.
- **New cost model:** implement `ExecutionHandler::execute_order`.
- **New sizing/risk logic:** implement `Portfolio::update_signal`.
- **Live/other data source:** implement `DataHandler`.

## Project layout

```
include/
  Event.h            Event types (Market/Signal/Order/Fill)
  DataHandler.h       DataHandler interface + CSVDataHandler
  Strategy.h          Strategy interface + MovingAverageCrossStrategy
  Portfolio.h          Portfolio interface + NaivePortfolio (sizing, costs, equity curve)
  ExecutionHandler.h   ExecutionHandler interface + SimulatedExecutionHandler
  Backtest.h           Event-loop driver + reporting
src/
  main.cpp            CLI entry point
  benchmark.cpp        Throughput benchmark across dataset sizes
scripts/
  generate_data.py     Synthetic OHLCV data generator
  reference.py          Independent pandas re-implementation for validation
data/
  SYN.csv              Bundled sample dataset
CMakeLists.txt
```
