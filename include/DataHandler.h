#pragma once
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "Event.h"

// ---------------------------------------------------------------------------
// DataHandler.h
//
// Abstracts away where market data comes from. The rest of the engine only
// ever talks to this interface, so a live-data handler could be dropped in
// later without touching Strategy/Portfolio/ExecutionHandler at all.
// ---------------------------------------------------------------------------

struct Bar {
    std::string timestamp;
    double open, high, low, close, volume;
};

class DataHandler {
public:
    virtual ~DataHandler() = default;

    // Pushes the next MarketEvent(s) (one per symbol) onto the queue.
    virtual void update_bars(std::deque<std::unique_ptr<Event>>& queue) = 0;

    // Most recent N bars seen so far for a symbol (for strategies that need
    // lookback windows, e.g. moving averages).
    virtual std::vector<Bar> get_latest_bars(const std::string& symbol, int n = 1) const = 0;

    virtual bool continue_backtest() const = 0;
    virtual std::vector<std::string> symbols() const = 0;
};

// Reads one CSV file per symbol with columns: timestamp,open,high,low,close,volume
class CSVDataHandler : public DataHandler {
public:
    CSVDataHandler(const std::vector<std::string>& symbol_list,
                    const std::string& csv_dir)
        : symbol_list_(symbol_list) {
        for (const auto& sym : symbol_list_) {
            load_symbol(sym, csv_dir + "/" + sym + ".csv");
            cursor_[sym] = 0;
        }
        continue_ = true;
    }

    void update_bars(std::deque<std::unique_ptr<Event>>& queue) override {
        bool any = false;
        for (const auto& sym : symbol_list_) {
            size_t& idx = cursor_[sym];
            const auto& bars = all_bars_.at(sym);
            if (idx < bars.size()) {
                const Bar& b = bars[idx];
                latest_bars_[sym].push_back(b);
                queue.push_back(std::make_unique<MarketEvent>(
                    sym, b.timestamp, b.open, b.high, b.low, b.close, b.volume));
                idx++;
                any = true;
            }
        }
        if (!any) continue_ = false;
    }

    std::vector<Bar> get_latest_bars(const std::string& symbol, int n) const override {
        const auto& hist = latest_bars_.at(symbol);
        int size = static_cast<int>(hist.size());
        int start = std::max(0, size - n);
        return std::vector<Bar>(hist.begin() + start, hist.end());
    }

    bool continue_backtest() const override { return continue_; }
    std::vector<std::string> symbols() const override { return symbol_list_; }

    size_t total_bars(const std::string& symbol) const { return all_bars_.at(symbol).size(); }

private:
    void load_symbol(const std::string& symbol, const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open())
            throw std::runtime_error("Could not open data file: " + path);

        std::string line;
        std::getline(file, line); // header
        std::vector<Bar> bars;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string field;
            Bar b{};
            std::getline(ss, b.timestamp, ',');
            std::getline(ss, field, ','); b.open = std::stod(field);
            std::getline(ss, field, ','); b.high = std::stod(field);
            std::getline(ss, field, ','); b.low = std::stod(field);
            std::getline(ss, field, ','); b.close = std::stod(field);
            std::getline(ss, field, ','); b.volume = std::stod(field);
            bars.push_back(b);
        }
        all_bars_[symbol] = std::move(bars);
        latest_bars_[symbol] = {};
    }

    std::vector<std::string> symbol_list_;
    std::map<std::string, std::vector<Bar>> all_bars_;
    std::map<std::string, std::vector<Bar>> latest_bars_;
    std::map<std::string, size_t> cursor_;
    bool continue_ = true;
};
