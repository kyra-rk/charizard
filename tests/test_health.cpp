#include <gtest/gtest.h>
#define CPPHTTPLIB_THREAD_POOL_COUNT 2
#include "api.hpp"
#include "storage.hpp"

#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>

using nlohmann::json;

// Helper to start/stop the server for the test
class HealthServer
{
  public:
    InMemoryStore   store;
    httplib::Server svr;
    std::thread     thread;
    int             port = 18080; // change if needed

    HealthServer()
    {
        // Use the member `store` (do NOT redeclare a new local variable)
        configure_routes(svr, this->store);

        // run server on a background thread
        thread = std::thread([this] { svr.listen("127.0.0.1", port); });

        // Wait/poll until server responds or timeout to avoid flaky sleeps
        httplib::Client cli("127.0.0.1", port);
        const auto start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() - start < timeout) {
            auto res = cli.Get("/health");
            if (res && res->status == 200) {
                return; // ready
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // If we get here the server didn't start in time -> stop and fail the test setup
        svr.stop();
        if (thread.joinable()) thread.join();
        throw std::runtime_error("HealthServer failed to start within timeout");
    }

    ~HealthServer()
    {
        svr.stop();
        if (thread.joinable())
            thread.join();
    }
};

// TO-DO: place unit tests in unit/ directory
TEST(CharizardAPI, HealthEndpoint)
{
    HealthServer server;

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/health");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    // Content-Type check
    auto ct = res->get_header_value("Content-Type");
    EXPECT_TRUE(ct.find("application/json") != std::string::npos);

    auto j = json::parse(res->body);
    EXPECT_TRUE(j.contains("ok"));
    EXPECT_TRUE(j["ok"].get<bool>());
    EXPECT_EQ(j["service"], "charizard");

    // Optional: ensure there is a numeric time field
    EXPECT_TRUE(j.contains("time"));
    EXPECT_TRUE(j["time"].is_number());
}