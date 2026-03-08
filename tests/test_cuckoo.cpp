#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>  
#include <catch2/catch_test_case_info.hpp>

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <random>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "CuckooHash.h"

using Key = uint64_t;
using Val = uint64_t;

using Map = cuckoo::CuckooMultiMap<Key, std::vector<Val>>;

//prints one-line summary per test
class SimpleReporter : public Catch::EventListenerBase {
    using Base = Catch::EventListenerBase;
public:
    using Base::Base;

    void testCaseStarting(Catch::TestCaseInfo const& info) override {
        start_ = std::chrono::steady_clock::now();
        std::cout << std::left << std::setw(46)
                  << ("Test " + info.name + " >> ") << std::flush;
    }

    void testCaseEnded(Catch::TestCaseStats const& stats) override {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start_).count();
        bool ok = stats.totals.testCases.failed == 0;
        std::cout << "Runtime: " << ms << " ms - Result correct: "
                  << (ok ? "true" : "false") << std::endl;
    }

private:
    std::chrono::steady_clock::time_point start_;
};

CATCH_REGISTER_LISTENER(SimpleReporter);

//safely collect all values for a given key
static std::vector<Val> values_for(const Map& m, Key k) {
  std::vector<Val> out;
  bool ok = m.find_each(k, [&](const std::vector<Val>& vec){
    out.insert(out.end(), vec.begin(), vec.end());
  });
  if (!ok) out.clear();
  return out;
}

//TEST CASES 

TEST_CASE("Cuckoo: EmptyFind", "[cuckoo][basic]") { //Empty map lookup should return nothing
  Map m(0);
  auto vals = values_for(m, 123);
  REQUIRE(vals.empty());
}

TEST_CASE("Cuckoo: Insert & find unique with small cap (kicks expected)", "[cuckoo][basic]") { //Insert many unique keys in small table -> check for kicks and correctness
  Map m(8); 
  const int N = 200;
  for (int i = 0; i < N; ++i)
    m.emplace(i, std::vector<Val>{static_cast<Val>(i + 1000)});

  for (int i = 0; i < N; ++i) {
    auto got = values_for(m, i);
    REQUIRE(got.size() == 1u);
    REQUIRE(got[0] == static_cast<Val>(i + 1000));
  }
}

TEST_CASE("Cuckoo: Duplicates on same key keep all values (vector merge)", "[cuckoo][duplicates]") { //Multiple inserts of the same key must append (multimap semantics)
  Map m(0);
  const Key k = 42;
  const int N = 500;
  for (int i = 0; i < N; ++i)
    m.emplace(k, std::vector<Val>{static_cast<Val>(i)});

  auto got = values_for(m, k);
  REQUIRE(static_cast<int>(got.size()) == N);
  std::sort(got.begin(), got.end());
  for (int i = 0; i < N; ++i) REQUIRE(got[i] == static_cast<Val>(i));
}

TEST_CASE("Cuckoo: Forced cycle -> rehash/growth but data intact", "[cuckoo][rehash]") { //Force collisions and cycles -> must trigger rehash/growth without data loss
  Map m(4); 
  const int N = 2000;
  const uint64_t stride = 1299709ull; 

  std::vector<Key> keys; keys.reserve(N);
  for (int i = 0; i < N; ++i) {
    Key k = static_cast<Key>(i) * stride;
    keys.push_back(k);
    m.emplace(k, std::vector<Val>{static_cast<Val>(i)});
  }

  for (int i = 0; i < N; ++i) {
    auto got = values_for(m, keys[i]);
    REQUIRE(got.size() == 1u);
    REQUIRE(got[0] == static_cast<Val>(i));
  }
}

TEST_CASE("Cuckoo: find() is non-destructive", "[cuckoo][safety]") { //Repeated find() operations should never modify the map
  Map m(8);
  for (int i = 0; i < 300; ++i)
    m.emplace(i * 8, std::vector<Val>{static_cast<Val>(i)});

  std::unordered_map<Key, size_t> before;
  for (int i = 0; i < 300; ++i)
    before[i * 8] = values_for(m, i * 8).size();

  for (int rep = 0; rep < 1000; ++rep)
    for (int i = 0; i < 300; ++i)
      (void)values_for(m, i * 8);

  for (int i = 0; i < 300; ++i)
    REQUIRE(values_for(m, i * 8).size() == before[i * 8]);
}


TEST_CASE("Cuckoo: After rehash, further inserts still correct", "[cuckoo][rehash]") { //After table growth/rehash, further inserts must remain correct
  Map m(4);
  for (int i = 0; i < 4000; ++i)
    m.emplace(i, std::vector<Val>{static_cast<Val>(i)});
  for (int i = 4000; i < 5000; ++i)
    m.emplace(i, std::vector<Val>{static_cast<Val>(i)});

  for (int i : {0, 7, 64, 511, 1024, 4095, 4999}) {
    auto got = values_for(m, static_cast<Key>(i));
    REQUIRE(got.size() == 1u);
    REQUIRE(got[0] == static_cast<Val>(i));
  }
}