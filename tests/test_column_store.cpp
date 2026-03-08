#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <iostream>
#include <vector>
#define USE_UNC
#include "../src/macro.h"
#include "../src/execute.cpp"

using namespace Contest;

TEST_CASE("PagedColumn append/get") {
    PagedColumn col;

    for(int i=0; i<5000; i++){
        col.append(value_t(i));
    }

    REQUIRE(col.get(1234).int_val == 1234);
    REQUIRE(col.size() == 5000);
}

TEST_CASE("PagedColumn multiple pages") {
    PagedColumn col;

    for(int i=0; i<CHUNK_SIZE + 10; i++){
        col.append(value_t(i));
    }

    REQUIRE(col.get(CHUNK_SIZE + 5).int_val == CHUNK_SIZE + 5);
    REQUIRE(col.size() == CHUNK_SIZE + 10);
}


TEST_CASE("Memory Layout & Bit Packing", "[memory]") {

    REQUIRE(sizeof(StringIndex) == 8);

    REQUIRE(sizeof(value_t) == 16);

    StringIndex idx;
    idx.table_id = 63; 
    idx.col_id = 63;
    idx.page_id = 65535;
    idx.offset = 1048575;
    idx.length = 65535;

    REQUIRE(idx.table_id == 63);
    REQUIRE(idx.col_id == 63);
    REQUIRE(idx.page_id == 65535);
    REQUIRE(idx.offset == 1048575);
    REQUIRE(idx.length == 65535);
}

TEST_CASE("PagedColumn Pagination Logic", "[column_store]") {

    PagedColumn col;
    
    REQUIRE(col.size() == 0);
    REQUIRE(col.pages.size() == 0);

    //γεμισμα ακριβως μιας σελιδας
    for (size_t i = 0; i < CHUNK_SIZE; ++i) {
        col.append(value_t((int32_t)i));
    }

    REQUIRE(col.size() == CHUNK_SIZE);
    REQUIRE(col.pages.size() == 1);
    
    REQUIRE(col.get(0).int_val == 0);
    REQUIRE(col.get(CHUNK_SIZE - 1).int_val == CHUNK_SIZE - 1);

    col.append(value_t((int32_t)9999));

    REQUIRE(col.size() == CHUNK_SIZE + 1);
    REQUIRE(col.pages.size() == 2); 
    REQUIRE(col.get(CHUNK_SIZE).int_val == 9999);
}

TEST_CASE("Data Integrity Read/Write", "[data]") {

    PagedColumn col;
    size_t N = 10000; 

    for (size_t i = 0; i < N; ++i) {
        if (i % 2 == 0) {
            col.append(value_t((int32_t)i));
        } else {
            StringIndex idx;
            idx.table_id = 1;
            idx.col_id = 2;
            idx.page_id = 3;
            idx.offset = i; 
            idx.length = 10;
            col.append(value_t(idx));
        }
    }

    REQUIRE(col.size() == N);

    //αναγνωση και επιβεβαιωση
    for (size_t i = 0; i < N; ++i) {
        value_t val = col.get(i);
        
        if (i % 2 == 0) {
            REQUIRE(val.type == value_t::INT32);
            REQUIRE(val.int_val == (int32_t)i);
        } else {
            REQUIRE(val.type == value_t::VARCHAR);
            REQUIRE(val.str_index.offset == i);
        }
    }
}

TEST_CASE("materialize basic INT32 / NULL / VARCHAR", "[materialize]") {
    Plan plan;
    ColumnarTable tbl;
    tbl.num_rows = 1;
    tbl.columns.emplace_back(DataType::VARCHAR);
    Column& col = tbl.columns[0];

    auto page = std::make_unique<Page>();
    uint8_t* data = reinterpret_cast<uint8_t*>(page->data);

    *reinterpret_cast<uint16_t*>(data) = 1;
    *reinterpret_cast<uint16_t*>(data + 2) = 1;
    *reinterpret_cast<uint16_t*>(data + 4) = 6;

    const char* s = "hello";
    memcpy(data + 6, s, 5);
    data[11] = '\0';

    uint8_t* bitmap = data + PAGE_SIZE - 1;
    bitmap[0] = 0x01;

    col.pages.push_back(page.release());
    plan.inputs.push_back(std::move(tbl));
    value_t v_int(12345);
    Data d1 = v_int.type == value_t::INT32 ? Data(v_int.int_val) : std::monostate{};
    REQUIRE(std::holds_alternative<int32_t>(d1));
    REQUIRE(std::get<int32_t>(d1) == 12345);
    value_t v_null;
    Data d2 = v_null.is_null() ? std::monostate{} : Data{};
    REQUIRE(std::holds_alternative<std::monostate>(d2));
    StringIndex idx;
    idx.table_id = 0;
    idx.col_id   = 0;
    idx.page_id  = 0;
    idx.offset   = 6;
    idx.length   = 6;
    value_t v_str(idx);
    Data d3 = materialize_string(v_str, plan);
    REQUIRE(std::holds_alternative<std::string>(d3));
    REQUIRE(std::get<std::string>(d3) == "hello");
}

