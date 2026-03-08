#pragma once
#include <vector>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <nmmintrin.h>  //for crc32 hash function
#include <omp.h>
#include <chrono>
#include <iostream>

using namespace std;

struct Chunk {      //chunk of data
    uint8_t* data;
    uint32_t offset;
    uint32_t capacity;
    Chunk* next;
};

struct PartitionAlloc {     //the global allocator
    Chunk* current_chunk = nullptr;
    uint64_t count = 0;

    void addSpace(Chunk* c) {
        c->next = current_chunk;
        current_chunk = c;
        current_chunk->offset = 0;
    }

    size_t freeSpace() const {
        return current_chunk ? (current_chunk->capacity - current_chunk->offset) : 0;
    }

    template<typename T>
    T* allocate() {
        T* ptr = reinterpret_cast<T*>(current_chunk->data + current_chunk->offset);
        current_chunk->offset += sizeof(T);
        count++;
        return ptr;
    }
};

template<typename K, typename V>
class UnchainedHashTable {
public:
    struct Tuple {
        K key;
        V value;
    };

private:
    vector<uint64_t> directory;  
    vector<Tuple> adjacency;

    vector<uint64_t> bloom_bits;   //bloom filter map
    size_t bloom_bit_count = 0;     //total bloom bits

    vector<Tuple> raw_tuples;

    uint64_t hash_key(const K& key) const {
        if constexpr (sizeof(K) <= 4) {
            return static_cast<uint64_t>(_mm_crc32_u32(0, static_cast<uint32_t>(key))); //crc32 hash function
        } else {
            uint32_t lo = static_cast<uint32_t>(key & 0xFFFFFFFF);
            uint32_t hi = static_cast<uint32_t>((key >> 32) & 0xFFFFFFFF);
            uint64_t h = _mm_crc32_u32(0, lo);
            h = _mm_crc32_u32(static_cast<uint32_t>(h), hi);
            return h;
        }
    }

    static uint64_t bloom_hash1(uint64_t x) {   //Bit mixing hash function 1
        x ^= x >> 16;
        x ^= x << 3;
        return x;
    }

    static uint64_t bloom_hash2(uint64_t x) {   //Bit mixing hash function 2
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    }

    static uint16_t mini_hash(uint64_t hash){ 
        return static_cast<uint16_t>(hash & 0xFFFFu); //takes the last 16 bits 
    }

    static uint16_t fingerprint_to_mask(uint16_t fp) {
        uint16_t mask = 0;
        mask |= (1u << ((fp      ) & 0xF));
        mask |= (1u << ((fp >>  4) & 0xF));
        mask |= (1u << ((fp >>  8) & 0xF));
        mask |= (1u << ((fp >> 12) & 0xF));
        return mask;
    }
    inline void bloom_set_bit(uint64_t bitpos) {
        if (bloom_bit_count == 0) return;
        bitpos %= bloom_bit_count;
        bloom_bits[bitpos >> 6] |= (1ULL << (bitpos & 63));
    }

    inline void bloom_set_bit_atomic(uint64_t bitpos) { //bloom filters with atomic instuction for parallel build
    if (bloom_bit_count == 0) return;
    bitpos %= bloom_bit_count;
    __atomic_fetch_or(&bloom_bits[bitpos >> 6], (1ULL << (bitpos & 63)), __ATOMIC_RELAXED);
}

    inline bool bloom_may_contain(uint64_t key) const {
        if (bloom_bit_count == 0)   
            return true;    //empty bloom filter → always pretend "maybe present"
        uint64_t h1 = bloom_hash1(key) % bloom_bit_count;       //first bloom hash → bit position
        uint64_t h2 = bloom_hash2(key) % bloom_bit_count;        //second bloom hash → bit position
        return (bloom_bits[h1 >> 6] & (1ULL << (h1 & 63))) && (bloom_bits[h2 >> 6] & (1ULL << (h2 & 63)));  //check that both bloom-filter bits for this key are set
    }

    static size_t next_power_of_two(size_t n) {
        if (n == 0) return 1;
        n--;
        n |= n >> 1; n |= n >> 2; n |= n >> 4;
        n |= n >> 8; n |= n >> 16;
        if constexpr(sizeof(size_t) == 8) n |= n >> 32;
        return n + 1;
    }

public:
    explicit UnchainedHashTable(size_t expected_tuples = 0) {
        size_t dir_size = next_power_of_two(expected_tuples > 0 ? expected_tuples*2  : 4);
        directory.resize(dir_size, 0);
        bloom_bit_count = next_power_of_two(expected_tuples > 0 ? expected_tuples * 4 : 64);
        bloom_bits.resize((bloom_bit_count + 63) / 64, 0);
    }

