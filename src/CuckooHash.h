#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace cuckoo {

template <class K, class V = std::size_t>
class CuckooMultiMap {
public:
    using key_type    = K;            //key type
    using mapped_type = V;            //mapped type
    using value_type  = std::pair<const K, V>; //stl-like pair alias
    using size_type   = std::size_t;

private:
    //a single slot in a table
    struct Bucket {
        K    key{};        //stored key
        V    val{};        //stored value for that key
        bool occupied = false; //is this slot in use?
    };

    //two tables = classic cuckoo hashing
    std::vector<Bucket> table1_;
    std::vector<Bucket> table2_;
    size_type capacity_ = 0; //number of slots per table (power of two)
    size_type mask_     = 0; //for fast modulo
    size_type size_     = 0; //number of distinct keys present

    //tiny proxy 
    struct PairProxy {
        const K* first_ptr = nullptr;   //points to bucket.key
        V*       second_ptr = nullptr;  //points to bucket.val

        //sub-proxy exposing push_back / begin / end ...
        struct Second {
            V* p = nullptr;
            //forward push_back to the underlying V (vector)
            template <class... Args>
            auto push_back(Args&&... args)
                -> decltype( std::declval<V&>().push_back(std::forward<Args>(args)...) ) {
                return p->push_back(std::forward<Args>(args)...);
            }
            auto begin() -> decltype(std::declval<V&>().begin()) { return p->begin(); }
            auto end()   -> decltype(std::declval<V&>().end())   { return p->end(); }
            auto begin() const -> decltype(std::declval<const V&>().begin()) { return p->begin(); }
            auto end()   const -> decltype(std::declval<const V&>().end())   { return p->end(); }
            // implicit reference if someone needs the whole V
            operator V&() const { return *p; }
        } second;

        PairProxy() = default;
        PairProxy(const K* k, V* v) : first_ptr(k), second_ptr(v), second{v} {}
    };

public:
    //simple forward iterator holding a pointer to a bucket
    class iterator {
        Bucket* bptr_ = nullptr;
        PairProxy proxy_{}; // materialize = second
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = CuckooMultiMap::value_type;
        using reference         = value_type;  //returned by value 
        using pointer           = PairProxy*;  

        iterator() = default;
        explicit iterator(Bucket* b) : bptr_(b) {
            if (bptr_) proxy_ = PairProxy(&bptr_->key, &bptr_->val);
        }

        reference operator*() const { return value_type(bptr_->key, bptr_->val); }
        pointer   operator->()      { return &proxy_; }

        friend bool operator==(const iterator& a, const iterator& b) { return a.bptr_ == b.bptr_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }
    };

    class const_iterator {
        const Bucket* bptr_ = nullptr;
        PairProxy proxy_{};
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = CuckooMultiMap::value_type;
        using reference         = value_type;
        using pointer           = const PairProxy*;

        const_iterator() = default;
        explicit const_iterator(const Bucket* b) : bptr_(b) {
            if (bptr_) proxy_ = PairProxy(&bptr_->key, const_cast<V*>(&bptr_->val));
        }

        reference operator*() const { return value_type(bptr_->key, bptr_->val); }
        pointer   operator->() const { return &proxy_; }

        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.bptr_ == b.bptr_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return !(a == b); }
    };

    //ctor / capacity
    explicit CuckooMultiMap(size_type expected = 0) {
        //start with a small power-of-two capacity, double of expected for load <= 0.5
        const size_type cap = next_pow2(expected > 0 ? expected * 2 : 8);
        init_capacity(cap);
    }

    void reserve(size_type expected) {
        //grow tables if you know roughly how many keys are coming
        size_type want = next_pow2(expected > 0 ? expected * 2 : 8);
        if (want <= capacity_) return;
        auto elems = collect_all();
        rebuild_internal(want, elems);
    }

    size_type size()     const { return size_; }      //number of keys stored
    size_type capacity() const { return capacity_; }  //slots per table

    //modifiers
    void emplace(const K& key, const V& value) {
        //if key exists, merge/append value and return
        if (merge_if_present(key, value)) return;

        ensure_capacity_for_insert(); //grow if needed to keep load <= 0.5

        if (merge_if_present(key, value)) return; //might have changed after grow

        //create a fresh bucket for insertion
        Bucket fresh;
        fresh.key = key;
        fresh.val = value;
        fresh.occupied = true;

        //try each home without kicking first
        size_type i1 = h1(key);
        if (!table1_[i1].occupied) { table1_[i1] = std::move(fresh); ++size_; return; }
        size_type i2 = h2(key);
        if (!table2_[i2].occupied) { table2_[i2] = std::move(fresh); ++size_; return; }

        //need cuckoo kicks
        Bucket cur = std::move(fresh);
        if (cuckoo_kick_insert(cur)) { ++size_; return; }
        //too confused: rebuild larger and include leftover
        rehash_with_extra(cur);
    }

    //lookup
    iterator       find(const K& key)       { return find_impl<iterator>(key); }
    const_iterator find(const K& key) const { return find_impl<const_iterator>(key); }

    iterator       end()       { return iterator(); }       // end() is a null iterator
    const_iterator end() const { return const_iterator(); } // const end()

    //optional: visit the value for a key with a callback (if present)
    template <class Fn>
    bool find_each(const K& key, Fn&& fn) const {
        auto it = find(key);
        if (it == end()) return false;
        fn(it->second);
        return true;
    }

