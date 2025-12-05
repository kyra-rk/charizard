# ----- PROJECT CONFIGURATION -----
cmake_minimum_required(VERSION 3.16)
project(charizard_carbon_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----- OPTIONS -----
option(CHARIZARD_ENABLE_LTO "Enable link-time optimization" ON)
option(CHARIZARD_WITH_MONGO "Build with MongoDB persistence" ON)
option(CHARIZARD_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

if(CHARIZARD_WITH_MONGO)
  find_package(mongocxx CONFIG REQUIRED)
  find_package(bsoncxx CONFIG REQUIRED)
endif()

function(enable_coverage_for target)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    # link options needed so the runtime writes .profraw files
    if (COMMAND target_link_options)
      target_link_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    endif()
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_libraries(${target} PRIVATE --coverage)
  endif()
endfunction()

include(FetchContent)

# ----- DEPENDENCIES -----
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

# ----- TARGETS -----
set(CHARIZARD_API_SOURCES
  src/main.cpp
  $<TARGET_OBJECTS:charizard_api_obj>
)

if(CHARIZARD_WITH_MONGO)
  list(APPEND CHARIZARD_API_SOURCES src/mongo_store.cpp)
endif()

add_executable(charizard_api ${CHARIZARD_API_SOURCES})

target_include_directories(charizard_api PRIVATE
  include
  ${cpp_httplib_SOURCE_DIR}
)

target_link_libraries(charizard_api PRIVATE nlohmann_json::nlohmann_json)

if(CHARIZARD_WITH_MONGO)
  target_compile_definitions(charizard_api PRIVATE CHARIZARD_WITH_MONGO=1)
  target_link_libraries(charizard_api PRIVATE mongo::mongocxx_shared mongo::bsoncxx_shared)
endif()

# ----- TESTING -----
# API/library objects to test against
add_library(charizard_api_obj OBJECT
  src/api.cpp
  src/transit_validator.cpp
  src/transit_logic.cpp
  src/emission_factors.cpp
  src/emission_data_loader.cpp
  src/emission_calculator.cpp
  src/test_auth_helpers.cpp
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
  tests/unit/test_transit_logic.cpp
  tests/unit/test_auth.cpp
  tests/unit/test_storage.cpp
  tests/unit/test_emission_factors.cpp
  $<TARGET_OBJECTS:charizard_api_obj>
  # Any other unit test files to compile and run
)
target_include_directories(charizard_unit_tests PRIVATE
  include
  ${cpp_httplib_SOURCE_DIR}
)
target_link_libraries(charizard_unit_tests PRIVATE gtest_main nlohmann_json::nlohmann_json)

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

# ---- TEST COVERAGE ----
include(GoogleTest)
gtest_discover_tests(charizard_unit_tests
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(charizard_api_tests
  DISCOVERY_MODE PRE_TEST
)

if (CHARIZARD_ENABLE_COVERAGE)
  message(STATUS "Building with coverage instrumentation enabled")
  enable_coverage_for(charizard_api_obj)
  enable_coverage_for(charizard_api)
  enable_coverage_for(charizard_unit_tests)
  enable_coverage_for(charizard_api_tests)
endif()

# macOS: enable rpath for convenience
set_target_properties(charizard_api PROPERTIES
  MACOSX_RPATH ON
)

# ----- LINK TIME OPTIMIZATION -----
if(CHARIZARD_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT error)
  if(ipo_supported)
    set_property(TARGET charizard_api PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# ---- STYLE CHECKING / LINTING ----
if(DEFINED CLANG_TIDY AND CLANG_TIDY)
  find_program(CLANG_TIDY_BIN NAMES clang-tidy clang-tidy-18 clang-tidy-17 PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
  if(CLANG_TIDY_BIN)
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_BIN}")
    set(CMAKE_CXX_CLANG_TIDY
        ${CLANG_TIDY_BIN};
        -p=${CMAKE_BINARY_DIR};
        -header-filter=^src/;
    )
  else()
    message(WARNING "Requested CLANG_TIDY=ON but clang-tidy not found")
  endif()
endif()

