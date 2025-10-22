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
        configure_routes(svr, this->store);
        // run server on a background thread
        thread = std::thread([this] { svr.listen("127.0.0.1", port); });

        // Poll for server readiness instead of fixed sleep
        httplib::Client cli("127.0.0.1", port);
        auto            start = std::chrono::steady_clock::now();
        bool            ready = false;
        while (!ready)
        {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(5))
            {
                throw std::runtime_error("Server failed to start within 5 seconds");
            }
            auto res = cli.Get("/health");
            if (res && res->status == 200)
            {
                ready = true;
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
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

    // Check Content-Type header
    EXPECT_TRUE(res->headers.count("Content-Type") > 0);
    EXPECT_TRUE(res->headers.find("Content-Type")->second.find("application/json") != std::string::npos);

    auto j = json::parse(res->body);
    EXPECT_TRUE(j.contains("ok"));
    EXPECT_TRUE(j["ok"].get<bool>());
    EXPECT_EQ(j["service"], "charizard");

    // Check for "time" field and that it's numeric
    EXPECT_TRUE(j.contains("time"));
    EXPECT_TRUE(j["time"].is_number());
}