#include <iostream>
#include <iomanip>
#include <random>
#include "orderbook.h" // OrderBook, Order, Side

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

class Simulator
{
    OrderBook book;
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int64_t> offset_dist{0, 20}; // ticks into the book for passive limits
    std::bernoulli_distribution aggressive_dist{0.1};          // 10% of limits cross instead of resting
    std::geometric_distribution<uint32_t> size_dist{0.1};      // skewed towards smaller orders with infrequent big orders
    std::bernoulli_distribution side_dist{0.5};                // 50/50 split with no skew on side
    std::bernoulli_distribution limit_dist{0.8};               // 80% of orders are limit orders
    std::bernoulli_distribution action_dist{0.7};
    uint64_t next_id{0};                                      // for keeping track of order ids
    std::exponential_distribution<double> interval_dist{25.0}; // avg 25 actions/sec (mean gap = 1/25 s)
    double time_until_next{0.0};

    int64_t fair_value{10000};                             // drifting fundamental the market tracks
    std::normal_distribution<double> drift_dist{0.0, 4.0}; // random-walk step per action (ticks)

public:
    std::vector<std::pair<int64_t, uint32_t>> bids(int n) const { return book.get_bids(n); }
    std::vector<std::pair<int64_t, uint32_t>> asks(int n) const { return book.get_asks(n); }
    std::optional<int64_t> best_bid() const { return book.get_best_bid(); }
    std::optional<int64_t> best_ask() const { return book.get_best_ask(); }
    std::vector<Trade> trades(int n) const { return book.get_trades(n); }

    void advance(double dt)
    {
        time_until_next -= dt;
        while (time_until_next <= 0.0)
        {
            step();
            time_until_next += interval_dist(rng); // schedule the next gap
        }
    }

private:
    static constexpr int64_t SEED_MID{10000}; // reference price when the book is empty
    static constexpr int64_t DEPTH_K{4};      // smaller = liquidity ramps up faster with depth

    Order generate()
    {
        uint32_t base_size{std::min(1 + size_dist(rng), 100u)}; // base quantity (min 1)
        uint32_t size{base_size};
        Side side{Side::Sell};
        if (side_dist(rng))
        {
            side = Side::Buy;
        }
        bool is_limit{limit_dist(rng)};
        uint64_t id{next_id++};
        std::chrono::microseconds timestamp{std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())}; // get the current time

        // price around the drifting fair value (not the current mid) so the market
        // gets pulled along as fair value wanders
        auto best_bid = book.get_best_bid();
        auto best_ask = book.get_best_ask();
        int64_t anchor = fair_value;

        int64_t price{anchor}; // market orders: price is ignored by match

        if (is_limit)
        {
            if (aggressive_dist(rng))
            {
                // ~10%: price through the opposite side so it actually trades
                price = (side == Side::Buy) ? best_ask.value_or(anchor)
                                            : best_bid.value_or(anchor);
            }
            else
            {
                // passive: rest around fair value; crosses (and moves price) when fair value drifts past the book
                int64_t offset = offset_dist(rng);
                price = (side == Side::Buy) ? anchor - 1 - offset
                                            : anchor + 1 + offset;
                size = base_size * (1 + offset / DEPTH_K); // thicker liquidity deeper in the book
            }
        }

        return {id, side, price, size, timestamp, is_limit};
    }

    void step()
    {
        // random-walk the fundamental, with a gentle pull back toward the seed
        fair_value += (int64_t)drift_dist(rng) - (fair_value - SEED_MID) / 400;

        bool action{action_dist(rng)};
        if (action)
        {
            Order o{generate()};
            book.match(o);
        }
        else
        {
            auto order_to_cancel{book.get_random_order(rng)};
            if (order_to_cancel)
            {
                book.pull(*order_to_cancel);
            }
        }
    }
};

