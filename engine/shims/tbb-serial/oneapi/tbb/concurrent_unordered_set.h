// Serial oneTBB shim: tbb::concurrent_unordered_set / concurrent_unordered_multiset (see
// concurrent_unordered_map.h).
#pragma once

#include <functional>
#include <memory>
#include <unordered_set>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

template<typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<Key>>
using concurrent_unordered_set = std::unordered_set<Key, Hash, KeyEqual, Allocator>;

template<typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<Key>>
using concurrent_unordered_multiset = std::unordered_multiset<Key, Hash, KeyEqual, Allocator>;

} // namespace tbb
} // namespace oneapi
