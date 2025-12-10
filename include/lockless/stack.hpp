#pragma once

#include "array_stack.hpp"

namespace lockless {

// Alias the new implementation to LockFreeStack for backward compatibility
// but enforce the capacity requirement.
template<typename T>
using LockFreeStack = ArrayLockFreeStack<T>;

} // namespace lockless
