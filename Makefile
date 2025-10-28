# ---------- Settings ----------
# Build dir and target name must match your CMake project
BUILD_DIR ?= build
TARGET    ?= charizard_api

# Use bash for nicer scripting
SHELL := /bin/bash

# Default configuration (Debug or Release)
# OVERRIDE: CONFIG=Release
CONFIG ?= Debug

# Server env
# OVERRIDE: `make run HOST=0.0.0.0 PORT=9000`
HOST ?= 127.0.0.1
PORT ?= 8080

# Extra args to pass to the binary
# OVERRIDE: `make run ARGS="--flag foo"`
ARGS ?=

# CTest options
CTEST_FLAGS ?= --output-on-failure
# Run a subset of tests
# OVERRIDE: `make test-one TEST=health`
TEST ?=

# Paths to check/format
FORMAT_PATHS := src include tests

# Find location of LLVM
LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
ifeq ($(LLVM_PREFIX),)
  LLVM_BIN ?= /opt/homebrew/opt/llvm/bin
else
  LLVM_BIN ?= $(LLVM_PREFIX)/bin
endif

# Style checking and linting variables
CLANG_FORMAT ?= clang-format
CLANG_TIDY   ?= $(LLVM_BIN)/clang-tidy
LLVM_COV     ?= $(LLVM_BIN)/llvm-cov
LLVM_PROFDATA?= $(LLVM_BIN)/llvm-profdata

# Detect CPU cores (macOS or Linux)
CORES := $(shell (sysctl -n hw.ncpu 2>/dev/null) || (nproc 2>/dev/null) || echo 4)

# ---------- Phony ----------
.PHONY: help configure build build-cov run debug release clean distclean \
        rebuild test test-verbose test-list test-one test-unit test-api \
        build-tests format format-check lint lint-fix check coverage cov-open

# ---------- Help ----------
help:
	@echo ""
	@echo "Make targets:"
	@echo "  Build & Run:"
	@echo "    build           Configure (if needed) and build ($(CONFIG))"
	@echo "    run             Build then run the server (HOST=$(HOST) PORT=$(PORT))"
	@echo "    build-cov	   Configure build with coverage instrumentation"
	@echo ""
	@echo "  Testing:"
	@echo "    test            Build and run all CTest tests ($(CTEST_FLAGS))"
	@echo "    test-verbose    Same as 'test' but verbose"
	@echo "    test-list       List discovered tests (no run)"
	@echo "    test-one        Run tests matching regex: make test-one TEST=health"
	@echo "    test-unit       Run tests with names matching 'unit' (regex)"
	@echo "    test-api        Run tests with names matching 'api' (regex)"
	@echo ""
	@echo "  Style Checking & Linting:"
	@echo "    format          Auto-format sources with clang-format"
	@echo "    format-check    Check formatting (no changes; fails on diffs)"
	@echo "    lint            Run clang-tidy (no fixes)"
	@echo "    lint-fix        Run clang-tidy with auto-fixes + reformat"
	@echo "    check           Run format-check and lint"
	@echo ""
	@echo "  Test Coverage:"
	@echo "    coverage        Run tests with coverage & generate HTML report"
	@echo "    cov-open        Open the generated coverage HTML"
	@echo ""
	@echo "  Cleanup:"
	@echo "    clean           Remove build artifacts (keeps CMake cache)"
	@echo "    distclean       Remove build dir and CMake cache"
	@echo ""
	@echo "Vars you can override: CONFIG={Debug|Release} HOST=<ip> PORT=<num> ARGS=\"...\" TEST=<regex> LLVM_BIN=<path>"
	@echo "Examples:"
	@echo "  make release run HOST=0.0.0.0 PORT=9000"
	@echo "  make test-one TEST=health"
	@echo "  make coverage LLVM_BIN=$$(brew --prefix llvm)/bin"
	@echo ""

# ---------- Configure & Build ----------
configure:
	@mkdir -p $(BUILD_DIR)
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)

build: configure
	@cmake --build $(BUILD_DIR) -j

build-cov:
	@mkdir -p $(BUILD_DIR)
	@echo "==> Configuring build with coverage instrumentation..."
	@cmake -S . -B $(BUILD_DIR) \
	  -DCMAKE_BUILD_TYPE=Debug \
	  -DCHARIZARD_ENABLE_COVERAGE=ON

# Build tests explicitly (alias of build; handy in CI)
build-tests: build

# Convenience configs
debug:
	@$(MAKE) build CONFIG=Debug

release:
	@$(MAKE) build CONFIG=Release

# ---------- Run ----------
run: build
	@echo "==> Running $(TARGET) (CONFIG=$(CONFIG)) on $(HOST):$(PORT)"
	@HOST=$(HOST) PORT=$(PORT) $(BUILD_DIR)/$(TARGET) $(ARGS)

