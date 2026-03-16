cmake_minimum_required(VERSION 3.15)
project(Lockless VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

option(ENABLE_TSAN "Enable ThreadSanitizer for race detection" OFF)

if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Werror -Wpedantic)
    
    if(ENABLE_TSAN)
        add_compile_options(-fsanitize=thread -g)
        add_link_options(-fsanitize=thread)
        add_compile_definitions(ENABLE_TSAN)
    endif()
endif()

function(lockless_add_executable target_name)
    add_executable(${target_name} ${ARGN})
    if(NOT MSVC)
        target_link_libraries(${target_name} pthread atomic)
    endif()
endfunction()

# --- Tests ---

enable_testing()

lockless_add_executable(test_stack tests/test_stack.cpp)
add_test(NAME test_stack COMMAND test_stack)

lockless_add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
add_test(NAME test_ring_buffer COMMAND test_ring_buffer)

lockless_add_executable(test_hash_map tests/test_hash_map.cpp)
add_test(NAME test_hash_map COMMAND test_hash_map)

lockless_add_executable(test_message_queue tests/test_message_queue.cpp)
add_test(NAME test_message_queue COMMAND test_message_queue)

lockless_add_executable(test_cache_utils tests/test_cache_utils.cpp)
add_test(NAME test_cache_utils COMMAND test_cache_utils)

lockless_add_executable(test_ordering_validator tests/test_ordering_validator.cpp)
add_test(NAME test_ordering_validator COMMAND test_ordering_validator)

lockless_add_executable(test_array_stack tests/test_array_stack.cpp)
add_test(NAME test_array_stack COMMAND test_array_stack)

lockless_add_executable(test_object_pool tests/test_object_pool.cpp)
add_test(NAME test_object_pool COMMAND test_object_pool)

lockless_add_executable(test_order_book tests/test_order_book.cpp)
add_test(NAME test_order_book COMMAND test_order_book)

lockless_add_executable(test_epoch_reclaimer tests/test_epoch_reclaimer.cpp)
add_test(NAME test_epoch_reclaimer COMMAND test_epoch_reclaimer)

lockless_add_executable(test_spsc_queue tests/test_spsc_queue.cpp)
add_test(NAME test_spsc_queue COMMAND test_spsc_queue)

# --- Benchmarks ---

lockless_add_executable(benchmark_ring_buffer benchmarks/benchmark_ring_buffer.cpp)
lockless_add_executable(benchmark_stack benchmarks/benchmark_stack.cpp)
lockless_add_executable(benchmark_order_book benchmarks/benchmark_order_book.cpp)

# --- Examples ---

lockless_add_executable(ring_buffer_example examples/ring_buffer_example.cpp)
lockless_add_executable(order_book_example examples/order_book_example.cpp)
