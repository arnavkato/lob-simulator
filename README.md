# LOB — Limit Order Book Simulator

A from-scratch limit order book and matching engine in C++, with a live
[Dear ImGui](https://github.com/ocornut/imgui) visualization: a depth-of-market
ladder, a time & sales tape, and a candlestick chart, all driven by a
randomized order-flow simulator.

## Build

Requires a C++17 compiler, GLFW, and Dear ImGui.

```sh
# 1. install GLFW (Debian/Ubuntu)
sudo apt-get install -y libglfw3-dev

# 2. vendor Dear ImGui (gitignored — cloned locally)
git clone --depth 1 https://github.com/ocornut/imgui.git

# 3. build and run
make
./sim
```

## Layout

- `orderbook.h` / `orderbook.cpp` — the order book: add, cancel, match
  (market + marketable-limit), and depth / best-price / trade queries.
- `simulate_orders.cpp` — the randomized order-flow simulator and the ImGui GUI
  (DOM ladder, time & sales tape, candlestick chart).
