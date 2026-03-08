#pragma once
#include <vector>
#include <cstddef>

#ifdef USE_ROBIN
  #include "RobinHoodHash.h"
  template <class K>
  using HashTable = RobinHoodHashMap<K, std::vector<std::size_t>>;

#elif defined(USE_HOP)
  #include "HopscotchHash.h"
  template <class K>
  using HashTable = HopscotchHashMap<K, std::vector<std::size_t>>;

#elif defined(USE_CUCKOO)
  #include "CuckooHash.h"
  template <class K>
  using HashTable = cuckoo::CuckooMultiMap<K, std::vector<std::size_t>>;

#elif defined(USE_STD)
  #include <unordered_map>
  template <class K>
  using HashTable = std::unordered_map<K, std::vector<std::size_t>>;

  #elif defined(USE_UNC)
  #include "UnchainedHash.h"
  template <class K>
  using HashTable = UnchainedHashTable<K, std::vector<std::size_t>>;

#else
  #error "Define one of: USE_ROBIN, USE_HOP, USE_CUCKOO, USE_STD"
#endif
