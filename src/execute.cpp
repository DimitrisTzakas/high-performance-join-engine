#include <hardware.h>
#include <plan.h>
#include <table.h>
#include "macro.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <variant>
#include <cmath>
#include <memory>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <array>
#include <nmmintrin.h> 

using namespace std;

constexpr size_t SMALL_CHUNK_SIZE = 16 * 1024; // 16KB

struct StringIndex {
    uint64_t table_id : 6;
    uint64_t col_id   : 6;
    uint64_t page_id  : 16; 
    uint64_t offset   : 20; 
    uint64_t length   : 16; 
};

struct value_t {
    enum Type : uint8_t { INT32, VARCHAR, NULL_VAL } type;
    union {
        int32_t     int_val;
        StringIndex str_index;
    };

    value_t() : type(NULL_VAL), int_val(0) {}
    value_t(int32_t v) : type(INT32), int_val(v) {}
    value_t(StringIndex s) : type(VARCHAR), str_index(s) {}

    bool is_null() const { return type == NULL_VAL; }
};

namespace Contest {

struct PageHeader {
    uint16_t num_rows;
    uint16_t val_count;
};

constexpr size_t CHUNK_SIZE = 4096;

struct PagedColumn {
    vector<unique_ptr<value_t[]>> pages;
    size_t total_size = 0;
    size_t capacity = 0;

    bool is_view = false;
    vector<const int32_t*> view_chunks;
    uint32_t view_rows_per_page = 0;

    void append(const value_t& val) {
        size_t offset = total_size & (CHUNK_SIZE - 1); 
        
        if (offset == 0) { 
            pages.emplace_back(new value_t[CHUNK_SIZE]);
            capacity += CHUNK_SIZE;
        }

        pages.back()[offset] = val;
        total_size++;
    }

    void append_int32(int32_t val) {
        size_t offset = total_size & (CHUNK_SIZE - 1);
        if (offset == 0) {
            pages.emplace_back(new value_t[CHUNK_SIZE]);
            capacity += CHUNK_SIZE;
        }
        value_t& slot = pages.back()[offset];
        slot.type = value_t::INT32;
        slot.int_val = val;
        total_size++;
    }


    inline value_t get(size_t idx) const {
        if (is_view) {
            size_t page_idx = idx / view_rows_per_page;
            size_t offset   = idx % view_rows_per_page;
            return value_t(view_chunks[page_idx][offset]);
        }
        size_t page_idx = idx / CHUNK_SIZE; 
        size_t offset   = idx % CHUNK_SIZE; 
        return pages[page_idx][offset];
    }

    size_t size() const { return total_size; }
};

using ExecuteResult = vector<PagedColumn>;

ExecuteResult execute_impl(const Plan& plan, size_t node_idx);

inline bool get_bitmap_at(const uint8_t* bitmap, uint16_t idx) {
    return bitmap[idx / 8] & (1u << (idx % 8));
}

struct ColumnCursor {
    const Column* column_ptr;
    size_t         page_idx;
    const uint8_t* page_data;
    uint16_t       num_rows_in_page;
    uint16_t       current_row_in_page; 
    uint16_t       non_null_idx; 
    
    uint16_t       table_id;
    uint16_t       col_id;
    DataType       type;
    size_t         num_pages;

    const uint8_t* bitmap_ptr;
    const uint8_t* data_start_ptr; 
    const uint16_t* offsets_ptr;    
    const char* chars_base_ptr; 

    ColumnCursor(const ColumnarTable& table, size_t t_id, size_t c_id, DataType t) 
        : column_ptr(&table.columns[c_id]), page_idx(0), 
          table_id(t_id), col_id(c_id), type(t) {
        
        num_pages = column_ptr->pages.size();
        load_page(0);
    }

