#include "transit_logic.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

TEST(TransitLogic, MissingFieldsThrows)
{
    json j = json::object();
    EXPECT_THROW(make_transit_event_from_json("alice", j, 123), std::runtime_error);
}

TEST(TransitLogic, EmptyUserIdThrows)
{
    json j = { { "mode", "walk" }, { "distance_km", 1.0 } };
    EXPECT_THROW(make_transit_event_from_json("", j, 123), std::runtime_error);
}

TEST(TransitLogic, NegativeDistanceThrows)
{
    json j = { { "mode", "walk" }, { "distance_km", -1.0 } };
    EXPECT_THROW(make_transit_event_from_json("alice", j, 123), std::runtime_error);
}

TEST(TransitLogic, InvalidModeThrows)
{
    json j = { { "mode", "rocket" }, { "distance_km", 1.0 } };
    EXPECT_THROW(make_transit_event_from_json("alice", j, 123), std::runtime_error);
}

TEST(TransitLogic, ValidInputReturnsEvent)
{
    json j  = { { "mode", "bike" }, { "distance_km", 2.5 } };
    auto ev = make_transit_event_from_json("alice", j, 1600000000);
    EXPECT_EQ(ev.user_id, "alice");
    EXPECT_EQ(ev.mode, "bike");
    EXPECT_DOUBLE_EQ(ev.distance_km, 2.5);
    EXPECT_EQ(ev.ts, 1600000000);
}

TEST(TransitLogic, UsesProvidedNowEpochWhenTsMissing)
{
    json j = { { "mode", "walk" }, { "distance_km", 1.0 } };
    // when caller supplies now_epoch, and body lacks ts, the helper should use that value
    auto ev = make_transit_event_from_json("bob", j, 1234567890);
    EXPECT_EQ(ev.user_id, "bob");
    EXPECT_EQ(ev.ts, 1234567890);
}

TEST(TransitLogic, BodyProvidedTsIsRespected)
{
    json j  = { { "mode", "walk" }, { "distance_km", 1.0 }, { "ts", 4242424242 } };
    auto ev = make_transit_event_from_json("carol", j, 1);
    EXPECT_EQ(ev.ts, 4242424242);
}

TEST(TransitLogic, AcceptedModesDoNotThrow)
{
    const std::vector<std::string> modes = { "taxi", "car", "bus", "subway", "train", "bike", "walk" };
    for (const auto& m : modes)
    {
        json j = { { "mode", m }, { "distance_km", 0.1 } };
        EXPECT_NO_THROW(make_transit_event_from_json("dan", j, 0)) << "mode=" << m;
    }
}

TEST(TransitLogic, ZeroDistanceIsAllowed)
{
    json j = { { "mode", "walk" }, { "distance_km", 0.0 } };
    EXPECT_NO_THROW(make_transit_event_from_json("ellen", j, 0));
}

TEST(TransitLogic, LargeDistanceIsAllowed)
{
    json j  = { { "mode", "car" }, { "distance_km", 1e6 } };
    auto ev = make_transit_event_from_json("frank", j, 0);
    EXPECT_DOUBLE_EQ(ev.distance_km, 1e6);
}