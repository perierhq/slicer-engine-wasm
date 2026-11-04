// Serial oneTBB shim: tbb::concurrent_unordered_map / concurrent_unordered_multimap. With one thread the
// standard containers have the required semantics (Orca only uses find/insert/size on them).
#pragma once

#include <functional>
#include <memory>
#include <unordered_map>

#include "detail/_serial.h"

namespace oneapi {
namespace tbb {

template<typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<std::pair<const Key, T>>>
using concurrent_unordered_map = std::unordered_map<Key, T, Hash, KeyEqual, Allocator>;

template<typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<std::pair<const Key, T>>>
using concurrent_unordered_multimap = std::unordered_multimap<Key, T, Hash, KeyEqual, Allocator>;

} // namespace tbb
} // namespace oneapi