    void load_page(size_t idx) {
        page_idx = idx;
        current_row_in_page = 0;
        non_null_idx = 0;

        if (page_idx < num_pages) {
            page_data = reinterpret_cast<const uint8_t*>(column_ptr->pages[page_idx]->data);
            num_rows_in_page = *reinterpret_cast<const uint16_t*>(page_data);
            
            if (type == DataType::VARCHAR && (num_rows_in_page == 0xFFFF || num_rows_in_page == 0xFFFE)) {
                bitmap_ptr = nullptr;
                offsets_ptr = nullptr;
                return; 
            }

            size_t bitmap_size = (num_rows_in_page + 7) / 8;
            bitmap_ptr = page_data + PAGE_SIZE - bitmap_size;

            if (type == DataType::INT32) {
                data_start_ptr = page_data + 4;
            } else if (type == DataType::VARCHAR) {
                uint16_t num_offsets = *reinterpret_cast<const uint16_t*>(page_data + 2);
                offsets_ptr = reinterpret_cast<const uint16_t*>(page_data + 4);
                chars_base_ptr = reinterpret_cast<const char*>(page_data + 4 + num_offsets * 2);
            }
        } else {
            page_data = nullptr;
        }
    }

    void advance_page() {
        load_page(page_idx + 1);
    }

    value_t next() {
        while (true) { 
            if (!page_data) return value_t(); 

            if (type == DataType::VARCHAR && num_rows_in_page == 0xFFFE) {
                advance_page();
                continue;
            }

            if (type == DataType::VARCHAR && num_rows_in_page == 0xFFFF) {
                uint16_t chunk_len = *reinterpret_cast<const uint16_t*>(page_data + 2);
                StringIndex idx;
                idx.table_id = table_id;
                idx.col_id   = col_id;
                idx.page_id  = page_idx; 
                idx.offset   = 4; 
                idx.length   = 0; 

                advance_page(); 
                return value_t(idx);
            }

            if (current_row_in_page >= num_rows_in_page) {
                advance_page();
                continue; 
            }

            bool is_valid = false;
            if (bitmap_ptr) {
                is_valid = get_bitmap_at(bitmap_ptr, current_row_in_page);
            }
            current_row_in_page++;

            if (!is_valid) {
                return value_t(); 
            }

            if (type == DataType::INT32) {
                const int32_t* arr = reinterpret_cast<const int32_t*>(data_start_ptr);
                int32_t val = arr[non_null_idx++];
                return value_t(val);

            } else if (type == DataType::VARCHAR) {
                uint16_t end_offset = offsets_ptr[non_null_idx];
                uint16_t start_offset = (non_null_idx == 0) ? 0 : offsets_ptr[non_null_idx - 1];
                uint16_t len = end_offset - start_offset;
                
                uint64_t base_delta = reinterpret_cast<const uint8_t*>(chars_base_ptr) - page_data;
                uint32_t final_offset = static_cast<uint32_t>(base_delta) + static_cast<uint32_t>(start_offset);

                if (final_offset + len > PAGE_SIZE) {
                    advance_page();
                    continue; 
                }

                non_null_idx++;

                StringIndex idx;
                idx.table_id = table_id;
                idx.col_id   = col_id;
                idx.page_id  = page_idx;
                idx.offset   = final_offset;
                idx.length   = len;

                return value_t(idx);
            }
        }
    }
};

struct JoinAlgorithm {
    bool                                             build_left;
    ExecuteResult&                                   left;
    ExecuteResult&                                   right;
    ExecuteResult&                                   results;
    size_t                                           left_col, right_col;
    const vector<tuple<size_t, DataType>>& output_attrs;

