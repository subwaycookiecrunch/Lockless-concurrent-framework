#pragma once

#include "ring_buffer.hpp"
#include <cstdint>
#include <atomic>
#include <optional>
#include <vector>
#include <algorithm>

namespace lockless {

enum class Side : uint8_t {
    BUY,
    SELL
};

struct Order {
    uint64_t order_id;
    Side side;
    uint64_t price;
    uint64_t quantity;
    uint64_t timestamp;
    
    Order() : order_id(0), side(Side::BUY), price(0), quantity(0), timestamp(0) {}
    Order(uint64_t id, Side s, uint64_t p, uint64_t qty, uint64_t ts)
        : order_id(id), side(s), price(p), quantity(qty), timestamp(ts) {}
};

struct PriceLevel {
    uint64_t price;
    std::atomic<uint64_t> total_quantity;
    
    PriceLevel() : price(0), total_quantity(0) {}
    explicit PriceLevel(uint64_t p) : price(p), total_quantity(0) {}
};

template<size_t QueueSize = 4096>
class OrderBook {
    RingBuffer<Order, QueueSize> order_queue_;
    
    std::atomic<uint64_t> best_bid_;
    std::atomic<uint64_t> best_ask_;
    std::atomic<uint64_t> total_buy_quantity_;
    std::atomic<uint64_t> total_sell_quantity_;
    std::atomic<uint64_t> matched_orders_;
    
public:
    OrderBook() 
        : best_bid_(0), best_ask_(UINT64_MAX), 
          total_buy_quantity_(0), total_sell_quantity_(0),
          matched_orders_(0) {}
    
    bool submit_order(const Order& order) noexcept {
        return order_queue_.try_push(order);
    }
    
    uint64_t process_orders(size_t batch_size = 100) noexcept {
        Order orders[100];
        size_t count = order_queue_.try_pop_batch(orders, std::min(batch_size, size_t(100)));
        
        for (size_t i = 0; i < count; ++i) {
            process_single_order(orders[i]);
        }
        
        return count;
    }
    
    uint64_t get_best_bid() const noexcept {
        return best_bid_.load(std::memory_order_acquire);
    }
    
    uint64_t get_best_ask() const noexcept {
        uint64_t ask = best_ask_.load(std::memory_order_acquire);
        return ask == UINT64_MAX ? 0 : ask;
    }
    
    uint64_t get_total_buy_quantity() const noexcept {
        return total_buy_quantity_.load(std::memory_order_acquire);
    }
    
    uint64_t get_total_sell_quantity() const noexcept {
        return total_sell_quantity_.load(std::memory_order_acquire);
    }
    
    uint64_t get_matched_orders() const noexcept {
        return matched_orders_.load(std::memory_order_acquire);
    }
    
    uint64_t get_spread() const noexcept {
        uint64_t bid = get_best_bid();
        uint64_t ask = get_best_ask();
        if (bid == 0 || ask == 0) return 0;
        return ask > bid ? ask - bid : 0;
    }
    
private:
    void process_single_order(const Order& order) noexcept {
        if (order.side == Side::BUY) {
            uint64_t current_ask = best_ask_.load(std::memory_order_acquire);
            
            if (current_ask != UINT64_MAX && order.price >= current_ask) {
                matched_orders_.fetch_add(1, std::memory_order_relaxed);
                uint64_t sell_qty = total_sell_quantity_.load(std::memory_order_relaxed);
                total_sell_quantity_.fetch_sub(
                    std::min(order.quantity, sell_qty),
                    std::memory_order_relaxed
                );
            } else {
                total_buy_quantity_.fetch_add(order.quantity, std::memory_order_relaxed);
                
                // update best bid via CAS if this order improves it
                uint64_t current_bid = best_bid_.load(std::memory_order_acquire);
                while (order.price > current_bid) {
                    if (best_bid_.compare_exchange_weak(current_bid, order.price,
                                                         std::memory_order_release,
                                                         std::memory_order_acquire)) {
                        break;
                    }
                }
            }
        } else {
            uint64_t current_bid = best_bid_.load(std::memory_order_acquire);
            
            if (current_bid != 0 && order.price <= current_bid) {
                matched_orders_.fetch_add(1, std::memory_order_relaxed);
                uint64_t buy_qty = total_buy_quantity_.load(std::memory_order_relaxed);
                total_buy_quantity_.fetch_sub(
                    std::min(order.quantity, buy_qty),
                    std::memory_order_relaxed
                );
            } else {
                total_sell_quantity_.fetch_add(order.quantity, std::memory_order_relaxed);
                
                uint64_t current_ask = best_ask_.load(std::memory_order_acquire);
                while (order.price < current_ask) {
                    if (best_ask_.compare_exchange_weak(current_ask, order.price,
                                                         std::memory_order_release,
                                                         std::memory_order_acquire)) {
                        break;
                    }
                }
            }
        }
    }
};

} // namespace lockless
