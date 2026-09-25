#pragma once
#include <string>
#include <chrono>

// ---------------------------------------------------------------------------
// Event.h
//
// All communication between the components of the backtesting engine
// (DataHandler -> Strategy -> Portfolio -> ExecutionHandler -> Portfolio)
// happens through Event objects placed on a single central queue. This is
// the core of the "event-driven" design: nothing pulls data on its own
// schedule, everything reacts to an event arriving.
// ---------------------------------------------------------------------------

enum class EventType { MARKET, SIGNAL, ORDER, FILL };
enum class SignalDirection { LONG, SHORT, EXIT };
enum class OrderDirection { BUY, SELL };

struct Event {
    virtual ~Event() = default;
    virtual EventType type() const = 0;
};

// Emitted by the DataHandler each time a new bar of market data is available.
struct MarketEvent : public Event {
    std::string symbol;
    std::string timestamp;
    double open, high, low, close, volume;

    MarketEvent(std::string sym, std::string ts, double o, double h,
                double l, double c, double v)
        : symbol(std::move(sym)), timestamp(std::move(ts)),
          open(o), high(h), low(l), close(c), volume(v) {}

    EventType type() const override { return EventType::MARKET; }
};

// Emitted by a Strategy when it decides it wants to be long/short/flat.
struct SignalEvent : public Event {
    std::string symbol;
    std::string timestamp;
    SignalDirection direction;
    double strength; // optional sizing hint in [0,1], defaults to 1.0

    SignalEvent(std::string sym, std::string ts, SignalDirection dir,
                double strength_ = 1.0)
        : symbol(std::move(sym)), timestamp(std::move(ts)),
          direction(dir), strength(strength_) {}

    EventType type() const override { return EventType::SIGNAL; }
};

// Emitted by the Portfolio when it turns a signal into an actual order,
// with position sizing already applied.
struct OrderEvent : public Event {
    std::string symbol;
    std::string order_type; // "MKT" (only market orders are simulated here)
    long quantity;
    OrderDirection direction;

    OrderEvent(std::string sym, std::string otype, long qty, OrderDirection dir)
        : symbol(std::move(sym)), order_type(std::move(otype)),
          quantity(qty), direction(dir) {}

    EventType type() const override { return EventType::ORDER; }
};

// Emitted by the ExecutionHandler once an order has been "filled" by the
// simulated market, including transaction costs and slippage.
struct FillEvent : public Event {
    std::string timestamp;
    std::string symbol;
    std::string exchange;
    long quantity;
    OrderDirection direction;
    double fill_price;
    double commission;
    double slippage_cost;

    FillEvent(std::string ts, std::string sym, std::string exch, long qty,
               OrderDirection dir, double price, double comm, double slip)
        : timestamp(std::move(ts)), symbol(std::move(sym)),
          exchange(std::move(exch)), quantity(qty), direction(dir),
          fill_price(price), commission(comm), slippage_cost(slip) {}

    EventType type() const override { return EventType::FILL; }
};
