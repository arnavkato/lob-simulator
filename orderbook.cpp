#include "orderbook.h"

// get the best bid and make sure book is not empty
std::optional<int64_t> OrderBook::get_best_bid() const
{
    if (bids.empty())
    {
        return std::nullopt;
    }
    return bids.rbegin()->first;
}

// get the best ask and make sure book is not empty
std::optional<int64_t> OrderBook::get_best_ask() const
{
    if (asks.empty())
    {
        return std::nullopt;
    }
    return asks.begin()->first;
}

// add orders by adding to the price list then storing in order_index for fast lookup
void OrderBook::add(const Order &o)
{
    if (o.side == Side::Buy)
    {
        bids[o.price].push_back(o);
        order_index[o.id] = std::prev(bids[o.price].end());
    }
    else
    {
        asks[o.price].push_back(o);
        order_index[o.id] = std::prev(asks[o.price].end());
    }
}

// pull orders by searching with order_index then erasing
bool OrderBook::pull(const Order &o)
{
    auto it{order_index.find(o.id)};

    if (it == order_index.end())
    {
        return false;
    }

    // make sure we dont have to call o after erasing
    const uint64_t id{o.id};
    const int64_t price{o.price};
    const Side side{o.side};

    if (side == Side::Buy)
    {
        bids[price].erase(it->second);

        if (bids[price].empty())
        {
            bids.erase(price);
        }
    }
    else
    {
        asks[price].erase(it->second);

        if (asks[price].empty())
        {
            asks.erase(price);
        }
    }
    order_index.erase(id);
    return true;
}

// take orders by reducing size at the best bid/ask FIFO by the same amount as the mkt order
void OrderBook::match(const Order &o)
{
    uint32_t size_of_unfilled_mkt_order{o.size};

    if (o.side == Side::Buy)
    {
        while (size_of_unfilled_mkt_order > 0)
        {
            auto best_ask{get_best_ask()};

            if (!best_ask)
            {
                break;
            }
            if (best_ask > o.price && o.is_limit)
            {
                break;
            }

            auto &level{asks[*best_ask]};
            Order &front{level.front()};
            uint32_t fill{std::min(size_of_unfilled_mkt_order, front.size)};
            front.size -= fill;
            size_of_unfilled_mkt_order -= fill;
            last_price = front.price;
            record_trade(front.price, fill, o.side);

            if (front.size == 0)
            {
                pull(front);
            }
        }
    }
    else
    {
        while (size_of_unfilled_mkt_order > 0)
        {
            auto best_bid{get_best_bid()};

            if (!best_bid)
            {
                break;
            }
            if (best_bid < o.price && o.is_limit)
            {
                break;
            }

            auto &level{bids[*best_bid]};
            Order &front{level.front()};
            uint32_t fill{std::min(size_of_unfilled_mkt_order, front.size)};
            front.size -= fill;
            size_of_unfilled_mkt_order -= fill;
            last_price = front.price;
            record_trade(front.price, fill, o.side);

            if (front.size == 0)
            {
                pull(front);
            }
        }
    }

    if (size_of_unfilled_mkt_order > 0 && o.is_limit)
    {
        Order remainder{o};
        remainder.size = size_of_unfilled_mkt_order;
        add(remainder);
    }
}

std::vector<std::pair<int64_t, uint32_t>> OrderBook::get_bids(int depth) const
{
    std::vector<std::pair<int64_t, uint32_t>> result;
    for (auto it{bids.rbegin()}; it != bids.rend() && depth-- > 0; ++it)
    {
        uint32_t total = 0;
        for (const auto &order : it->second) // sum sizes at this level
            total += order.size;
        result.push_back({it->first, total}); // {price, total size}
    }
    return result;
}

std::vector<std::pair<int64_t, uint32_t>> OrderBook::get_asks(int depth) const
{
    std::vector<std::pair<int64_t, uint32_t>> result;
    for (auto it{asks.begin()}; it != asks.end() && depth-- > 0; ++it)
    {
        uint32_t total = 0;
        for (const auto &order : it->second)
            total += order.size;
        result.push_back({it->first, total});
    }
    return result;
}

std::optional<Order> OrderBook::get_random_order(std::mt19937 &rng) const
{
    if (order_index.empty())
        return std::nullopt; // nothing resting

    std::uniform_int_distribution<size_t> pick(0, order_index.size() - 1);
    auto it = order_index.begin();
    std::advance(it, pick(rng)); // hop forward a random # of steps

    return *it->second;
}

// append an execution, keeping only the most recent ones
void OrderBook::record_trade(int64_t price, uint32_t size, Side aggressor)
{
    trades.push_back({price, size, aggressor});
    if (trades.size() > 200)
        trades.pop_front();
}

// most recent n trades, newest first
std::vector<Trade> OrderBook::get_trades(int n) const
{
    std::vector<Trade> result;
    for (auto it = trades.rbegin(); it != trades.rend() && n-- > 0; ++it)
        result.push_back(*it);
    return result;
}