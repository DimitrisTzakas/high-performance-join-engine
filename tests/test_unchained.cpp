#include <catch2/catch_test_macros.hpp>
#include "../src/UnchainedHash.h"
#include <vector>

TEST_CASE("UnchainedHashTable basic insert/find operations", "[insert][find]") {
    UnchainedHashTable<int, int> table(8);

    std::vector<UnchainedHashTable<int,int>::Tuple> local_tuples = {
        {1, 100},
        {2, 200},
        {3, 300}
    };

    PartitionAlloc partitions[1];   
    Chunk* c = new Chunk{new uint8_t[1024], 0, 1024, nullptr};
    partitions[0].addSpace(c);

    for (auto& t : local_tuples) {
        auto* slot = partitions[0].allocate<UnchainedHashTable<int,int>::Tuple>();
        *slot = t;
    }

    table.build_from_slabs(partitions, 1);

    auto v1 = table.find(1);
    auto v2 = table.find(2);
    auto v3 = table.find(3);
    auto v4 = table.find(4);

    REQUIRE(v1.size() == 1); REQUIRE(*v1[0] == 100);
    REQUIRE(v2.size() == 1); REQUIRE(*v2[0] == 200);
    REQUIRE(v3.size() == 1); REQUIRE(*v3[0] == 300);
    REQUIRE(v4.empty());
}

TEST_CASE("UnchainedHashTable handles duplicate keys", "[insert][duplicate]") {
    UnchainedHashTable<int, int> table(8);

    std::vector<UnchainedHashTable<int,int>::Tuple> local_tuples = {
        {1, 10}, {1, 20}, {2, 30}
    };

    PartitionAlloc partitions[1];
    Chunk* c = new Chunk{new uint8_t[1024], 0, 1024, nullptr};
    partitions[0].addSpace(c);

    for (auto& t : local_tuples)
        *partitions[0].allocate<UnchainedHashTable<int,int>::Tuple>() = t;

    table.build_from_slabs(partitions, 1);

    auto vec1 = table.find(1);
    auto vec2 = table.find(2);
    auto vec3 = table.find(3);

    REQUIRE(vec1.size() == 2);
    REQUIRE(vec2.size() == 1);
    REQUIRE(vec3.empty());
}

TEST_CASE("UnchainedHashTable bloom filter rejects missing keys", "[bloom]") {
    UnchainedHashTable<int, int> table(4);

    std::vector<UnchainedHashTable<int,int>::Tuple> local_tuples = { {4, 999} };

    PartitionAlloc partitions[1];
    Chunk* c = new Chunk{new uint8_t[1024], 0, 1024, nullptr};
    partitions[0].addSpace(c);

    for (auto& t : local_tuples)
        *partitions[0].allocate<UnchainedHashTable<int,int>::Tuple>() = t;

    table.build_from_slabs(partitions, 1);

    auto missing = table.find(99);
    REQUIRE(missing.empty());

    auto present = table.find(4);
    REQUIRE(present.size() == 1);
    REQUIRE(*present[0] == 999);
}

TEST_CASE("UnchainedHashTable empty table behavior", "[empty]") {
    UnchainedHashTable<int, int> table(0);
    PartitionAlloc partitions[1];
    table.build_from_slabs(partitions, 0);  //no partitions

    REQUIRE(table.find(0).empty());
    REQUIRE(table.find(12345).empty());
}

TEST_CASE("UnchainedHashTable large number of elements", "[stress][large]") {
    size_t N = 100000;
    UnchainedHashTable<int,int> table(N);

    std::vector<UnchainedHashTable<int,int>::Tuple> local_tuples;
    local_tuples.reserve(N);
    for (int i = 0; i < N; ++i) local_tuples.push_back({i, i*10});

    PartitionAlloc partitions[1];
    Chunk* c = new Chunk{new uint8_t[N*sizeof(UnchainedHashTable<int,int>::Tuple)], 0,
                         static_cast<uint32_t>(N*sizeof(UnchainedHashTable<int,int>::Tuple)), nullptr};
    partitions[0].addSpace(c);

    for (auto& t : local_tuples)
        *partitions[0].allocate<UnchainedHashTable<int,int>::Tuple>() = t;

    table.build_from_slabs(partitions, 1);

    for (int i = 0; i < N; ++i) {
        auto val = table.find(i);
        REQUIRE(val.size() == 1);
        REQUIRE(*val[0] == i*10);
    }

    auto missing = table.find(N+1);
    REQUIRE(missing.empty());
}