# ---------- Tests ----------
test: build-tests
	@ctest --test-dir $(BUILD_DIR) $(CTEST_FLAGS)

test-verbose: build-tests
	@ctest --test-dir $(BUILD_DIR) -VV $(CTEST_FLAGS)

test-list: build-tests
	@ctest --test-dir $(BUILD_DIR) -N

test-one: build-tests
	@if [ -z "$(TEST)" ]; then echo "Usage: make test-one TEST=<regex>"; exit 2; fi
	@ctest --test-dir $(BUILD_DIR) -R "$(TEST)" $(CTEST_FLAGS)

test-unit: build-tests
	@ctest --test-dir $(BUILD_DIR) -R "unit" $(CTEST_FLAGS)

test-api: build-tests
	@ctest --test-dir $(BUILD_DIR) -R "api|health" $(CTEST_FLAGS)

# ---------- Clean ----------
clean:
	@$(MAKE) -C $(BUILD_DIR) clean || true

distclean:
	@rm -rf $(BUILD_DIR)

rebuild: distclean build

# ---------- Formatting ----------
format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@find $(FORMAT_PATHS) -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" \) -print0 | xargs -0 $(CLANG_FORMAT) -i

format-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "clang-format not found. brew install clang-format"; exit 1; }
	@echo "==> Checking formatting..."
	@find $(FORMAT_PATHS) -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.hh' -o -name '*.cc' -o -name '*.cpp' \) -print0 | \
	  xargs -0 $(CLANG_FORMAT) -n --Werror --style=file

# ---------- Linting ----------
lint: build
	@echo "==> Running clang-tidy (no fixes) with $(CORES) jobs..."
	@PATH="$(LLVM_BIN):$$PATH"; \
	find $(FORMAT_PATHS) -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0 | \
	  xargs -0 -n1 -P $(CORES) "$(CLANG_TIDY)" -p "$(BUILD_DIR)" --quiet

lint-fix: build
	@echo "==> Running clang-tidy with -fix (and formatting) ..."
	@PATH="$(LLVM_BIN):$$PATH"; \
	find $(FORMAT_PATHS) -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0 | \
	  xargs -0 -n1 -P $(CORES) "$(CLANG_TIDY)" -p "$(BUILD_DIR)" -fix --format-style=file --quiet || true
	@$(MAKE) format

check: format-check lint

# ---------- Coverage (LLVM/Clang workflow) ----------
# NOTE: Configure with coverage instrumentation in CMake:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCHARIZARD_ENABLE_COVERAGE=ON
coverage: build-tests
	@echo "==> Running tests with coverage instrumentation..."
	@mkdir -p $(BUILD_DIR)/coverage
	@LLVM_PROFILE_FILE=$(abspath $(BUILD_DIR))/coverage/%p-%m.profraw \
	  ctest --test-dir $(BUILD_DIR) --output-on-failure
	@echo "==> Using LLVM tools from: $(LLVM_BIN)"
	@if [ -x "$(LLVM_PROFDATA)" ] && [ -x "$(LLVM_COV)" ]; then \
	  echo "==> Merging raw profiles..."; \
	  "$(LLVM_PROFDATA)" merge -sparse $(BUILD_DIR)/coverage/*.profraw -o $(BUILD_DIR)/coverage/coverage.profdata; \
	  echo "==> Generating HTML coverage..."; \
	  "$(LLVM_COV)" show \
	    $(BUILD_DIR)/charizard_unit_tests $(BUILD_DIR)/charizard_api_tests \
	    -instr-profile $(BUILD_DIR)/coverage/coverage.profdata \
	    -format=html -output-dir $(BUILD_DIR)/coverage_html \
	    -ignore-filename-regex='(tests|_deps|gtest|nlohmann|cpp-httplib)'; \
	  echo "==> Summary:"; \
	  "$(LLVM_COV)" report \
	    $(BUILD_DIR)/charizard_unit_tests $(BUILD_DIR)/charizard_api_tests \
	    -instr-profile $(BUILD_DIR)/coverage/coverage.profdata \
	    -ignore-filename-regex='(tests|_deps|gtest|nlohmann|cpp-httplib)'; \
	  echo "Coverage report: $(BUILD_DIR)/coverage_html/index.html"; \
	else \
	  echo "ERROR: llvm-cov / llvm-profdata not found. Install with: brew install llvm"; \
	  echo "Or override: make coverage LLVM_BIN=$$(brew --prefix llvm)/bin"; \
	  exit 1; \
	fi

cov-open:
	@open $(BUILD_DIR)/coverage_html/index.html 2>/dev/null || \
	 (echo "Open manually: $(BUILD_DIR)/coverage_html/index.html"; exit 0)
