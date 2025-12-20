#include "lockless/order_book.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>

using namespace lockless;

void test_basic_order_submission() {
    OrderBook<1024> book;
    
    // Submit a buy order
    Order buy_order(1, Side::BUY, 10000, 100, 1);
    assert(book.submit_order(buy_order));
    
    // Process it
    assert(book.process_orders(10) == 1);
    
    // Check that it's in the book
    assert(book.get_best_bid() == 10000);
    assert(book.get_total_buy_quantity() == 100);
    
    std::cout << "✓ Basic order submission test passed" << std::endl;
}

void test_order_matching() {
    OrderBook<1024> book;
    
    // Submit a sell order at price 10100
    Order sell_order(1, Side::SELL, 10100, 50, 1);
    book.submit_order(sell_order);
    book.process_orders(10);
    
    assert(book.get_best_ask() == 10100);
    assert(book.get_total_sell_quantity() == 50);
    
    // Submit a buy order at price 10100 (should match)
    Order buy_order(2, Side::BUY, 10100, 50, 2);
    book.submit_order(buy_order);
    book.process_orders(10);
    
    // Should have matched
    assert(book.get_matched_orders() >= 1);
    
    std::cout << "✓ Order matching test passed (matched: " 
              << book.get_matched_orders() << " orders)" << std::endl;
}

void test_spread_calculation() {
    OrderBook<1024> book;
    
    // Submit buy at 10000
    Order buy(1, Side::BUY, 10000, 100, 1);
    book.submit_order(buy);
    book.process_orders(10);
    
    // Submit sell at 10050
    Order sell(2, Side::SELL, 10050, 100, 2);
    book.submit_order(sell);
    book.process_orders(10);
    
    uint64_t spread = book.get_spread();
    assert(spread == 50);
    
    std::cout << "✓ Spread calculation test passed (spread: " << spread << ")" << std::endl;
}

void test_concurrent_submission() {
    OrderBook<4096> book;
    constexpr int NUM_THREADS = 4;
    constexpr int ORDERS_PER_THREAD = 1000;
    
    auto submit_orders = [&](int thread_id) {
        for (int i = 0; i < ORDERS_PER_THREAD; ++i) {
            uint64_t order_id = thread_id * ORDERS_PER_THREAD + i;
            Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
            uint64_t price = 10000 + (i % 100);
            uint64_t quantity = 10 + (i % 50);
            
            Order order(order_id, side, price, quantity, order_id);
            
            // Retry until we can submit
            while (!book.submit_order(order)) {
                std::this_thread::yield();
            }
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(submit_orders, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Process all orders
    uint64_t total_processed = 0;
    while (true) {
        uint64_t processed = book.process_orders(100);
        total_processed += processed;
        if (processed == 0) break;
    }
    
    assert(total_processed == NUM_THREADS * ORDERS_PER_THREAD);
    
    std::cout << "✓ Concurrent submission test passed (processed: " 
              << total_processed << " orders)" << std::endl;
    std::cout << "  Best Bid: " << book.get_best_bid() << std::endl;
    std::cout << "  Best Ask: " << book.get_best_ask() << std::endl;
    std::cout << "  Matched Orders: " << book.get_matched_orders() << std::endl;
}

int main() {
    std::cout << "=== Order Book Tests ===" << std::endl;
    
    test_basic_order_submission();
    test_order_matching();
    test_spread_calculation();
    test_concurrent_submission();
    
    std::cout << "\nAll order book tests passed!" << std::endl;
    return 0;
}