   void build_from_slabs(PartitionAlloc* partitions, size_t num_partitions) {
        const size_t dir_n = directory.size();  //dir size as hashtable size
        
        fill(directory.begin(), directory.end(), 0);    
        fill(bloom_bits.begin(), bloom_bits.end(), 0);
        size_t total_tuples_sum = 0;


            #pragma omp parallel reduction(+:total_tuples_sum)
            {
                #pragma omp for
                for (size_t p = 0; p < num_partitions; ++p) {
                    size_t p_count = 0;
                    Chunk* curr = partitions[p].current_chunk;
                    while (curr) {  //counts the num of tuples that we will save in that partition
                        size_t n = curr->offset / sizeof(Tuple);
                        Tuple* ts = reinterpret_cast<Tuple*>(curr->data);
                        for (size_t i = 0; i < n; ++i) {    //traverse all the tuples
                            uint64_t h = hash_key(ts[i].key);   //takes the hash value
                            size_t slot = static_cast<size_t>(h & (dir_n - 1));
                            
                            #pragma omp atomic  //adds to directory with atomic
                            directory[slot] += (1ULL << 16);
                            
                            uint16_t mask = fingerprint_to_mask(mini_hash(h));  //take the fingerprint
                            #pragma omp atomic
                            directory[slot] |= static_cast<uint64_t>(mask);
                            
                            bloom_set_bit_atomic(bloom_hash1(h));   //bloom filter update with atomic
                            bloom_set_bit_atomic(bloom_hash2(h));
                        }
                        p_count += n;
                        curr = curr->next;
                    }
                    total_tuples_sum += p_count;
                }
            }

        adjacency.resize(total_tuples_sum);     //makes the adjency array at the size of total tuples

        size_t current_offset = 0;
        for (size_t i = 0; i < dir_n; ++i) {    //prefix sum
            uint64_t count = directory[i] >> 16;    //tuples per slot
            uint16_t tag = static_cast<uint16_t>(directory[i] & 0xFFFFu);   //fingerprint
            directory[i] = (static_cast<uint64_t>(current_offset) << 16) | tag;
            current_offset += count;    //goes to next slot
        }

        #pragma omp parallel for
        for (size_t p = 0; p < num_partitions; ++p) {   //parallel placement   
            Chunk* curr = partitions[p].current_chunk;
            while (curr) {  //traverse all the chunks
                size_t n = curr->offset / sizeof(Tuple);    //total tuples
                Tuple* ts = reinterpret_cast<Tuple*>(curr->data);
                for (size_t i = 0; i < n; ++i) {    //traverse all the tuples
                    uint64_t h = hash_key(ts[i].key);   //hashkey
                    size_t slot = static_cast<size_t>(h & (dir_n - 1));     //the directory position
                    
                    uint64_t old_val = __atomic_fetch_add(&directory[slot], (1ULL << 16), __ATOMIC_RELAXED);    //increase the slot count    
                    size_t target_pos = static_cast<size_t>(old_val >> 16); //the position we will write the next tuple
                    
                    adjacency[target_pos] = ts[i];
                }
                curr = curr->next;
            }
        }

        if (dir_n > 0) {    //sliding for the correct offset 
            for (size_t i = dir_n - 1; i > 0; --i) {
                uint16_t current_tag = static_cast<uint16_t>(directory[i] & 0xFFFFu);
                uint64_t prev_end_offset = directory[i-1] >> 16;
                directory[i] = (prev_end_offset << 16) | current_tag;
            }
            directory[0] = (0ULL << 16) | (static_cast<uint16_t>(directory[0] & 0xFFFFu));
        }

    }

    vector<V*> find(const K& key) {
        vector<V*> out;

        if (directory.empty()) return out;  

        uint64_t h = hash_key(key);

        size_t slot = static_cast<size_t>(h & (directory.size() - 1));
        uint64_t entry = directory[slot];

        if (!bloom_may_contain(h)) return out;  //bloom filters use

        uint16_t probe_mask = fingerprint_to_mask(mini_hash(h));
        uint16_t slot_filter = static_cast<uint16_t>(entry & 0xFFFFu);
        if ((slot_filter & probe_mask) != probe_mask) return out;

        size_t dir_n = directory.size();
        uint64_t start = directory[slot] >> 16;
        uint64_t end = (slot + 1 == dir_n) ? adjacency.size() : (directory[slot + 1] >> 16);
        out.reserve(end - start);

        Tuple* ptr = adjacency.data() + start;
        for (uint64_t i = 0; i < end - start; ++i) {    //takes the values 
            if (ptr[i].key == key) {
                out.push_back(&ptr[i].value);
            }
        }

        return out;
    }

};