int main()
{
    // ---- GLFW + OpenGL3 window setup (ImGui boilerplate) ----
    if (!glfwInit())
    {
        std::cerr << "glfwInit failed\n";
        return 1;
    }
    const char *glsl_version{"#version 130"};
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow *window{glfwCreateWindow(1100, 1000, "LOB", nullptr, nullptr)};
    if (!window)
    {
        std::cerr << "glfwCreateWindow failed (check DISPLAY)\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().FontGlobalScale = 1.3f; // bump text size for readability
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    Simulator sim;

    // ---- main loop: one frame per iteration ----
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        float dt = ImGui::GetIO().DeltaTime;
        sim.advance(dt); // advance the sim in real time

        // ---- DOM-style depth ladder (fills the whole window) ----
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Order Book", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        auto bids = sim.bids(10); // best-first: highest bid first
        auto asks = sim.asks(10); // best-first: lowest ask first

        auto bb = sim.best_bid();
        auto ba = sim.best_ask();
        if (bb && ba)
            ImGui::Text("Bid %lld     Ask %lld     Spread %lld",
                        (long long)*bb, (long long)*ba, (long long)(*ba - *bb));
        else
            ImGui::TextDisabled("(book empty)");
        ImGui::Spacing();

        // ---- market depth chart (cumulative order book depth) ----
        {
            auto dbids = sim.bids(40); // best-first: descending price
            auto dasks = sim.asks(40); // best-first: ascending price
            ImGui::BeginChild("chart", ImVec2(0, 340), true);
            if (!dbids.empty() && !dasks.empty())
            {
                // cumulative depth outward from the spread
                std::vector<std::pair<int64_t, double>> bc, ac;
                double run = 0;
                for (auto &b : dbids)
                {
                    run += b.second;
                    bc.push_back({b.first, run});
                }
                run = 0;
                for (auto &a : dasks)
                {
                    run += a.second;
                    ac.push_back({a.first, run});
                }

                int64_t pmin = bc.back().first; // deepest (lowest) bid price
                int64_t pmax = ac.back().first; // deepest (highest) ask price
                double cmax = std::max(bc.back().second, ac.back().second);
                if (pmax <= pmin)
                    pmax = pmin + 1;
                if (cmax <= 0.0)
                    cmax = 1.0;

                ImVec2 org = ImGui::GetCursorScreenPos();
                ImVec2 area = ImGui::GetContentRegionAvail();
                ImDrawList *dl = ImGui::GetWindowDrawList();
                float base = org.y + area.y;
                auto X = [&](int64_t p) // price -> screen x
                { return org.x + area.x * (float)(p - pmin) / (float)(pmax - pmin); };
                auto Y = [&](double c) // cumulative volume -> screen y
                { return org.y + area.y * (float)(1.0 - c / cmax); };

                const ImU32 gfill = IM_COL32(90, 200, 110, 80), gline = IM_COL32(90, 210, 120, 255);
                const ImU32 rfill = IM_COL32(230, 90, 90, 80), rline = IM_COL32(230, 100, 100, 255);

                // bids: staircase over [p(i+1), p(i)] at cumulative height c(i), rising to the left
                for (size_t i = 0; i + 1 < bc.size(); ++i)
                {
                    float xL = X(bc[i + 1].first), xR = X(bc[i].first), y = Y(bc[i].second);
                    dl->AddRectFilled(ImVec2(xL, y), ImVec2(xR, base), gfill);
                    dl->AddLine(ImVec2(xL, y), ImVec2(xR, y), gline, 2.0f);
                    dl->AddLine(ImVec2(xL, y), ImVec2(xL, Y(bc[i + 1].second)), gline, 2.0f);
                }
                // asks: staircase over [q(i), q(i+1)] at cumulative height d(i), rising to the right
                for (size_t i = 0; i + 1 < ac.size(); ++i)
                {
                    float xL = X(ac[i].first), xR = X(ac[i + 1].first), y = Y(ac[i].second);
                    dl->AddRectFilled(ImVec2(xL, y), ImVec2(xR, base), rfill);
                    dl->AddLine(ImVec2(xL, y), ImVec2(xR, y), rline, 2.0f);
                    dl->AddLine(ImVec2(xR, y), ImVec2(xR, Y(ac[i + 1].second)), rline, 2.0f);
                }
            }
            else
            {
                ImGui::TextDisabled("waiting for a two-sided book...");
            }
            ImGui::EndChild();
        }

        // largest size across the visible levels, for scaling the depth shading
        uint32_t max_sz = 1;
        for (auto &a : asks)
            max_sz = std::max(max_sz, a.second);
        for (auto &b : bids)
            max_sz = std::max(max_sz, b.second);

        const ImVec4 green{0.40f, 0.85f, 0.45f, 1.0f};
        const ImVec4 red{0.92f, 0.42f, 0.42f, 1.0f};

        // left pane: DOM ladder
        ImGui::BeginChild("dom_pane", ImVec2(440, 0), true);
        if (ImGui::BeginTable("dom", 3,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableSetupColumn("Bid Size");
            ImGui::TableSetupColumn("Price");
            ImGui::TableSetupColumn("Ask Size");
            ImGui::TableHeadersRow();

            // ASKS on top: highest price first, best (lowest) ask nearest the middle
            for (auto it = asks.rbegin(); it != asks.rend(); ++it)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(red, "%lld", (long long)it->first);
                ImGui::TableSetColumnIndex(2);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                       ImGui::GetColorU32(ImVec4(0.70f, 0.25f, 0.25f, (float)it->second / max_sz)));
                ImGui::Text("%u", it->second);
            }

            // BIDS below: best (highest) bid first, descending down the ladder
            for (auto &b : bids)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                       ImGui::GetColorU32(ImVec4(0.20f, 0.60f, 0.30f, (float)b.second / max_sz)));
                ImGui::Text("%u", b.second);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(green, "%lld", (long long)b.first);
            }

            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // right pane: time & sales tape
        ImGui::BeginChild("tape_pane", ImVec2(0, 0), true);
        ImGui::Text("Time & Sales");
        if (ImGui::BeginTable("tape", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Price");
            ImGui::TableSetupColumn("Size");
            ImGui::TableHeadersRow();
            for (auto &t : sim.trades(50)) // newest first
            {
                const ImVec4 &col = (t.aggressor == Side::Buy) ? green : red;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(col, "%lld", (long long)t.price);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(col, "%u", t.size);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::End();

        // ---- render ----
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // ---- cleanup ----
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
