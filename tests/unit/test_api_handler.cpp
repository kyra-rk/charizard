#include <gtest/gtest.h>
#include <gmock/gmock.h>

#define CPPHTTPLIB_THREAD_POOL_COUNT 2
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "api.hpp"
#include "storage.hpp"

#include <thread>
#include <chrono>
#include <stdexcept>
#include <sstream>
#include <random>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;
using ::testing::DoAll;
using ::testing::SaveArg;
using nlohmann::json;

// Helper: find a free ephemeral port by binding to port 0 and reading getsockname.
static int find_free_port()
{
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        throw std::runtime_error("socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        throw std::runtime_error("bind() failed");
    }

    if (listen(sock, 1) < 0) {
        ::close(sock);
        throw std::runtime_error("listen() failed");
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        ::close(sock);
        throw std::runtime_error("getsockname() failed");
    }

    int port = ntohs(addr.sin_port);
    ::close(sock);
    return port;
}

// Mock implementation of IStore for API handler testing
struct MockStore : public IStore {
    MOCK_METHOD(void, set_api_key, (const std::string&, const std::string&, const std::string&), (override));
    MOCK_METHOD(bool, check_api_key, (const std::string&, const std::string&), (const, override));
    MOCK_METHOD(void, add_event, (const TransitEvent&), (override));
    MOCK_METHOD(std::vector<TransitEvent>, get_events, (const std::string&), (const, override));
    MOCK_METHOD(FootprintSummary, summarize, (const std::string&), (override));
    MOCK_METHOD(double, global_average_weekly, (), (override));
};

// InlineTestServer: wraps httplib::Server and runs it in a background thread.
struct InlineTestServer {
    httplib::Server svr;
    std::thread th;
    int port = 0;

    InlineTestServer(IStore& store)
    {
        port = find_free_port();
        configure_routes(svr, store);

        th = std::thread([this] {
            svr.listen("127.0.0.1", port);
        });

        httplib::Client cli("127.0.0.1", port);
        const auto start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() - start < timeout) {
            auto res = cli.Get("/health");
            if (res && res->status == 200) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        svr.stop();
        if (th.joinable()) th.join();
        throw std::runtime_error("server failed to start");
    }

    ~InlineTestServer() {
        svr.stop();
        if (th.joinable()) th.join();
    }
};

// ---------------------------------------------------------------------------
// Fixture for API handler tests
// ---------------------------------------------------------------------------
struct ApiHandlerTest : public ::testing::Test {
protected:
    NiceMock<MockStore> mock;
    InlineTestServer* server = nullptr;
    httplib::Client* client = nullptr;

    void SetUp() override {
        std::cout << "[SetUp] Starting InlineTestServer..." << std::endl;
        // Start the server before each test
        server = new InlineTestServer(mock);
        client = new httplib::Client("127.0.0.1", server->port);
    }

