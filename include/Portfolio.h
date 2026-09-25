#pragma once
#include <deque>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "Event.h"
#include "DataHandler.h"

// ---------------------------------------------------------------------------
// Portfolio.h
//
// The Portfolio is the only component that knows about cash, positions, and
// money. It turns SignalEvents into sized OrderEvents, and turns FillEvents
// (confirmed executions, including costs) into updated holdings and an
// equity curve. Keeping this logic in one place is what makes it possible
// to reason about risk and P&L independently of strategy logic.
// ---------------------------------------------------------------------------

class Portfolio {
public:
    virtual ~Portfolio() = default;
    virtual void update_signal(const SignalEvent& event,
                                std::deque<std::unique_ptr<Event>>& queue) = 0;
    virtual void update_fill(const FillEvent& event) = 0;
    virtual void update_timeindex(const std::string& timestamp,
                                   const DataHandler& data) = 0;
};

struct EquityPoint {
    std::string timestamp;
    double cash;
    double holdings_value;
    double total;
};

// A straightforward "all-in / all-out" sizing model: on a LONG signal it
// buys as many shares as a fixed fraction of current equity allows; on EXIT
// it liquidates the full position. Commission is a flat rate + per-share
// fee; slippage is modeled as a fixed basis-point cost against the fill
// price. This is intentionally simple — the point is that the cost model
// lives in exactly one place and every order passes through it.
class NaivePortfolio : public Portfolio {
public:
    NaivePortfolio(std::vector<std::string> symbols, double initial_capital,
                    double position_size_pct = 0.95)
        : symbols_(std::move(symbols)), cash_(initial_capital),
          initial_capital_(initial_capital), position_size_pct_(position_size_pct) {
        for (const auto& s : symbols_) {
            positions_[s] = 0;
            last_close_[s] = 0.0;
        }
    }

    void update_signal(const SignalEvent& event,
                        std::deque<std::unique_ptr<Event>>& queue) override {
        long current_qty = positions_[event.symbol];
        double price = last_close_[event.symbol];
        if (price <= 0.0) return; // no market data seen yet for this symbol

        if (event.direction == SignalDirection::LONG && current_qty == 0) {
            double alloc = cash_ * position_size_pct_ * event.strength;
            long qty = static_cast<long>(alloc / price);
            if (qty > 0) {
                queue.push_back(std::make_unique<OrderEvent>(
                    event.symbol, "MKT", qty, OrderDirection::BUY));
            }
        } else if (event.direction == SignalDirection::EXIT && current_qty > 0) {
            queue.push_back(std::make_unique<OrderEvent>(
                event.symbol, "MKT", current_qty, OrderDirection::SELL));
        } else if (event.direction == SignalDirection::SHORT && current_qty == 0) {
            double alloc = cash_ * position_size_pct_ * event.strength;
            long qty = static_cast<long>(alloc / price);
            if (qty > 0) {
                queue.push_back(std::make_unique<OrderEvent>(
                    event.symbol, "MKT", qty, OrderDirection::SELL));
            }
        }
    }

    void update_fill(const FillEvent& event) override {
        long signed_qty = (event.direction == OrderDirection::BUY)
                               ? event.quantity : -event.quantity;
        positions_[event.symbol] += signed_qty;

        double cost = signed_qty * event.fill_price;
        cash_ -= cost;
        cash_ -= event.commission;
        cash_ -= event.slippage_cost;

        total_commission_ += event.commission;
        total_slippage_ += event.slippage_cost;
        n_fills_++;
    }

    void update_timeindex(const std::string& timestamp,
                           const DataHandler& data) override {
        double holdings_value = 0.0;
        for (const auto& s : symbols_) {
            auto bars = data.get_latest_bars(s, 1);
            if (!bars.empty()) {
                last_close_[s] = bars.back().close;
                holdings_value += positions_[s] * last_close_[s];
            }
        }
        double total = cash_ + holdings_value;
        equity_curve_.push_back({timestamp, cash_, holdings_value, total});
    }

    const std::vector<EquityPoint>& equity_curve() const { return equity_curve_; }
    double final_equity() const { return equity_curve_.empty() ? initial_capital_ : equity_curve_.back().total; }
    double initial_capital() const { return initial_capital_; }
    double total_commission() const { return total_commission_; }
    double total_slippage() const { return total_slippage_; }
    int n_fills() const { return n_fills_; }

    // Sharpe ratio computed from the equity curve's period-over-period
    // returns, annualized assuming daily bars (252 trading days/year).
    double sharpe_ratio() const {
        if (equity_curve_.size() < 2) return 0.0;
        std::vector<double> rets;
        rets.reserve(equity_curve_.size() - 1);
        for (size_t i = 1; i < equity_curve_.size(); ++i) {
            double prev = equity_curve_[i - 1].total;
            double cur = equity_curve_[i].total;
            if (prev > 0) rets.push_back((cur - prev) / prev);
        }
        if (rets.empty()) return 0.0;
        double mean = 0.0;
        for (double r : rets) mean += r;
        mean /= rets.size();
        double var = 0.0;
        for (double r : rets) var += (r - mean) * (r - mean);
        var /= rets.size();
        double stdev = std::sqrt(var);
        if (stdev == 0.0) return 0.0;
        return (mean / stdev) * std::sqrt(252.0);
    }

    double max_drawdown() const {
        double peak = -1e18, mdd = 0.0;
        for (const auto& p : equity_curve_) {
            peak = std::max(peak, p.total);
            double dd = (peak - p.total) / peak;
            mdd = std::max(mdd, dd);
        }
        return mdd;
    }

private:
    std::vector<std::string> symbols_;
    std::map<std::string, long> positions_;
    std::map<std::string, double> last_close_;
    double cash_;
    double initial_capital_;
    double position_size_pct_;
    std::vector<EquityPoint> equity_curve_;
    double total_commission_ = 0.0;
    double total_slippage_ = 0.0;
    int n_fills_ = 0;
};