    void run() {
    using JoinType   = int32_t;
    using TableTuple = typename UnchainedHashTable<JoinType, size_t>::Tuple;

    auto& build_rel = build_left ? left : right;
    auto& probe_rel = build_left ? right : left;

    size_t build_col_idx = build_left ? left_col  : right_col;
    size_t probe_col_idx = build_left ? right_col : left_col;

    results.resize(output_attrs.size());

    size_t build_rows = build_rel.empty() ? 0 : build_rel[0].size();
    size_t probe_rows = probe_rel.empty() ? 0 : probe_rel[0].size();

    size_t build_probe_size = max(build_rows, probe_rows);
    size_t num_partitions = 1;
    if (build_probe_size > 0) {
        num_partitions = min<size_t>(64, max<size_t>(1, build_probe_size / 4096));
       size_t p = 1;
        while (p < num_partitions) p <<= 1;
        num_partitions = p;
    }

    PartitionAlloc level3[num_partitions];
    mutex part_mutexes[num_partitions];

    int n = num_partitions;  // num of threads
    constexpr size_t chunk_size = 2048;  // fixed chunk size

#   pragma omp parallel
    {
        vector<TableTuple> local_tuples;
        local_tuples.reserve(1024);

#   pragma omp for schedule(guided, chunk_size)     //collect tuples parallel
        for (size_t i = 0; i < build_rows; ++i) {
            value_t key_val = build_rel[build_col_idx].get(i);
            if (key_val.is_null()) continue;

            JoinType key = key_val.int_val;
            uint32_t h   = _mm_crc32_u32(0, static_cast<uint32_t>(key));
            size_t part  = h & (num_partitions - 1);

            local_tuples.push_back({key, i});

            if (local_tuples.size() >= 256) {
                lock_guard<mutex> lock(part_mutexes[part]);
                for (auto& tuple : local_tuples) {
                    if (level3[part].freeSpace() < sizeof(TableTuple)) {
                        Chunk* c = new Chunk();
                        c->data = new uint8_t[SMALL_CHUNK_SIZE];
                        c->capacity = SMALL_CHUNK_SIZE;
                        level3[part].addSpace(c);
                    }
                    TableTuple* slot = level3[part].allocate<TableTuple>();
                    *slot = tuple;
                }
                local_tuples.clear();
            }
        }

        if (!local_tuples.empty()) {    
            for (auto& tuple : local_tuples) {
                size_t part = _mm_crc32_u32(0, static_cast<uint32_t>(tuple.key)) & (num_partitions - 1);
                lock_guard<mutex> lock(part_mutexes[part]);
                if (level3[part].freeSpace() < sizeof(TableTuple)) {
                    Chunk* c = new Chunk();
                    c->data = new uint8_t[SMALL_CHUNK_SIZE];
                    c->capacity = SMALL_CHUNK_SIZE;
                    level3[part].addSpace(c);
                }
                TableTuple* slot = level3[part].allocate<TableTuple>();
                *slot = tuple;
            }
        }
    }


    UnchainedHashTable<JoinType, size_t> hash_table(build_rows);
    hash_table.build_from_slabs(level3, num_partitions); //build the hash table from tuples

    struct OutputAccessor { bool from_left; size_t col; };  //struct to precompute output accessors
    vector<OutputAccessor> accessors;  
    accessors.reserve(output_attrs.size()); 
    
    for (auto [idx, _] : output_attrs)
        accessors.push_back({idx < left.size(), idx < left.size() ? idx : idx - left.size()});

    atomic<size_t> global_idx(0);  //atomic counter
    constexpr size_t grain_size = 4096;     
    int num_threads = omp_get_max_threads();

    vector<vector<vector<value_t>>> all_thread_results(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        all_thread_results[t].resize(accessors.size());
        for (size_t out = 0; out < accessors.size(); ++out)
            all_thread_results[t][out].reserve(grain_size);
    }

#pragma omp parallel    //parallel probe, with thread stealing (dynamic)
        {
            int tid = omp_get_thread_num(); 
            auto& local = all_thread_results[tid];

            while (true) {
                size_t start = global_idx.fetch_add(grain_size, memory_order_relaxed);
                if (start >= probe_rows) break;
                size_t end = min(start + grain_size, probe_rows);

                for (size_t i = start; i < end; ++i) {
                    value_t key_val = probe_rel[probe_col_idx].get(i);
                    if (key_val.is_null()) continue;

                    auto matches = hash_table.find(key_val.int_val);
                    for (size_t* match_ptr : matches) {
                        size_t match_idx = *match_ptr;

                        size_t left_row  = build_left ? match_idx : i;
                        size_t right_row = build_left ? i         : match_idx;

                        for (size_t out = 0; out < accessors.size(); ++out)
                            local[out].push_back(accessors[out].from_left
                                                ? left[accessors[out].col].get(left_row)
                                                : right[accessors[out].col].get(right_row));
                    }
                }
            }
        }

        for (size_t out = 0; out < accessors.size(); ++out) {   //single threaded aggeration
            for (int t = 0; t < num_threads; ++t) {
                for (auto& v : all_thread_results[t][out])
                    results[out].append(v);
            }
        }
    }
};




ExecuteResult execute_hash_join(const Plan& plan,
    const JoinNode& join,
    const vector<tuple<size_t, DataType>>& output_attrs) {
    
    auto left_res  = execute_impl(plan, join.left);
    auto right_res = execute_impl(plan, join.right);
    
    ExecuteResult results; 

    JoinAlgorithm algo{
        .build_left   = join.build_left,
        .left         = left_res,
        .right        = right_res,
        .results      = results,
        .left_col     = join.left_attr,
        .right_col    = join.right_attr,
        .output_attrs = output_attrs
    };
    algo.run();

    return results;
}

ExecuteResult execute_scan(const Plan& plan,
                           const ScanNode& scan,
                           const vector<tuple<size_t, DataType>>& output_attrs) {

    const auto table_id    = scan.base_table_id;
    const auto& table      = plan.inputs[table_id];
    const size_t total_rows = table.num_rows;

    ExecuteResult results;
    results.resize(output_attrs.size());

    for (size_t out_idx = 0; out_idx < output_attrs.size(); ++out_idx) {
        const auto [col_idx, type] = output_attrs[out_idx];
        auto& out_col = results[out_idx];
        const auto& in_col = table.columns[col_idx];

        if (type == DataType::INT32 && !in_col.pages.empty()) {
            bool can_view = true;
            uint32_t rows_per_page = 0;

            for (auto* page : in_col.pages) {
                const auto* header = reinterpret_cast<const PageHeader*>(page->data);
                if (header->num_rows != header->val_count) {
                    can_view = false;
                    break;
                }
                if (rows_per_page == 0) rows_per_page = header->num_rows;
            }

            if (can_view) {
                out_col.is_view = true;
                out_col.total_size = total_rows;
                out_col.view_rows_per_page = rows_per_page;
                out_col.view_chunks.reserve(in_col.pages.size());

                for (auto* page : in_col.pages) {
                    const auto* raw = reinterpret_cast<const int32_t*>(
                        reinterpret_cast<const uint8_t*>(page->data) + sizeof(PageHeader)
                    );
                    out_col.view_chunks.push_back(raw);
                }
                continue;  
            }
        }
        
        ColumnCursor cursor(table, table_id, col_idx, type);
        for (size_t r = 0; r < total_rows; ++r) {
            out_col.append(cursor.next());
        }
    }
    return results;
}


ExecuteResult execute_impl(const Plan& plan, size_t node_idx) {
    auto& node = plan.nodes[node_idx];
    return visit(
        [&](const auto& value) {
            using T = decay_t<decltype(value)>;
            if constexpr (is_same_v<T, JoinNode>) {
                return execute_hash_join(plan, value, node.output_attrs);
            } else {
                return execute_scan(plan, value, node.output_attrs);
            }
        },
        node.data);
}

string materialize_string(const value_t& val, const Plan& plan) {
    StringIndex idx = val.str_index;
    if (idx.table_id >= plan.inputs.size()) return "";
    const auto& col = plan.inputs[idx.table_id].columns[idx.col_id];
    if (idx.page_id >= col.pages.size()) return "";
    
    const uint8_t* page_data = reinterpret_cast<const uint8_t*>(col.pages[idx.page_id]->data);
    uint16_t header = *reinterpret_cast<const uint16_t*>(page_data);

    if (header == 0xFFFF) {
        string full_string;
        uint16_t chunk_len = *reinterpret_cast<const uint16_t*>(page_data + 2);
        if (chunk_len > PAGE_SIZE) chunk_len = 0;
        full_string.append(reinterpret_cast<const char*>(page_data + 4), chunk_len);
        
        size_t next_page_idx = idx.page_id + 1;
        while (next_page_idx < col.pages.size()) {
            const uint8_t* next_page_data = reinterpret_cast<const uint8_t*>(col.pages[next_page_idx]->data);
            uint16_t next_header = *reinterpret_cast<const uint16_t*>(next_page_data);
            if (next_header != 0xFFFE) break; 
            uint16_t next_chunk_len = *reinterpret_cast<const uint16_t*>(next_page_data + 2);
            if (next_chunk_len > PAGE_SIZE) break;
            full_string.append(reinterpret_cast<const char*>(next_page_data + 4), next_chunk_len);
            next_page_idx++;
        }
        if (!full_string.empty() && full_string.back() == '\0') full_string.pop_back();
        return full_string;
    }
    
    if (idx.offset + idx.length > PAGE_SIZE) return "";
    const char* ptr = reinterpret_cast<const char*>(page_data) + idx.offset;
    size_t len = idx.length;
    if (len > 0 && ptr[len - 1] == '\0') return string(ptr, len - 1);
    return string(ptr, len);
}

ColumnarTable execute(const Plan& plan, [[maybe_unused]] void* context) {
    auto columns = execute_impl(plan, plan.root);

    size_t num_rows = columns.empty() ? 0 : columns[0].size();
    size_t num_cols = columns.size();

    ColumnarTable output_table;
    output_table.num_rows = num_rows;

    output_table.columns.reserve(num_cols);
    for (size_t j = 0; j < num_cols; ++j)
        output_table.columns.emplace_back(get<1>(plan.nodes[plan.root].output_attrs[j]));

    int threads = omp_get_max_threads(); // always use max threads
    constexpr size_t chunk_size = 1024;  // fixed chunk size

#pragma omp parallel for schedule(guided) num_threads(threads)  // parallel per-column guided schedule
    for (size_t j = 0; j < num_cols; ++j) {
        auto type = get<1>(plan.nodes[plan.root].output_attrs[j]);
        auto& src = columns[j];
        auto& dst = output_table.columns[j];

        if (type == DataType::INT32) {
            ColumnInserter<int32_t> inserter(dst);
            if (src.is_view && src.total_size == num_rows) {
                for (size_t pg = 0; pg < src.view_chunks.size(); ++pg) {
                    const int32_t* chunk = src.view_chunks[pg];
                    size_t start = pg * src.view_rows_per_page;
                    size_t end   = min(start + src.view_rows_per_page, num_rows);
                    for (size_t i = start; i < end; ++i)
                        inserter.insert(chunk[i - start]);
                }
            } else {
                for (size_t start = 0; start < num_rows; start += chunk_size) {
                    size_t end = min(start + chunk_size, num_rows);
                    for (size_t i = start; i < end; ++i) {
                        value_t val = src.get(i);
                        if (val.is_null()) inserter.insert_null();
                        else inserter.insert(val.int_val);
                    }
                }
            }
            inserter.finalize();
        }
        else if (type == DataType::VARCHAR) {
            ColumnInserter<string> inserter(dst);
            for (size_t start = 0; start < num_rows; start += chunk_size) {
                size_t end = min(start + chunk_size, num_rows);
                for (size_t i = start; i < end; ++i) {
                    value_t val = src.get(i);
                    if (val.is_null()) inserter.insert_null();
                    else inserter.insert(materialize_string(val, plan));
                }
            }
            inserter.finalize();
        }
    }

    return output_table;
}





void* build_context() { return nullptr; }
void destroy_context([[maybe_unused]] void* context) {}
}