    void TearDown() override {
        std::cout << "[TearDown] Stopping InlineTestServer and cleaning up..." << std::endl;
        delete client;
        delete server;
        client = nullptr;
        server = nullptr;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(ApiHandlerTest, Register_Valid_CallsSetApiKeyAndReturns201)
{
    std::string saved_user, saved_key;
    EXPECT_CALL(mock, set_api_key(_, _, "testApp"))
        .WillOnce(DoAll(SaveArg<0>(&saved_user), SaveArg<1>(&saved_key)));

    json body = { { "app_name", "testApp" } };
    auto res = client->Post("/users/register", body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    auto j = json::parse(res->body);
    EXPECT_EQ(j["app_name"].get<std::string>(), "testApp");
    EXPECT_EQ(saved_user, j["user_id"].get<std::string>());
    EXPECT_EQ(saved_key, j["api_key"].get<std::string>());
}

TEST_F(ApiHandlerTest, Transit_Valid_CallsAddEvent)
{
    TransitEvent captured_ev;
    EXPECT_CALL(mock, check_api_key("alice", "goodkey")).WillOnce(Return(true));
    EXPECT_CALL(mock, add_event(_)).WillOnce(SaveArg<0>(&captured_ev));

    json body = { { "mode", "car" }, { "distance_km", 12.5 }, { "ts", 1620000000 } };
    httplib::Headers headers = { { "X-API-Key", "goodkey" } };
    auto res = client->Post("/users/alice/transit", headers, body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(captured_ev.user_id, "alice");
    EXPECT_EQ(captured_ev.mode, "car");
    EXPECT_DOUBLE_EQ(captured_ev.distance_km, 12.5);
    EXPECT_EQ(captured_ev.ts, 1620000000);
}

TEST_F(ApiHandlerTest, Transit_MissingFields_Returns400_NoAddEvent)
{
    EXPECT_CALL(mock, check_api_key("alice", _)).WillOnce(Return(true));
    EXPECT_CALL(mock, add_event(_)).Times(0);

    json body = { { "mode", "bus" } };
    httplib::Headers headers = { { "X-API-Key", "any" } };
    auto res = client->Post("/users/alice/transit", headers, body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    auto j = json::parse(res->body);
    EXPECT_EQ(j["error"].get<std::string>(), "missing_fields");
}

TEST_F(ApiHandlerTest, LifetimeFootprint_HappyPath_UsesSummarize)
{
    EXPECT_CALL(mock, check_api_key("alice", _)).WillOnce(Return(true));
    FootprintSummary fake{ 42.0, 7.0, 21.0 };
    EXPECT_CALL(mock, summarize("alice")).WillOnce(Return(fake));

    httplib::Headers headers = { { "X-API-Key", "k" } };
    auto res = client->Get("/users/alice/lifetime-footprint", headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    auto j = json::parse(res->body);
    EXPECT_DOUBLE_EQ(j["lifetime_kg_co2"], fake.lifetime_kg_co2);
    EXPECT_DOUBLE_EQ(j["last_7d_kg_co2"], fake.week_kg_co2);
    EXPECT_DOUBLE_EQ(j["last_30d_kg_co2"], fake.month_kg_co2);
}

TEST_F(ApiHandlerTest, Suggestions_ReturnsAdvice_WhenHighWeek)
{
    EXPECT_CALL(mock, check_api_key("bob", _)).WillOnce(Return(true));
    FootprintSummary big{ 100.0, 25.0, 40.0 };
    EXPECT_CALL(mock, summarize("bob")).WillOnce(Return(big));

    httplib::Headers headers = { { "X-API-Key", "key" } };
    auto res = client->Get("/users/bob/suggestions", headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    auto j = json::parse(res->body);
    EXPECT_TRUE(j["suggestions"].is_array());
    EXPECT_GT(j["suggestions"].size(), 1u);
}

TEST_F(ApiHandlerTest, Analytics_ReturnsComparison)
{
    EXPECT_CALL(mock, check_api_key("carol", _)).WillOnce(Return(true));
    FootprintSummary s{ 10.0, 5.0, 12.0 };
    EXPECT_CALL(mock, summarize("carol")).WillOnce(Return(s));
    EXPECT_CALL(mock, global_average_weekly()).WillOnce(Return(3.0));

    httplib::Headers headers = { { "X-API-Key", "k" } };
    auto res = client->Get("/users/carol/analytics", headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    auto j = json::parse(res->body);
    EXPECT_DOUBLE_EQ(j["this_week_kg_co2"], s.week_kg_co2);
    EXPECT_DOUBLE_EQ(j["peer_week_avg_kg_co2"], 3.0);
    EXPECT_EQ(j["above_peer_avg"], true);
}

TEST_F(ApiHandlerTest, HealthAndRootEndpoints_Basic)
{
    auto h = client->Get("/health");
    ASSERT_TRUE(h != nullptr);
    EXPECT_EQ(h->status, 200);
    auto jh = json::parse(h->body);
    EXPECT_TRUE(jh["ok"]);

    auto r = client->Get("/");
    ASSERT_TRUE(r != nullptr);
    EXPECT_EQ(r->status, 200);
    auto jr = json::parse(r->body);
    EXPECT_TRUE(jr.contains("service"));
    EXPECT_TRUE(jr.contains("endpoints"));
}
