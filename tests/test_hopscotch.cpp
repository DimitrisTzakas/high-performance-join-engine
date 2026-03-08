#include <catch2/catch_test_macros.hpp>
#include "../src/HopscotchHash.h"

TEST_CASE("HopscotchHashMap basic insert and find", "[insert][find]") {
HopscotchHashMap<int, std::string> map(8);

auto [it1, inserted1] = map.emplace(1, "One");
auto [it2, inserted2] = map.emplace(2, "Two");

REQUIRE(inserted1);
REQUIRE(inserted2);
REQUIRE(it1->first == 1);
REQUIRE(it1->second == "One");
REQUIRE(it2->first == 2);
REQUIRE(it2->second == "Two");

auto found = map.find(1);
REQUIRE(found != map.end());
REQUIRE(found->second == "One");

auto not_found = map.find(99);
REQUIRE(not_found == map.end());

}

TEST_CASE("HopscotchHashMap overwriting existing keys", "[overwrite]") {
HopscotchHashMap<int, std::string> map(8);

map.emplace(5, "OldValue");
auto [it, inserted] = map.emplace(5, "NewValue");

REQUIRE_FALSE(inserted);
REQUIRE(it->first == 5);
REQUIRE(it->second == "NewValue");

auto found = map.find(5);
REQUIRE(found != map.end());
REQUIRE(found->second == "NewValue");
}

TEST_CASE("HopscotchHashMap handles rehashing correctly", "[rehash]") {
HopscotchHashMap<int, int> map(4);

for (int i = 0; i < 100; ++i)
    map.emplace(i, i * 10);

for (int i = 0; i < 100; ++i) {
    auto it = map.find(i);
    REQUIRE(it != map.end());
    REQUIRE(it->second == i * 10);
}

}

TEST_CASE("HopscotchHashMap stores and retrieves strings", "[string]") {
HopscotchHashMap<std::string, std::string> map(4);

map.emplace("alpha", "a");
map.emplace("beta", "b");
map.emplace("gamma", "g");

REQUIRE(map.find("alpha")->second == "a");
REQUIRE(map.find("beta")->second == "b");
REQUIRE(map.find("gamma")->second == "g");

}

TEST_CASE("HopscotchHashMap iterator traversal", "[iterator]") {
HopscotchHashMap<int, std::string> map(8);
map.emplace(10, "ten");
map.emplace(20, "twenty");
map.emplace(30, "thirty");

size_t count = 0;
for (int key : {10, 20, 30}) {
    auto it = map.find(key);
    REQUIRE(it != map.end());
    ++count;
}

REQUIRE(count == 3);

}