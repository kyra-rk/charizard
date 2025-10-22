# ---------- Settings ----------
# Build dir and target name must match your CMake project
BUILD_DIR ?= build
TARGET    ?= charizard_api

# Shell
SHELL := /bin/bash

# Default configuration (Debug or Release)
CONFIG ?= Debug

# Server env (can be overridden: `make run HOST=0.0.0.0 PORT=9000`)
HOST ?= 127.0.0.1
PORT ?= 8080

# Extra args to pass to the binary: `make run ARGS="--flag foo"`
ARGS ?=

# CTest options
CTEST_FLAGS ?= --output-on-failure
# Run a subset of tests by regex: `make test TEST=health`
TEST ?=

# Files to clang-format (optional)
FORMAT_PATHS := src include tests

# ---------- Phony ----------
.PHONY: help configure build run debug release clean distclean rebuild format \
        test test-verbose test-list test-one test-unit test-api build-tests

# TO-DO: update the help message
help:
	@echo ""
	@echo "Make targets:"
	@echo "  make build              - Configure (if needed) and build ($(CONFIG))"
	@echo "  make run                - Build then run the server"
	@echo "  make test               - Build and run all CTest tests"
	@echo "  make test-verbose       - Same as 'test' but verbose ctest output"
	@echo "  make test-list          - List discovered tests (no run)"
	@echo "  make test-one TEST=rx   - Run tests matching regex (e.g., TEST=health)"
	@echo "  make test-unit          - Run tests whose names match 'unit' (regex)"
	@echo "  make test-api           - Run tests whose names match 'api' (regex)"
	@echo "  make debug              - Build with Debug config"
	@echo "  make release            - Build with Release config"
	@echo "  make rebuild            - Clean build dir then build ($(CONFIG))"
	@echo "  make clean              - Remove build artifacts"
	@echo "  make distclean          - Remove build dir and CMake cache"
	@echo "  make format             - clang-format all sources (optional)"
	@echo ""
	@echo "Vars you can override:"
	@echo "  CONFIG={Debug|Release}  HOST=<ip> PORT=<num> ARGS=\"...\" TEST=<regex>"
	@echo "Examples:"
	@echo "  make release run HOST=0.0.0.0 PORT=9000"
	@echo "  make test-one TEST=health"
	@echo "  make test-api CONFIG=Debug"
	@echo ""

# ---------- Configure & Build ----------
configure:
	@mkdir -p $(BUILD_DIR)
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)

build: configure
	@cmake --build $(BUILD_DIR) -j

# Build tests explicitly (alias of build; kept for readability in CI/scripts)
build-tests: build

# Convenience configs
debug:
	@$(MAKE) build CONFIG=Debug

release:
	@$(MAKE) build CONFIG=Release

# ---------- Run ----------
# Rebuild (if necessary) then run the server with env vars
run: build
	@echo "==> Running $(TARGET) (CONFIG=$(CONFIG)) on $(HOST):$(PORT)"
	@HOST=$(HOST) PORT=$(PORT) $(BUILD_DIR)/$(TARGET) $(ARGS)

# ---------- Tests ----------
# Run all tests
test: build-tests
	@ctest --test-dir $(BUILD_DIR) $(CTEST_FLAGS)

# Verbose ctest (shows full command lines)
test-verbose: build-tests
	@ctest --test-dir $(BUILD_DIR) -VV $(CTEST_FLAGS)

# List tests without running
test-list: build-tests
	@ctest --test-dir $(BUILD_DIR) -N

# Run tests matching a regex: make test-one TEST=health
test-one: build-tests
	@if [ -z "$(TEST)" ]; then echo "Usage: make test-one TEST=<regex>"; exit 2; fi
	@ctest --test-dir $(BUILD_DIR) -R "$(TEST)" $(CTEST_FLAGS)

# Common subsets (by regex). Adjust to your test names if needed.
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

# ---------- Extras ----------
format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@find $(FORMAT_PATHS) -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.cc" \) -print0 | xargs -0 clang-format -i

# ---------- Linting / Formatting ----------
SHELL := /bin/bash
LLVM_BIN ?= /opt/homebrew/opt/llvm/bin
CLANG_TIDY ?= $(LLVM_BIN)/clang-tidy
CLANG_FORMAT ?= clang-format

FORMAT_PATHS := src include tests

format-check:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { echo "clang-format not found. brew install clang-format"; exit 1; }
	@echo "==> Checking formatting..."
	@find $(FORMAT_PATHS) -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.hh' -o -name '*.cc' -o -name '*.cpp' \) -print0 | \
	  xargs -0 $(CLANG_FORMAT) -n --Werror --style=file

lint: build
	@echo "==> Running clang-tidy (no fixes)..."
	@PATH="$(LLVM_BIN):$$PATH"; \
	cores=$$(sysctl -n hw.ncpu 2>/dev/null || nproc || echo 4); \
	find $(FORMAT_PATHS) -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0 | \
	  xargs -0 -n1 -P $$cores "$(CLANG_TIDY)" -p "$(BUILD_DIR)" --quiet

lint-fix: build
	@echo "==> Running clang-tidy with -fix..."
	@PATH="$(LLVM_BIN):$$PATH"; \
	cores=$$(sysctl -n hw.ncpu 2>/dev/null || nproc || echo 4); \
	find $(FORMAT_PATHS) -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0 | \
	  xargs -0 -n1 -P $$cores "$(CLANG_TIDY)" -p "$(BUILD_DIR)" -fix --format-style=file --quiet || true
	@$(MAKE) format