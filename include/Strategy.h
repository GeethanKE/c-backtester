#pragma once
#include <deque>
#include <memory>
#include <map>
#include <numeric>
#include "Event.h"
#include "DataHandler.h"

// ---------------------------------------------------------------------------
// Strategy.h
//
// A Strategy consumes MarketEvents and, based on whatever logic it likes,
// pushes SignalEvents (LONG/SHORT/EXIT) onto the shared queue. Strategies
// never touch order sizing, cash, or execution — that's the Portfolio's job.
// This separation is what lets you swap strategies without touching risk
// or accounting code, and vice versa.
// ---------------------------------------------------------------------------

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void calculate_signals(const MarketEvent& event,
                                    const DataHandler& data,
                                    std::deque<std::unique_ptr<Event>>& queue) = 0;
};

// Classic dual moving-average crossover: go long when the short-window SMA
// crosses above the long-window SMA, exit (go flat) when it crosses back
// below. One of the simplest strategies with genuine path-dependent state,
// which makes it a good correctness check against a reference implementation.
class MovingAverageCrossStrategy : public Strategy {
public:
    MovingAverageCrossStrategy(int short_window, int long_window)
        : short_window_(short_window), long_window_(long_window) {}

    void calculate_signals(const MarketEvent& event,
                            const DataHandler& data,
                            std::deque<std::unique_ptr<Event>>& queue) override {
        const std::string& sym = event.symbol;
        auto bars = data.get_latest_bars(sym, long_window_);
        if (static_cast<int>(bars.size()) < long_window_) return; // not enough history yet

        double short_sma = sma(bars, short_window_);
        double long_sma = sma(bars, long_window_);

        bool bullish = short_sma > long_sma;
        bool& invested = invested_[sym]; // defaults to false on first access

        if (bullish && !invested) {
            queue.push_back(std::make_unique<SignalEvent>(
                sym, event.timestamp, SignalDirection::LONG));
            invested = true;
        } else if (!bullish && invested) {
            queue.push_back(std::make_unique<SignalEvent>(
                sym, event.timestamp, SignalDirection::EXIT));
            invested = false;
        }
    }

private:
    static double sma(const std::vector<Bar>& bars, int window) {
        int n = static_cast<int>(bars.size());
        double sum = 0.0;
        for (int i = n - window; i < n; ++i) sum += bars[i].close;
        return sum / window;
    }

    int short_window_;
    int long_window_;
    std::map<std::string, bool> invested_;
};
