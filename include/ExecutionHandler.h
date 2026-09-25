#pragma once
#include <deque>
#include <memory>
#include <map>
#include <string>
#include <cmath>
#include "Event.h"

// ---------------------------------------------------------------------------
// ExecutionHandler.h
//
// Simulates sending an OrderEvent to a broker/exchange and getting a fill
// back. In a live system this would be replaced by a real broker API; here
// it fills every order immediately at the last known price plus a slippage
// and commission model, and emits a FillEvent.
// ---------------------------------------------------------------------------

class ExecutionHandler {
public:
    virtual ~ExecutionHandler() = default;
    virtual void execute_order(const OrderEvent& event,
                                const std::string& timestamp,
                                double market_price,
                                std::deque<std::unique_ptr<Event>>& queue) = 0;
};

class SimulatedExecutionHandler : public ExecutionHandler {
public:
    // commission_per_share + commission_flat models a typical broker fee;
    // slippage_bps models market impact / bid-ask spread as basis points
    // of the fill price, applied against the trader (worse fills).
    SimulatedExecutionHandler(double commission_per_share = 0.005,
                                double commission_flat = 1.0,
                                double slippage_bps = 1.0)
        : commission_per_share_(commission_per_share),
          commission_flat_(commission_flat),
          slippage_bps_(slippage_bps) {}

    void execute_order(const OrderEvent& event,
                        const std::string& timestamp,
                        double market_price,
                        std::deque<std::unique_ptr<Event>>& queue) override {
        double slip = market_price * (slippage_bps_ / 10000.0);
        double fill_price = (event.direction == OrderDirection::BUY)
                                 ? market_price + slip
                                 : market_price - slip;
        double slippage_cost = std::abs(fill_price - market_price) * event.quantity;
        double commission = commission_flat_ + commission_per_share_ * event.quantity;

        queue.push_back(std::make_unique<FillEvent>(
            timestamp, event.symbol, "SIMULATED", event.quantity,
            event.direction, fill_price, commission, slippage_cost));
    }

private:
    double commission_per_share_;
    double commission_flat_;
    double slippage_bps_;
};
