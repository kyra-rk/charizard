#include <gtest/gtest.h>
#include "storage.hpp"

#include <chrono>
#include <thread>
#include <cmath>
#include <iostream>

using namespace std::chrono;

struct StorageTest : public ::testing::Test
{
protected:
    InMemoryStore store;

    // Return current epoch seconds (same approach as InMemoryStore)
    std::int64_t now_seconds() const
    {
        using clock = std::chrono::system_clock;
        return duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count();
    }

    TransitEvent make_event_at(const std::string& user,
                            const std::string& mode,
                            double distance_km,
                            std::int64_t ts_absolute) const
    {
        TransitEvent ev;
        ev.user_id = user;
        ev.mode = mode;
        ev.distance_km = distance_km;
        ev.ts = ts_absolute;
        return ev;
    }

    TransitEvent make_event_offset(const std::string& user,
                            const std::string& mode,
                            double distance_km,
                            std::int64_t ts_offset_seconds = 0) const
    {
        return make_event_at(user, mode, distance_km, now_seconds() + ts_offset_seconds);
    }

    static double kg_for(const std::string& mode, double km)
    {
        return emission_factor_for(mode) * km;
    }

    static constexpr double EPS = 1e-9;

    // --- Setup and teardown helpers ----------------------------------------
    void SetUp() override {
        std::cout << "[SetUp] Creating fresh InMemoryStore before test..." << std::endl;
    }

    void TearDown() override {
        std::cout << "[TearDown] Cleaning up after test..." << std::endl;
        // In a real project, you could clear caches or free resources here
    }
};

// --- Boundary tests: week/month windows ------------------------------------
TEST_F(StorageTest, Summarize_BoundaryWeekMonth)
{
    const std::int64_t now = now_seconds();
    const std::int64_t WEEK = 7 * 24 * 3600;
    const std::int64_t MONTH = 30 * 24 * 3600;

    auto e_week_in  = make_event_at("u_bound", "car", 10.0, now - WEEK + 1);
    auto e_week_out = make_event_at("u_bound", "bus", 5.0, now - WEEK - 1);
    auto e_month_in = make_event_at("u_bound", "taxi", 8.0, now - MONTH + 1);
    auto e_month_out= make_event_at("u_bound", "train", 3.0, now - MONTH - 1);

    store.add_event(e_week_in);
    store.add_event(e_week_out);
    store.add_event(e_month_in);
    store.add_event(e_month_out);

    auto s = store.summarize("u_bound");

    double expect_lifetime =
        kg_for("car", 10.0) + kg_for("bus", 5.0) + kg_for("taxi", 8.0) + kg_for("train", 3.0);
    EXPECT_NEAR(s.lifetime_kg_co2, expect_lifetime, EPS);

    double expect_week = kg_for("car", 10.0);
    EXPECT_NEAR(s.week_kg_co2, expect_week, EPS);

    double expect_month = kg_for("car", 10.0) + kg_for("taxi", 8.0);
    EXPECT_NEAR(s.month_kg_co2, expect_month, EPS);
}

// --- Cache invalidation: ensure summarize reflects newly added events --------
TEST_F(StorageTest, Summarize_CacheInvalidation)
{
    auto e1 = make_event_offset("cache_user", "train", 20.0, -3600);
    store.add_event(e1);

    auto s1 = store.summarize("cache_user");
    auto s1_again = store.summarize("cache_user");
    EXPECT_NEAR(s1.lifetime_kg_co2, s1_again.lifetime_kg_co2, EPS);

    auto e2 = make_event_offset("cache_user", "car", 5.0, -100);
    store.add_event(e2);

    auto s2 = store.summarize("cache_user");
    EXPECT_GT(s2.lifetime_kg_co2, s1.lifetime_kg_co2);
    EXPECT_NEAR(s2.lifetime_kg_co2, s1.lifetime_kg_co2 + kg_for("car", 5.0), 1e-7);
}

// --- add_event / get_events: ordering and per-user separation ----------------
TEST_F(StorageTest, AddEvent_GetEvents_OrderAndSeparation)
{
    auto a1 = make_event_offset("userA", "car", 7.5, -500);
    auto a2 = make_event_offset("userA", "bus", 2.0, -400);
    auto b1 = make_event_offset("userB", "bike", 1.2, -300);

    store.add_event(a1);
    store.add_event(a2);
    store.add_event(b1);

    auto gotA = store.get_events("userA");
    auto gotB = store.get_events("userB");
    EXPECT_EQ(gotA.size(), 2u);
    EXPECT_EQ(gotB.size(), 1u);

    EXPECT_EQ(gotA[0].mode, "car");
    EXPECT_EQ(gotA[1].mode, "bus");
}

// --- API key hashing and checking -------------------------------------------
TEST_F(StorageTest, ApiKey_SetCheck_IsolatedPerUser)
{
    store.set_api_key("alice", "secret-alpha", "appA");
    store.set_api_key("bob",   "secret-beta",  "appB");

    EXPECT_TRUE(store.check_api_key("alice", "secret-alpha"));
    EXPECT_FALSE(store.check_api_key("alice", "secret-beta"));
    EXPECT_TRUE(store.check_api_key("bob", "secret-beta"));
    EXPECT_FALSE(store.check_api_key("bob", "secret-alpha"));

    store.set_api_key("alice", "new-secret");
    EXPECT_FALSE(store.check_api_key("alice", "secret-alpha"));
    EXPECT_TRUE(store.check_api_key("alice", "new-secret"));
}

// --- global_average_weekly behavior ----------------------------------------
TEST_F(StorageTest, GlobalAverageWeekly_Basic)
{
    EXPECT_DOUBLE_EQ(store.global_average_weekly(), 0.0);

    auto a1 = make_event_offset("uA", "car", 10.0, -3600);
    auto a2 = make_event_offset("uA", "bus", 5.0, -3600);
    store.add_event(a1);
    store.add_event(a2);

    double uA_week = kg_for("car", 10.0) + kg_for("bus", 5.0);
    EXPECT_NEAR(store.global_average_weekly(), uA_week, 1e-9);

    auto b1 = make_event_offset("uB", "taxi", 8.0, -3600);
    store.add_event(b1);
    double uB_week = kg_for("taxi", 8.0);

    double expected_avg = (uA_week + uB_week) / 2.0;
    EXPECT_NEAR(store.global_average_weekly(), expected_avg, 1e-9);
}
