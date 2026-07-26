#pragma once

#include <cstdint>
#include <chrono>
#include <map>
#include <unordered_map>
#include <list>
#include <optional>
#include <algorithm>
#include <vector>
#include <random>
#include <deque>

enum class Side
{
    Buy,
    Sell
};

struct Order
{
    uint64_t id{};
    Side side{};
    int64_t price{};
    uint32_t size{};
    std::chrono::microseconds timestamp{};
    bool is_limit{};
};

struct Trade
{
    int64_t price{};
    uint32_t size{};
    Side aggressor{}; // side of the incoming order that caused the fill
};

class OrderBook
{
    std::unordered_map<uint64_t, std::list<Order>::iterator> order_index;
    std::map<int64_t, std::list<Order>> bids;
    std::map<int64_t, std::list<Order>> asks;
    int64_t last_price{};
    std::deque<Trade> trades; // recent executions, bounded

    void record_trade(int64_t price, uint32_t size, Side aggressor);

public:
    std::optional<int64_t>
    get_best_bid() const;

    std::optional<int64_t>
    get_best_ask() const;

    void
    add(const Order &o);

    bool
    pull(const Order &o);

    void
    match(const Order &o);

    std::vector<std::pair<int64_t, uint32_t>>
    get_bids(int depth) const;

    std::vector<std::pair<int64_t, uint32_t>>
    get_asks(int depth) const;

    std::optional<Order>
    get_random_order(std::mt19937 &rng) const;

    std::vector<Trade>
    get_trades(int n) const;
};