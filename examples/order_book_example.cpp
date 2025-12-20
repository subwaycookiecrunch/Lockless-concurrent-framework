// Simple example demonstrating Order Book usage
#include "lockless/order_book.hpp"
#include <iostream>
#include <thread>
#include <vector>

using namespace lockless;

void basic_trading_example() {
    OrderBook<1024> book;
    
    std::cout << "Submitting initial orders..." << std::endl;
    
    // Submit some buy orders
    book.submit_order(Order(1, Side::BUY, 9950, 100, 1));
    book.submit_order(Order(2, Side::BUY, 9975, 200, 2));
    book.submit_order(Order(3, Side::BUY, 10000, 150, 3));
    
    // Submit some sell orders
    book.submit_order(Order(4, Side::SELL, 10050, 100, 4));
    book.submit_order(Order(5, Side::SELL, 10025, 200, 5));
    book.submit_order(Order(6, Side::SELL, 10010, 150, 6));
    
    // Process orders
    book.process_orders(10);
    
    std::cout << "Best Bid: " << book.get_best_bid() << std::endl;
    std::cout << "Best Ask: " << book.get_best_ask() << std::endl;
    std::cout << "Spread: " << book.get_spread() << std::endl;
    std::cout << "Matched Orders: " << book.get_matched_orders() << std::endl;
}

void matching_example() {
    OrderBook<1024> book;
    
    std::cout << "\nDemonstrating order matching..." << std::endl;
    
    // Submit a sell order at 10000
    book.submit_order(Order(1, Side::SELL, 10000, 100, 1));
    book.process_orders(10);
    
    std::cout << "After sell order - Best Ask: " << book.get_best_ask() << std::endl;
    
    // Submit a buy order at 10000 (should match!)
    book.submit_order(Order(2, Side::BUY, 10000, 100, 2));
    book.process_orders(10);
    
    std::cout << "After matching buy order - Matched Orders: " << book.get_matched_orders() << std::endl;
}

void concurrent_trading_example() {
    OrderBook<4096> book;
    
    std::cout << "\nConcurrent trading simulation..." << std::endl;
    
    constexpr int NUM_TRADERS = 4;
    constexpr int ORDERS_PER_TRADER = 500;
    
    std::vector<std::thread> traders;
    
    for (int i = 0; i < NUM_TRADERS; ++i) {
        traders.emplace_back([&, i]() {
            for (int j = 0; j < ORDERS_PER_TRADER; ++j) {
                uint64_t order_id = i * ORDERS_PER_TRADER + j;
                Side side = (j % 2 == 0) ? Side::BUY : Side::SELL;
                uint64_t price = 10000 + (j % 100) - 50; // Prices around 10000
                uint64_t quantity = 10 + (j % 90);
                
                Order order(order_id, side, price, quantity, order_id);
                
                while (!book.submit_order(order)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Join all traders
    for (auto& t : traders) {
        t.join();
    }
    
    // Process all submitted orders
    uint64_t total_processed = 0;
    while (true) {
        uint64_t processed = book.process_orders(100);
        total_processed += processed;
        if (processed == 0) break;
    }
    
    std::cout << "Total orders processed: " << total_processed << std::endl;
    std::cout << "Final Best Bid: " << book.get_best_bid() << std::endl;
    std::cout << "Final Best Ask: " << book.get_best_ask() << std::endl;
    std::cout << "Final Spread: " << book.get_spread() << std::endl;
    std::cout << "Matched Orders: " << book.get_matched_orders() << std::endl;
}

int main() {
    std::cout << "=== Order Book Examples ===" << std::endl;
    
    basic_trading_example();
    matching_example();
    concurrent_trading_example();
    
    return 0;
}
