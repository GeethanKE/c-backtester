#pragma once
#include <deque>
#include <memory>
#include <chrono>
#include <iostream>
#include "Event.h"
#include "DataHandler.h"
#include "Strategy.h"
#include "Portfolio.h"
#include "ExecutionHandler.h"

// ---------------------------------------------------------------------------
// Backtest.h
//
// This is the event loop. On each outer iteration it asks the DataHandler
// for the next bar(s); that produces MarketEvents which drain through the
// queue, each handler pushing new events onto the back as it reacts, until
// the queue is empty — then the loop asks for the next bar. This mirrors
// how a live trading system would actually be structured (market data
// arrives, strategy reacts, orders go out, fills come back), which is the
// whole point of an *event-driven* backtester versus a vectorized one:
// it can't accidentally "see the future" the way a naive vectorized
// pandas backtest sometimes does.
// ---------------------------------------------------------------------------

struct BacktestStats {
    double initial_capital;
    double final_equity;
    double total_return_pct;
    double sharpe_ratio;
    double max_drawdown_pct;
    int n_fills;
    double total_commission;
    double total_slippage;
    long n_bars_processed;
    double elapsed_ms;
};

class Backtest {
public:
    Backtest(std::unique_ptr<DataHandler> data,
              std::unique_ptr<Strategy> strategy,
              std::unique_ptr<Portfolio> portfolio,
              std::unique_ptr<ExecutionHandler> execution)
        : data_(std::move(data)), strategy_(std::move(strategy)),
          portfolio_(std::move(portfolio)), execution_(std::move(execution)) {}

    BacktestStats run(bool verbose = true) {
        auto start = std::chrono::high_resolution_clock::now();
        long bar_count = 0;

        while (data_->continue_backtest()) {
            data_->update_bars(queue_);

            std::string current_ts;
            while (!queue_.empty()) {
                std::unique_ptr<Event> event = std::move(queue_.front());
                queue_.pop_front();

                switch (event->type()) {
                    case EventType::MARKET: {
                        auto* mkt = static_cast<MarketEvent*>(event.get());
                        current_ts = mkt->timestamp;
                        bar_count++;
                        market_prices_[mkt->symbol] = mkt->close;
                        // Strategy reacts to the new bar first (may push a
                        // SIGNAL event), then Portfolio refreshes its price
                        // cache and equity snapshot for this bar. Both run
                        // to completion before the queue's next entry (the
                        // SIGNAL, if any) is dequeued, so by the time
                        // update_signal sizes an order, the Portfolio's
                        // price cache already reflects *this* bar's close
                        // rather than the previous one.
                        strategy_->calculate_signals(*mkt, *data_, queue_);
                        portfolio_->update_timeindex(current_ts, *data_);
                        break;
                    }
                    case EventType::SIGNAL: {
                        auto* sig = static_cast<SignalEvent*>(event.get());
                        portfolio_->update_signal(*sig, queue_);
                        break;
                    }
                    case EventType::ORDER: {
                        auto* ord = static_cast<OrderEvent*>(event.get());
                        double px = market_prices_[ord->symbol];
                        execution_->execute_order(*ord, current_ts, px, queue_);
                        break;
                    }
                    case EventType::FILL: {
                        auto* fill = static_cast<FillEvent*>(event.get());
                        portfolio_->update_fill(*fill);
                        break;
                    }
                }
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        auto* np = dynamic_cast<NaivePortfolio*>(portfolio_.get());
        BacktestStats stats{};
        stats.initial_capital = np->initial_capital();
        stats.final_equity = np->final_equity();
        stats.total_return_pct = 100.0 * (stats.final_equity - stats.initial_capital) / stats.initial_capital;
        stats.sharpe_ratio = np->sharpe_ratio();
        stats.max_drawdown_pct = 100.0 * np->max_drawdown();
        stats.n_fills = np->n_fills();
        stats.total_commission = np->total_commission();
        stats.total_slippage = np->total_slippage();
        stats.n_bars_processed = bar_count;
        stats.elapsed_ms = elapsed_ms;

        if (verbose) print_report(stats);
        return stats;
    }

    const Portfolio* portfolio() const { return portfolio_.get(); }
    NaivePortfolio* naive_portfolio() { return dynamic_cast<NaivePortfolio*>(portfolio_.get()); }

private:
    void print_report(const BacktestStats& s) const {
        std::cout << "\n===== Backtest Report =====\n";
        std::cout << "Bars processed:     " << s.n_bars_processed << "\n";
        std::cout << "Elapsed time:       " << s.elapsed_ms << " ms\n";
        std::cout << "Throughput:         "
                  << (s.elapsed_ms > 0 ? (s.n_bars_processed / (s.elapsed_ms / 1000.0)) : 0)
                  << " bars/sec\n";
        std::cout << "----------------------------\n";
        std::cout << "Initial capital:    $" << s.initial_capital << "\n";
        std::cout << "Final equity:       $" << s.final_equity << "\n";
        std::cout << "Total return:       " << s.total_return_pct << " %\n";
        std::cout << "Sharpe ratio:       " << s.sharpe_ratio << "\n";
        std::cout << "Max drawdown:       " << s.max_drawdown_pct << " %\n";
        std::cout << "Fills executed:     " << s.n_fills << "\n";
        std::cout << "Total commission:   $" << s.total_commission << "\n";
        std::cout << "Total slippage:     $" << s.total_slippage << "\n";
        std::cout << "============================\n";
    }

    std::unique_ptr<DataHandler> data_;
    std::unique_ptr<Strategy> strategy_;
    std::unique_ptr<Portfolio> portfolio_;
    std::unique_ptr<ExecutionHandler> execution_;
    std::deque<std::unique_ptr<Event>> queue_;
    std::map<std::string, double> market_prices_;
};
