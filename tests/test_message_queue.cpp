#include "lockless/message_queue.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <cstring>

struct MyMsg {
    int id;
    char data[64];
};

void test_basic() {
    lockless::ZeroCopyQueue<16, 1024*1024> queue;
    
    // Producer
    void* ptr = queue.prepare_message(sizeof(MyMsg));
    MyMsg* msg = new (ptr) MyMsg();
    msg->id = 42;
    #ifdef _MSC_VER
        strcpy_s(msg->data, sizeof(msg->data), "Hello Zero-Copy");
    #else
        std::strcpy(msg->data, "Hello Zero-Copy");
    #endif
    
    assert(queue.push_message(msg, sizeof(MyMsg)));
    
    // Consumer
    lockless::MessageHandle handle;
    assert(queue.pop_message(handle));
    assert(handle.size == sizeof(MyMsg));
    
    MyMsg* received = static_cast<MyMsg*>(queue.get_message_data(handle));
    assert(received->id == 42);
    assert(strcmp(received->data, "Hello Zero-Copy") == 0);
    
    std::cout << "MessageQueue Basic test passed." << std::endl;
}

void test_concurrent() {
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 1000;
    
    lockless::ZeroCopyQueue<4096, 1024*1024*10> queue; // 10MB arena
    std::atomic<int> sent_count{0};
    std::atomic<int> received_count{0};

    auto producer = [&](int id) {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            void* ptr = queue.prepare_message(sizeof(int));
            *static_cast<int*>(ptr) = id * OPS_PER_THREAD + i;
            while (!queue.push_message(ptr, sizeof(int))) {
                std::this_thread::yield();
            }
            sent_count++;
        }
    };

    auto consumer = [&]() {
        lockless::MessageHandle handle;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while (!queue.pop_message(handle)) {
                std::this_thread::yield();
            }
            received_count++;
            int* val = static_cast<int*>(queue.get_message_data(handle));
            (void)val; // Use value
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(producer, i);
        threads.emplace_back(consumer);
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(sent_count == NUM_THREADS * OPS_PER_THREAD);
    assert(received_count == NUM_THREADS * OPS_PER_THREAD);

    std::cout << "MessageQueue Concurrent test passed. Total messages: " << sent_count << std::endl;
}

int main() {
    test_basic();
    test_concurrent();
    return 0;
}
