#include "lockless/ring_buffer.hpp"
#include <iostream>

int main() {
    lockless::RingBuffer<int, 4> buffer;
    std::cout << "Created buffer" << std::endl;
    return 0;
}
