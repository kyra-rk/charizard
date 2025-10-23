#include <gtest/gtest.h>
#include "storage.hpp"
#include <string>
#include <iostream>

// ------------------------------------------------------------
// Fixture for emission-factor tests
// ------------------------------------------------------------
struct EmissionFactorsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // This runs before each emission-factor test
        std::cout << "[SetUp] Preparing emission factor test environment..." << std::endl;
    }

    void TearDown() override {
        // This runs after each emission-factor test
        std::cout << "[TearDown] Cleaning up emission factor test environment..." << std::endl;
    }
};

TEST_F(EmissionFactorsTest, KnownModes) {
    EXPECT_DOUBLE_EQ(emission_factor_for("taxi"), 0.18);
    EXPECT_DOUBLE_EQ(emission_factor_for("car"), 0.18);
    EXPECT_DOUBLE_EQ(emission_factor_for("bus"), 0.08);
    EXPECT_DOUBLE_EQ(emission_factor_for("subway"), 0.04);
    EXPECT_DOUBLE_EQ(emission_factor_for("train"), 0.04);
    EXPECT_DOUBLE_EQ(emission_factor_for("bike"), 0.0);
    EXPECT_DOUBLE_EQ(emission_factor_for("walk"), 0.0);
}

TEST_F(EmissionFactorsTest, UnknownModeFallsBack) {
    EXPECT_DOUBLE_EQ(emission_factor_for("unicorn"), 0.1);
    EXPECT_DOUBLE_EQ(emission_factor_for(""), 0.1);
}

TEST_F(EmissionFactorsTest, CaseSensitivity) {
    EXPECT_DOUBLE_EQ(emission_factor_for("Car"), 0.1);
    EXPECT_DOUBLE_EQ(emission_factor_for("TAXI"), 0.1);
}

// ------------------------------------------------------------
// Fixture for API key hashing tests
// ------------------------------------------------------------
struct ApiKeyHashingTest : public ::testing::Test {
protected:
    InMemoryStore store;

    void SetUp() override {
        // Runs before each test
        std::cout << "[SetUp] Creating fresh InMemoryStore instance..." << std::endl;
        store = InMemoryStore();  // reset store to ensure clean state
    }

    void TearDown() override {
        // Runs after each test
        std::cout << "[TearDown] Clearing InMemoryStore..." << std::endl;
        // In a real project, we might release resources or clear caches here
    }
};

TEST_F(ApiKeyHashingTest, DeterministicForSameKey) {
    store.set_api_key("alice", "s3cr3t-key", "appA");

    EXPECT_TRUE(store.check_api_key("alice", "s3cr3t-key"));
    EXPECT_TRUE(store.check_api_key("alice", "s3cr3t-key"));
    EXPECT_FALSE(store.check_api_key("alice", "other-key"));
}

TEST_F(ApiKeyHashingTest, IsolatedPerUser) {
    store.set_api_key("alice", "alpha-key");
    store.set_api_key("bob", "beta-key");

    EXPECT_TRUE(store.check_api_key("alice", "alpha-key"));
    EXPECT_FALSE(store.check_api_key("alice", "beta-key"));
    EXPECT_TRUE(store.check_api_key("bob", "beta-key"));
    EXPECT_FALSE(store.check_api_key("bob", "alpha-key"));
}

TEST_F(ApiKeyHashingTest, EmptyKeySupported) {
    store.set_api_key("empty", "");

    EXPECT_TRUE(store.check_api_key("empty", ""));
    EXPECT_FALSE(store.check_api_key("empty", "non-empty"));
}
