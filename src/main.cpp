#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "Backtest.h"

// Very small CLI arg parser: --symbol SYM --data-dir DIR --short N --long N
//                             --capital C --commission C --commission-flat C
//                             --slippage-bps N --quiet
struct Args {
    std::string symbol = "SYN";
    std::string data_dir = "data";
    int short_window = 10;
    int long_window = 30;
    double capital = 100000.0;
    double commission_per_share = 0.005;
    double commission_flat = 1.0;
    double slippage_bps = 1.0;
    bool verbose = true;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (arg == "--symbol") a.symbol = next();
        else if (arg == "--data-dir") a.data_dir = next();
        else if (arg == "--short") a.short_window = std::stoi(next());
        else if (arg == "--long") a.long_window = std::stoi(next());
        else if (arg == "--capital") a.capital = std::stod(next());
        else if (arg == "--commission") a.commission_per_share = std::stod(next());
        else if (arg == "--commission-flat") a.commission_flat = std::stod(next());
        else if (arg == "--slippage-bps") a.slippage_bps = std::stod(next());
        else if (arg == "--quiet") a.verbose = false;
    }
    return a;
}

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    std::vector<std::string> symbols{args.symbol};

    auto data = std::make_unique<CSVDataHandler>(symbols, args.data_dir);
    auto strategy = std::make_unique<MovingAverageCrossStrategy>(args.short_window, args.long_window);
    auto portfolio = std::make_unique<NaivePortfolio>(symbols, args.capital);
    auto execution = std::make_unique<SimulatedExecutionHandler>(
        args.commission_per_share, args.commission_flat, args.slippage_bps);

    Backtest engine(std::move(data), std::move(strategy),
                     std::move(portfolio), std::move(execution));

    BacktestStats stats = engine.run(args.verbose);
    (void)stats; // report already printed by engine.run(); kept for programmatic use

    // Dump the equity curve so it can be diffed against the Python reference.
    std::ofstream out("equity_curve.csv");
    out << "timestamp,cash,holdings_value,total\n";
    for (const auto& p : engine.naive_portfolio()->equity_curve()) {
        out << p.timestamp << "," << p.cash << "," << p.holdings_value << "," << p.total << "\n";
    }

    return 0;
}
