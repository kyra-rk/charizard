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
        InMemoryStore store;
        configure_routes(svr, store);
        // run server on a background thread
        thread = std::thread([this] { svr.listen("127.0.0.1", port); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // give server time to start
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

    auto j = json::parse(res->body);
    EXPECT_TRUE(j.contains("ok"));
    EXPECT_TRUE(j["ok"].get<bool>());
    EXPECT_EQ(j["service"], "charizard");
}