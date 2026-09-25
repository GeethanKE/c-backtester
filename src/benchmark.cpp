#include <iostream>
#include <vector>
#include <string>
#include "Backtest.h"

// Runs the full engine against synthetic datasets of increasing size
// (generated ahead of time by scripts/generate_data.py) and reports
// throughput, so the effect of dataset size on wall-clock time and
// bars/sec is directly measurable rather than assumed.
int main(int argc, char** argv) {
    std::vector<std::string> sizes = {"1000", "10000", "100000", "500000"};
    std::string data_dir = (argc > 1) ? argv[1] : "data/bench";

    std::cout << "symbol,bars,elapsed_ms,bars_per_sec\n";
    for (const auto& sz : sizes) {
        std::string symbol = "BENCH_" + sz;
        std::vector<std::string> symbols{symbol};

        auto data = std::make_unique<CSVDataHandler>(symbols, data_dir);
        auto strategy = std::make_unique<MovingAverageCrossStrategy>(10, 30);
        auto portfolio = std::make_unique<NaivePortfolio>(symbols, 100000.0);
        auto execution = std::make_unique<SimulatedExecutionHandler>();

        Backtest engine(std::move(data), std::move(strategy),
                         std::move(portfolio), std::move(execution));
        BacktestStats stats = engine.run(/*verbose=*/false);

        double bars_per_sec = stats.elapsed_ms > 0
            ? stats.n_bars_processed / (stats.elapsed_ms / 1000.0) : 0;
        std::cout << symbol << "," << stats.n_bars_processed << ","
                  << stats.elapsed_ms << "," << bars_per_sec << "\n";
    }
    return 0;
}
