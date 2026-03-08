# K23a: Software Development - High-Performance Join Engine 

This is a project I built for my "Software Development for Information Systems" course (Winter Semester 2025-2026). It is heavily based on the SIGMOD 2025 programming contest, and my main goal was to heavily optimize a relational join pipeline to drop the execution time as much as humanly possible.

## What I Built (The 3 Milestones) 

### 1. Custom Hash Tables
First things first, I had to get rid of the slow `std::unordered_map`. I implemented 3 custom hash tables from scratch: **Hopscotch**, **Cuckoo**, and **Robin Hood**. Robin Hood combined with the DJB2 hash function gave the best initial results, shaving off about 10 seconds from the execution time!

### 2. From Row-Store to Column-Store & Late Materialization
This is where the real performance gains started to show:
* **Value_t:** I created a custom 64-bit struct to completely avoid unnecessary string copying during intermediate joins. Joins are now purely integer-based, and strings are materialized only at the very end of the execution.
* **Column-Store:** I changed the data storage layout from rows to columns. I used a Paged Column structure with a 4096-byte page size (found the sweet spot after running several experiments) to make the program highly cache-friendly and minimize cache misses.
* **Unchained Hashtable:** I implemented the advanced hashtable described in the assignment's paper. It uses Bloom Filters to quickly check if a key exists, and duplicate keys are stored in the exact same slot.

### 3. Parallelism with OpenMP & My Custom Tweak
To completely max out the performance, I threw OpenMP into the mix.
* I parallelized the hashtable build phase (using bump allocators and prefix sums) as well as the probing phase. I also utilized work stealing so that no thread ever sits idle if it finishes its chunk early.
* **My Custom Tweak:** I figured it would be a great idea to also parallelize the final materialization phase (converting the `value_t` structs back to actual strings). This final step was quite expensive time-wise, and parallelizing it gave a massive extra performance boost!

---

## Results & Timings 

All experiments were run on an Ubuntu 22.04 machine with an Intel Core i7 (10 cores, 16 threads) and 32GB RAM. 
I started at **190,469 ms** using the basic `std::unordered_map` implementation. Here is how the time dropped with each optimization:

* Robin Hood: 177,144 ms
* Value_t (Late Materialization): 112,454 ms
* Column Store: 58,782 ms
* Unchained Hashtable: 47,658 ms
* **Parallelism + Parallel Materialization (Final): 9,475 ms!**

In total, I managed to achieve a **94.5% speedup**! 

---

## How to Run 

I use CMake for the build process.

**To build and run the main program:**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
cmake --build build --target fast_unc -- -j 6
./build/fast_unc plans.json

To run the Unit Tests (Catch2):
I've written several unit tests to make sure that my custom data structures (hash tables, column store) work flawlessly.

Bash
cmake --build build --target test_unchained test_column_store
./build/test_column_store
./build/test_unchained