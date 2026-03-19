# High-Performance Join Engine

This repository contains the final version of my course project for **Software Development for Information Systems** (Winter Semester 2025-2026), based on the **SIGMOD 2025 programming contest**.

The goal of the project was to speed up the execution of a relational join pipeline as much as possible. Starting from a baseline implementation that used `std::unordered_map`, I gradually redesigned the execution path by focusing on data layout, hash table design, and parallel execution. The final version reduced execution time from **190,469 ms** to **9,475 ms**.

## What I implemented

The project evolved through a few main optimization steps:

### 1. Custom hash tables
I first replaced `std::unordered_map` with custom hash table implementations written from scratch:
- Hopscotch hashing
- Cuckoo hashing
- Robin Hood hashing

Among these, **Robin Hood hashing** combined with the **DJB2** hash function gave the best early improvement.

### 2. Late materialization
One of the biggest bottlenecks was handling strings during intermediate joins. To avoid repeated string copying, I introduced a custom **64-bit `value_t` representation**. Intermediate join results are stored in this compact form, and actual strings are materialized only at the end of execution.

This significantly reduced overhead and made later stages much faster.

### 3. Column-store layout
I changed the internal representation from a row-oriented layout to a **column-oriented layout**. I used a paged column structure to improve cache locality and reduce unnecessary memory traffic during joins.

After experimenting with different page sizes, I found that **4096-byte pages** worked best for this implementation.

### 4. Unchained hash table
I also implemented the **Unchained Hash Table** described in the assignment paper. It uses a Bloom filter for fast key existence checks and stores duplicate keys in the same slot, which helped improve join performance further.

### 5. Parallel execution with OpenMP
In the final stage, I parallelized the most expensive parts of the pipeline using **OpenMP**:
- hash table build phase
- probe phase
- final materialization phase

I also used work stealing / dynamic scheduling ideas so that threads could keep working instead of sitting idle.

## Results

All experiments were run on **Ubuntu 22.04** on a machine with:
- **Intel Core i7** (10 cores / 16 threads)
- **32 GB RAM**

Execution time improved as follows:

| Version | Execution Time |
|---|---:|
| Baseline (`std::unordered_map`) | 190,469 ms |
| Robin Hood hashing | 177,144 ms |
| Late materialization (`value_t`) | 112,454 ms |
| Column store | 58,782 ms |
| Unchained hash table | 47,658 ms |
| Final parallel version | **9,475 ms** |

Overall, this corresponds to a **94.5% reduction in execution time**.

## Correctness and testing

To make sure the optimizations did not break correctness, I used the provided tooling and also wrote unit tests for the custom data structures.

The repository includes tests for:
- the unchained hash table
- the column-store implementation

For correctness checking, the project also uses the provided DuckDB-based validation flow from the assignment.

## What I learned

This project taught me a lot about performance-oriented systems programming in C++.

A few of the main takeaways for me were:
- data layout matters just as much as algorithm choice
- avoiding unnecessary string work can completely change performance
- cache behavior has a huge effect on query execution
- parallelism helps much more when the underlying data structures are already efficient
- performance improvements are only meaningful if correctness is preserved

## Build and run

Run all commands from the root of the repository.

### Build the project
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
cmake --build build -- -j $(nproc)

Build the cache
bash

./build/build_cache plans.json

Run the benchmarked version
bash

./build/fast plans.json

Run correctness checks
bash

./build/run plans.json

Run unit tests
bash

cmake --build build --target test_unchained test_column_store
./build/test_column_store
./build/test_unchained

Report

The full course report is included in this repository as Report.pdf.
It contains the more detailed explanation of the design choices, experiments, and implementation process behind the final result.
Assignment context

This project was originally developed as a university assignment inspired by the SIGMOD 2025 programming contest. The original assignment description is included in ASSIGNMENT.md.
