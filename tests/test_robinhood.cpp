#include <catch2/catch_test_macros.hpp>
#include "../src/RobinHoodHash.h"
#include <string>
#include <vector>

TEST_CASE("RobinHoodHashMap basic insert/find operations", "[insert][find]") {
    RobinHoodHashMap<int, std::string> map(16);

    REQUIRE(map.find(10) == map.end()); // not found yet

    map.emplace(10, "Alice");
    map.emplace(20, "Bob");
    map.emplace(30, "Charlie");

    auto it = map.find(20);
    REQUIRE(it != map.end());
    REQUIRE(it->first == 20);
    REQUIRE(it->second == "Bob");
}

TEST_CASE("RobinHoodHashMap overwriting existing keys", "[insert][update]") {
    RobinHoodHashMap<int, std::string> map(8);

    map.emplace(5, "One");
    map.emplace(5, "Two"); // same key, should overwrite or reinsert properly

    auto it = map.find(5);
    REQUIRE(it != map.end());
    REQUIRE(it->second == "Two");
}

TEST_CASE("RobinHoodHashMap handles collisions and distance", "[collision][distance]") {
    RobinHoodHashMap<size_t, std::string> map(8);

    // Force collisions (keys with same hash modulo small table)
    size_t base = 0;
    for (int i = 0; i < 10; ++i) {
        map.emplace(base + i * 8, "val" + std::to_string(i));
    }

    // Verify all keys are still retrievable
    for (int i = 0; i < 10; ++i) {
        auto it = map.find(base + i * 8);
        REQUIRE(it != map.end());
        REQUIRE(it->second == "val" + std::to_string(i));
    }
}

TEST_CASE("RobinHoodHashMap triggers rehash when load factor exceeds 0.6", "[rehash]") {
    RobinHoodHashMap<int, int> map(4);

    for (int i = 0; i < 10; ++i) {
        map.emplace(i, i * 10);
    }

    // After many insertions, the map should still work
    for (int i = 0; i < 10; ++i) {
        auto it = map.find(i);
        REQUIRE(it != map.end());
        REQUIRE(it->second == i * 10);
    }
}

TEST_CASE("RobinHoodHashMap works with complex value types", "[vector][value]") {
    RobinHoodHashMap<std::string, std::vector<int>> map(8);

    map.emplace("a", std::vector<int>{1, 2, 3});
    map.emplace("b", std::vector<int>{4, 5});

    auto it = map.find("a");
    REQUIRE(it != map.end());
    REQUIRE(it->second.size() == 3);
    REQUIRE(it->second[0] == 1);
    REQUIRE(it->second[2] == 3);
}

TEST_CASE("RobinHoodHashMap swap trigger, insertion and swap unit test", "[robinhood][djb2]") {
    RobinHoodHashMap<int, int> map(8);

    int k1 = 9;    // bucket = (9 - 9) % 17 = 0
    int k2 = 8;    // bucket = (9 - 8) % 17 = 1
    int k3 = 26;   // 26 % 256 = 26; 26 % 17 = 9; bucket = (9 - 9) % 17 = 0

    map.emplace(k1, 100);
    map.emplace(k2, 200);
    map.emplace(k3, 300);  // triggers Robin Hood swapping

    auto it1 = map.find(k1);
    auto it2 = map.find(k2);
    auto it3 = map.find(k3);

    REQUIRE(it1 != map.end());
    REQUIRE(it2 != map.end());
    REQUIRE(it3 != map.end());

    size_t pos1 = it1.it - map.begin().it;
    size_t pos2 = it2.it - map.begin().it;
    size_t pos3 = it3.it - map.begin().it;

    // Expected arrangement:
    // bucket 0: k1
    // bucket 1: k3 (displaced k2 via Robin Hood)
    // bucket 2: k2
    REQUIRE(pos1 == 0);
    REQUIRE(pos3 == 1);
    REQUIRE(pos2 == 2);
}
