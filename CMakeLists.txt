cmake_minimum_required(VERSION 3.16)

project(SigmodContest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

Include(FetchContent)

FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.0.tar.gz
)

FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(
    abseil
    URL https://github.com/abseil/abseil-cpp/releases/download/20240722.1/abseil-cpp-20240722.1.tar.gz
)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_ENABLE_INSTALL ON)
FetchContent_MakeAvailable(abseil)

FetchContent_Declare(
    re2
    URL https://github.com/google/re2/releases/download/2024-07-02/re2-2024-07-02.tar.gz
)

FetchContent_MakeAvailable(re2)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(json)

FetchContent_Declare(
    sql-parser
    URL https://github.com/a858438680/sql-parser/archive/refs/tags/win-port-2.tar.gz
)
set(HSQL_ENABLE_WERROR OFF)
FetchContent_MakeAvailable(sql-parser)

FetchContent_Declare(
    range-v3
    URL https://github.com/ericniebler/range-v3/archive/refs/tags/0.12.0.tar.gz
)

FetchContent_MakeAvailable(range-v3)

FetchContent_Declare(
    fmtlib
    URL https://github.com/fmtlib/fmt/releases/download/11.1.3/fmt-11.1.3.zip
)

find_package(OpenMP REQUIRED)

FetchContent_MakeAvailable(fmtlib)

FetchContent_Declare(
    duckdb
    URL https://github.com/duckdb/duckdb/archive/refs/tags/v1.2.1.tar.gz
)
set(ENABLE_SANITIZER OFF)
set(ENABLE_UBSAN OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc|powerpc|ppc64|ppc64le")
    message("Disabling jemalloc extension of DuckDB on Power.")
    set(SKIP_EXTENSIONS jemalloc)
endif()
FetchContent_MakeAvailable(duckdb)

# Include all sources from /src directory. CONFIGURE_DEPENDS can be unreliable.
# Try re-running cmake in case changes are not recognized.
file(GLOB SIGMODPC_SRC
    CONFIGURE_DEPENDS
    "src/*.cpp"
)

# Enable server-specific compiler optimizations.
# Use march=native for all but Power servers, which results in the following error:
#    clang++-18: error: unsupported option '-march=' for target 'powerpc64le-unknown-linux-gnu'
# This flag works on other Power systems, but for now, we disable march=native on all Power machines.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ppc|powerpc|ppc64|ppc64le")
    add_compile_options(-march=native)
endif()

# Targets
add_executable(run ${SIGMODPC_SRC} tests/read_sql.cpp)
add_executable(unit_tests ${SIGMODPC_SRC} tests/unit_tests.cpp)
add_executable(build_database tests/build_database.cpp)

set(TEST_SOURCES ${SIGMODPC_SRC})

list(FILTER TEST_SOURCES EXCLUDE REGEX "execute\.cpp$")

add_executable(test_column_store tests/test_column_store.cpp ${TEST_SOURCES})

target_link_libraries(test_column_store PRIVATE 
    Catch2::Catch2WithMain 
    re2 
    fmt 
    range-v3 
    nlohmann_json::nlohmann_json 
    sqlparser
    OpenMP::OpenMP_CXX
)

target_include_directories(test_column_store PRIVATE include src)

# For all executables
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(run PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(build_database PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(unit_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_executable(build_cache ${SIGMODPC_SRC} tests/read_sql.cpp)
    add_executable(fast ${SIGMODPC_SRC} tests/read_sql.cpp)

    target_link_libraries(build_cache PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser duckdb)
    target_compile_definitions(build_cache PRIVATE TEAMOPT_USE_DUCKDB TEAMOPT_BUILD_CACHE)
    target_link_libraries(fast PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser)
    target_include_directories(build_cache PRIVATE include)
    target_include_directories(fast PRIVATE include)
endif()

set(FAST_SRC ${SIGMODPC_SRC} tests/read_sql.cpp)

add_executable(fast_robin   ${FAST_SRC})
add_executable(fast_hop     ${FAST_SRC})
add_executable(fast_cuckoo  ${FAST_SRC})
add_executable(fast_std     ${FAST_SRC})
add_executable(fast_unc     ${FAST_SRC})

# Ίδιες βιβλιοθήκες & include dirs
target_link_libraries(fast_robin  PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser)
target_link_libraries(fast_hop    PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser)
target_link_libraries(fast_cuckoo PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser)
target_link_libraries(fast_std    PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser)
target_link_libraries(fast_unc    PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser OpenMP::OpenMP_CXX)


target_include_directories(fast_robin  PRIVATE include)
target_include_directories(fast_hop    PRIVATE include)
target_include_directories(fast_cuckoo PRIVATE include)
target_include_directories(fast_std    PRIVATE include)
target_include_directories(fast_unc    PRIVATE include)

# Μακροεντολές για compile-time επιλογή
target_compile_definitions(fast_robin  PRIVATE USE_ROBIN)
target_compile_definitions(fast_hop    PRIVATE USE_HOP)
target_compile_definitions(fast_cuckoo PRIVATE USE_CUCKOO)
target_compile_definitions(fast_std    PRIVATE USE_STD)
target_compile_definitions(fast_unc    PRIVATE USE_UNC)
target_compile_options(fast_unc PRIVATE ${OpenMP_CXX_FLAGS})

add_executable(leaderboard ${FAST_SRC})

target_link_libraries(leaderboard PRIVATE
    re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser OpenMP::OpenMP_CXX
)

target_include_directories(leaderboard PRIVATE include)
target_compile_definitions(leaderboard PRIVATE USE_UNC)
target_compile_options(leaderboard PRIVATE ${OpenMP_CXX_FLAGS})

add_executable(test_cuckoo tests/test_cuckoo.cpp)
target_link_libraries(test_cuckoo PRIVATE 
re2 range-v3 fmt
nlohmann_json::nlohmann_json
sqlparser
Catch2::Catch2WithMain OpenMP::OpenMP_CXX)
target_include_directories(test_cuckoo PRIVATE src)

add_executable(test_robinhood tests/test_robinhood.cpp)
target_link_libraries(test_robinhood PRIVATE Catch2::Catch2WithMain)
target_include_directories(test_robinhood PRIVATE src)

add_executable(test_hopscotch tests/test_hopscotch.cpp)
target_link_libraries(test_hopscotch PRIVATE Catch2::Catch2WithMain)
target_include_directories(test_hopscotch PRIVATE src)

add_executable(test_unchained tests/test_unchained.cpp)
target_link_libraries(test_unchained PRIVATE Catch2::Catch2WithMain)
target_include_directories(test_unchained PRIVATE src)

target_link_libraries(run PRIVATE re2 fmt range-v3 nlohmann_json::nlohmann_json sqlparser duckdb)
target_compile_definitions(run PRIVATE TEAMOPT_USE_DUCKDB)
target_link_libraries(build_database PRIVATE fmt duckdb)
target_link_libraries(unit_tests PRIVATE re2 range-v3 fmt Catch2::Catch2WithMain)

target_include_directories(run PRIVATE include)
target_include_directories(build_database PRIVATE include)
target_include_directories(unit_tests PRIVATE include)
