#pragma once
#include <vector>
#include <optional>
#include <bitset>
#include <functional>
#include <utility>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <iostream>

using namespace std;

inline bool is_prime(size_t n) {        //function to see if a num is prime 
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (size_t i = 3; i * i <= n; i += 2)
        if (n % i == 0) return false;
    return true;
}

inline size_t next_prime(size_t n) {       //function to find the next prime number
    while (!is_prime(n)) ++n;
    return n;
}

template <typename Key, typename Value>
class HopscotchHashMap {
private:
    static constexpr size_t MAX_BITS = 64; 
    struct Bucket {     //every tables bucket contains 
        optional<pair<Key, Value>> kv;  //pair of key + value
        bitset<MAX_BITS> bitmap;      //The bitmap
    };

    vector<Bucket> table;   //a table of buckets
    size_t table_size;      //the size of the table
    size_t element_count;   //the total elements I have in the table
    static constexpr size_t neighborhood_size = 32;   //the size of the neighborhood

    size_t hash(const Key& key) const {         //the hash function
        return std::hash<Key>{}(key) % table_size;
    }

    void bitmap_set(size_t base, size_t offset) {     //sets the bit of the position that has been captured by the same key
        if (offset < neighborhood_size && offset < MAX_BITS) {
            table[base].bitmap.set(offset);
        }
    }

    void bitmap_reset(size_t base, size_t offset) {   //unsets the bit 
        if (offset < neighborhood_size && offset < MAX_BITS) {
            table[base].bitmap.reset(offset);
        }
    }

    bool bitmap_test(size_t base, size_t offset) const {      //sees if the position is inside the neighborhood so we can add the num there
        if (offset < neighborhood_size && offset < MAX_BITS) {
            return table[base].bitmap.test(offset);
        } else {
            return false;
        }
    }

    void rehash() {     //rehash function 
        size_t new_size = next_prime(max<size_t>(2, table_size) * 2);   //doubles the table 
        vector<pair<Key, Value>> old_pairs; 

        for (auto& b : table)   //takes every k&v pair and stores it temporarly into an array  
            if (b.kv)
                old_pairs.push_back(*b.kv);

        table.clear();  //clears the table 
        table.resize(new_size);     //sets the new size
        table_size = new_size;
        element_count = 0;

        for (auto& [k, v] : old_pairs)     // add all the elements back to the new table 
            emplace(k, v);
    }

public:
    HopscotchHashMap(size_t expected_elements = 64)     //takes an expected amount of elements
        : table_size(0), element_count(0) {   
        if (expected_elements < 1) expected_elements = 1;   //check so the table will be at least 1 size
        table_size = next_prime(expected_elements * 2);     //finds the next prime of the double of elements that going to be inserted
        table.resize(table_size);   //sets the table size
    }

    struct iterator {
        using bucket_iter = typename vector<Bucket>::iterator;
        bucket_iter it; //iterator to cuurent position
        bucket_iter end_it; //iteraton to last position
        iterator(bucket_iter i, bucket_iter e) : it(i), end_it(e) {}
        auto& operator*() { return *it->kv; }   //returns the kv
        auto* operator->() { return &(*it->kv); }   //returns a pointer to the kv
        bool operator==(const iterator& other) const { return it == other.it; } //sees if 2 iterators are equal
        bool operator!=(const iterator& other) const { return it != other.it; } // ^^-
    };

    iterator end() { return iterator(table.end(), table.end()); }       //returns an iterator to end of table

    iterator find(const Key& key) {
        if (table_size == 0) return end();  
        size_t base = hash(key);    //takes the hash position of the key

        for (size_t i = 0; i < neighborhood_size; ++i) {    //seeks into the neighborhood for the key
            if (bitmap_test(base, i)) {   //see if there is any element in that position
                size_t idx = (base + i) % table_size;   //goes to next bucket, with wrap around 
                if (table[idx].kv && table[idx].kv->first == key) {     //when we find the key
                    return iterator(table.begin() + idx, table.end());  //returns an iterator to keys position
                }
            }
        }
        return end();
    }

    pair<iterator, bool> emplace(const Key& key, const Value& value) {  //function to insert a kv pair
        if (table_size == 0) {
            table_size = next_prime(2);
            table.resize(table_size);
        }

        size_t base = hash(key);    //takes the positiion that is going to be emplaced

        auto it = find(key);
        if (it != end()) {
            it->second = value;     // overwrite always
            return {it, false};
        }

        if (table[base].bitmap.all()) {     //checks the bitmap
            rehash();
            return emplace(key, value);
        }

        size_t free_idx = base; 
        size_t scanned = 0; //how many buckets we checked
        while (table[free_idx].kv.has_value()) {    //while the bucket is not empty we go to next one
            free_idx = (free_idx + 1) % table_size; //checks every position for free bucket
            ++scanned;
            if (scanned >= table_size) {    //if the table is full we rehash
                rehash();
                return emplace(key, value); //and we try to insert the pair again
            }
        }

        auto distance_base = [&](size_t idx)->size_t {  //finds the distance from the position of the key
            return (idx + table_size - base) % table_size;
        };

        while (distance_base(free_idx) >= neighborhood_size) {  //if we have a distance bigger than the nh size we must do some moves
            bool moved = false;

            for (size_t i = 0; i < neighborhood_size - 1; ++i) {    //checks the neighbohood for moves
                size_t idx = (free_idx + table_size - (neighborhood_size - 1) + i) % table_size;    //checks every bucket that is in the neighborhood
                if (!table[idx].kv) continue;   

                size_t origin = hash(table[idx].kv->first); //takes the start position of the kv that is in the bucket
                size_t dist = (idx + table_size - origin) % table_size;     //sees the distance from the origin

                if (dist < neighborhood_size) {      
                    size_t new_dist = (free_idx + table_size - origin) % table_size;    //checks the distance of the origin of element we check

                    if (new_dist < neighborhood_size) {     //if the free position is in it's neighborhood
                        table[free_idx].kv = std::move(table[idx].kv);   //we move the elemnt there
                        bitmap_reset(origin, dist);   //reset the bitmap of the origin
                        bitmap_set(origin, new_dist); //set the bitmap
                        table[idx].kv.reset();  //free the bucket
                        free_idx = idx; //change the free position
                        moved = true;   
                        break;
                    } 
                }
            }

            if (!moved) {       //if we couldn't move we rehash and try to insert the kv again
                rehash();
                return emplace(key, value);
            }
        }

        table[free_idx].kv = make_pair(key, value); //insert the new pair kv 
        size_t offset = (free_idx + table_size - base) % table_size;    //takes the offset
        if (offset < neighborhood_size && offset < MAX_BITS) {  //sets the bitmap
            bitmap_set(base, offset);
        } else {
            rehash();
            return emplace(key, value);
        }

        ++element_count;    //increase the element count 
        return {iterator(table.begin() + free_idx, table.end()), true}; //returns an iterator to new kv
    }
};