private:
    //hashing & helpers
    static inline std::uint64_t mix64_u(std::uint64_t x) {
        // 64-bit mix (like splitmix) for better bit diffusion
        x ^= x >> 33u; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33u; x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33u; return x;
    }
    static size_type next_pow2(size_type x) {
        //round up to next power of two (min 8)
        if (x < 8) x = 8;
        --x; x |= x>>1; x |= x>>2; x |= x>>4; x |= x>>8; x |= x>>16;
        if (sizeof(size_type) >= 8) x |= x>>32;
        return ++x;
    }
    static size_type h1_static(const K& k, size_type mask) {
        //primary hash -> index
        std::uint64_t h = (std::uint64_t)std::hash<K>{}(k);
        return (size_type)(mix64_u(h)) & mask;
    }
    static size_type h2_static(const K& k, size_type mask) {
        //secondary hash -> different index (xor a constant)
        std::uint64_t h = ((std::uint64_t)std::hash<K>{}(k)) ^ 0x9e3779b97f4a7c15ULL;
        return (size_type)(mix64_u(h)) & mask;
    }
    size_type h1(const K& k) const { return h1_static(k, mask_); }
    size_type h2(const K& k) const { return h2_static(k, mask_); }

    void init_capacity(size_type cap) {
        //allocate both tables and reset counters
        capacity_ = next_pow2(cap);
        mask_     = capacity_ - 1;
        table1_.assign(capacity_, Bucket{});
        table2_.assign(capacity_, Bucket{});
        size_ = 0;
    }

    void ensure_capacity_for_insert() {
        //keep load <= 0.5 (distinct keys / capacity)
        if ((size_ + 1) * 2 <= capacity_) return;
        auto elems = collect_all();
        rebuild_internal(capacity_ * 2, elems);
    }

    std::vector<Bucket> collect_all() const {
        //gather all occupied buckets from both tables
        std::vector<Bucket> elems;
        elems.reserve(size_);
        for (const auto& b : table1_) if (b.occupied) elems.push_back(b);
        for (const auto& b : table2_) if (b.occupied) elems.push_back(b);
        return elems;
    }

    // merge policy:
    //  if V supports insert(range), append src into dst
    //  otherwise, just assign (overwrite)
    template <class T>
    static auto merge_into_impl(T& dst, const T& src, int)
        -> decltype(dst.insert(dst.end(), src.begin(), src.end()), void()) {
        dst.insert(dst.end(), src.begin(), src.end());
    }
    template <class T>
    static void merge_into_impl(T& dst, const T& src, ...) {
        dst = src;
    }
    static void merge_into(V& dst, const V& src) {
        merge_into_impl(dst, src, 0);
    }

    bool merge_if_present(const K& key, const V& value) {
        //check both homes for an existing key; if found, merge
        size_type i1 = h1(key);
        if (table1_[i1].occupied && table1_[i1].key == key) {
            merge_into(table1_[i1].val, value);
            return true;
        }
        size_type i2 = h2(key);
        if (table2_[i2].occupied && table2_[i2].key == key) {
            merge_into(table2_[i2].val, value);
            return true;
        }
        return false;
    }

    bool cuckoo_kick_insert(Bucket& cur) {
        //try to place cur by bumping existing items between homes
        constexpr size_type MAX_KICKS = 256; //cap cycles before rebuild
        bool in_first = true;
        size_type idx = h1(cur.key);

        for (size_type kick = 0; kick < MAX_KICKS; ++kick) {
            if (in_first) {
                if (!table1_[idx].occupied) { table1_[idx] = std::move(cur); table1_[idx].occupied = true; return true; }
                if (table1_[idx].key == cur.key) { merge_into(table1_[idx].val, cur.val); return true; }
                std::swap(cur, table1_[idx]); in_first = false; idx = h2(cur.key);

                if (!table2_[idx].occupied) { table2_[idx] = std::move(cur); table2_[idx].occupied = true; return true; }
                if (table2_[idx].key == cur.key) { merge_into(table2_[idx].val, cur.val); return true; }
            } else {
                if (!table2_[idx].occupied) { table2_[idx] = std::move(cur); table2_[idx].occupied = true; return true; }
                if (table2_[idx].key == cur.key) { merge_into(table2_[idx].val, cur.val); return true; }
                std::swap(cur, table2_[idx]); in_first = true; idx = h1(cur.key);

                if (!table1_[idx].occupied) { table1_[idx] = std::move(cur); table1_[idx].occupied = true; return true; }
                if (table1_[idx].key == cur.key) { merge_into(table1_[idx].val, cur.val); return true; }
            }
        }
        return false; //too many kicks, need rebuild
    }

    void rehash_with_extra(const Bucket& extra) {
        //collect everything, double capacity, try again, include leftover
        auto elems = collect_all();
        elems.push_back(extra);
        rebuild_internal(capacity_ * 2, elems);
    }

    void rebuild_internal(size_type start_cap, std::vector<Bucket>& elems) {
        //keep doubling until we can place everything without exceeding MAX_KICKS
        size_type cap = next_pow2(start_cap);
        if (cap < 8) cap = 8;

        for (;;) {
            std::vector<Bucket> new_t1(cap);
            std::vector<Bucket> new_t2(cap);
            const size_type new_mask = cap - 1;

            if (try_build_tables(elems, new_t1, new_t2, new_mask)) {
                table1_.swap(new_t1);
                table2_.swap(new_t2);
                capacity_ = cap;
                mask_     = new_mask;

                //recompute size (number of occupied buckets)
                size_ = 0;
                for (const auto& b : table1_) if (b.occupied) ++size_;
                for (const auto& b : table2_) if (b.occupied) ++size_;
                return;
            }
            cap *= 2; //still stuck? go bigger
        }
    }

    static bool try_build_tables(
        const std::vector<Bucket>& elems,
        std::vector<Bucket>& T1,
        std::vector<Bucket>& T2,
        size_type mask
    ) {
        //offline build used by rebuild: behaves like cuckoo insert but restarts clean
        constexpr size_type MAX_KICKS = 512;

        for (const Bucket& b0 : elems) {
            Bucket cur = b0;

            size_type i1 = h1_static(cur.key, mask);
            if (!T1[i1].occupied) { T1[i1] = std::move(cur); T1[i1].occupied = true; continue; }
            if (T1[i1].key == cur.key) { merge_into(T1[i1].val, cur.val); continue; }

            size_type i2 = h2_static(cur.key, mask);
            if (!T2[i2].occupied) { T2[i2] = std::move(cur); T2[i2].occupied = true; continue; }
            if (T2[i2].key == cur.key) { merge_into(T2[i2].val, cur.val); continue; }

            bool in_first = true;
            size_type idx = i1;

            size_type kick;
            for (kick = 0; kick < MAX_KICKS; ++kick) {
                if (in_first) {
                    if (!T1[idx].occupied) { T1[idx] = std::move(cur); T1[idx].occupied = true; break; }
                    if (T1[idx].key == cur.key) { merge_into(T1[idx].val, cur.val); break; }
                    std::swap(cur, T1[idx]); in_first = false; idx = h2_static(cur.key, mask);

                    if (!T2[idx].occupied) { T2[idx] = std::move(cur); T2[idx].occupied = true; break; }
                    if (T2[idx].key == cur.key) { merge_into(T2[idx].val, cur.val); break; }
                } else {
                    if (!T2[idx].occupied) { T2[idx] = std::move(cur); T2[idx].occupied = true; break; }
                    if (T2[idx].key == cur.key) { merge_into(T2[idx].val, cur.val); break; }
                    std::swap(cur, T2[idx]); in_first = true; idx = h1_static(cur.key, mask);

                    if (!T1[idx].occupied) { T1[idx] = std::move(cur); T1[idx].occupied = true; break; }
                    if (T1[idx].key == cur.key) { merge_into(T1[idx].val, cur.val); break; }
                }
            }
            if (kick == MAX_KICKS) return false; // give up and ask caller to grow
        }
        return true;
    }

    //shared find logic for iterator/const_iterator
    template <class It>
    It find_impl(const K& key) const {
        size_type i1 = h1(key);
        if (table1_[i1].occupied && table1_[i1].key == key)
            return It(const_cast<Bucket*>(&table1_[i1]));
        size_type i2 = h2(key);
        if (table2_[i2].occupied && table2_[i2].key == key)
            return It(const_cast<Bucket*>(&table2_[i2]));
        return It(); // end()
    }
};

} // namespace cuckoo
