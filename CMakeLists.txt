cmake_minimum_required(VERSION 3.16)
project(charizard_carbon_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- OPTIONS ----
option(CHARIZARD_ENABLE_LTO "Enable link-time optimization" ON)
option(CHARIZARD_WITH_MONGO "Build with MongoDB persistence" ON)

if(CHARIZARD_WITH_MONGO)
  find_package(mongocxx CONFIG REQUIRED)
  find_package(bsoncxx CONFIG REQUIRED)
endif()

include(FetchContent)

# ---- Dependencies (header-only) ----
# cpp-httplib (API)
FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(cpp_httplib)

# nlohmann::json (serializing JSON)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# googletest (test suite)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ---- Target ----
add_executable(charizard_api
  src/main.cpp
  src/mongo_store.cpp
  $<TARGET_OBJECTS:charizard_api_obj>
)

target_include_directories(charizard_api PRIVATE
  include
  ${cpp_httplib_SOURCE_DIR}
)

target_link_libraries(charizard_api PRIVATE nlohmann_json::nlohmann_json)

if(CHARIZARD_WITH_MONGO)
  target_compile_definitions(charizard_api PRIVATE CHARIZARD_WITH_MONGO=1)
  target_link_libraries(charizard_api PRIVATE mongo::mongocxx_shared mongo::bsoncxx_shared)
endif()

# ---- Testing ----
# API/library objects to test against
add_library(charizard_api_obj OBJECT
  src/api.cpp
  # Any other non-main sources that define logic you want to reuse in tests
)
target_include_directories(charizard_api_obj PRIVATE 
  include
  ${cpp_httplib_SOURCE_DIR}
)
target_link_libraries(charizard_api_obj PRIVATE 
  nlohmann_json::nlohmann_json
)

# Unit test target
add_executable(charizard_unit_tests
  tests/unit/test_health.cpp
  $<TARGET_OBJECTS:charizard_api_obj>
  # Any other unit test files to compile and run
)
target_include_directories(charizard_unit_tests PRIVATE
  include
  ${cpp_httplib_SOURCE_DIR}
)
target_link_libraries(charizard_unit_tests PRIVATE gtest_main nlohmann_json::nlohmann_json)

add_test(NAME unit COMMAND charizard_unit_tests)

# Integration (HTTP) test target
add_executable(charizard_api_tests
  tests/integration/test_api.cpp
  $<TARGET_OBJECTS:charizard_api_obj>
  # Any other integration test files to compile and run
)
target_include_directories(charizard_api_tests PRIVATE
  include
  ${cpp_httplib_SOURCE_DIR}
)
target_link_libraries(charizard_api_tests PRIVATE gtest_main nlohmann_json::nlohmann_json)

add_test(NAME api COMMAND charizard_api_tests)

# macOS: enable rpath for convenience
set_target_properties(charizard_api PROPERTIES
  MACOSX_RPATH ON
)

# LTO
if(CHARIZARD_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT error)
  if(ipo_supported)
    set_property(TARGET charizard_api PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# ---- style checking / linting ----
if(DEFINED CLANG_TIDY AND CLANG_TIDY)
  find_program(CLANG_TIDY_BIN NAMES clang-tidy clang-tidy-18 clang-tidy-17 PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
  if(CLANG_TIDY_BIN)
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_BIN}")
    set(CMAKE_CXX_CLANG_TIDY
        ${CLANG_TIDY_BIN};
        -p=${CMAKE_BINARY_DIR};
        --quiet;
    )
  else()
    message(WARNING "Requested CLANG_TIDY=ON but clang-tidy not found")
  endif()
endif()

# ---- GoogleTest setup ----
find_package(GTest REQUIRED)
include(GoogleTest)

# Add your test executable (all .cpp files that contain TEST or TEST_F)
add_executable(run_all_tests
    test_memory.cpp
    test_utils.cpp
    test_api_handler.cpp
)

target_link_libraries(run_all_tests
    PRIVATE
    GTest::gtest
    GTest::gtest_main
    pthread
)

# Enable test discovery
gtest_discover_tests(run_all_tests)
