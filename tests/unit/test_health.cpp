#include <gtest/gtest.h>
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define CPPHTTPLIB_THREAD_POOL_COUNT 2
#include "api.hpp"
#include "storage.hpp"

#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>

using nlohmann::json;

class HealthServer
{
  public:
    HealthServer()
    {
        // Use the member `store` so the routes capture a reference that stays alive
        configure_routes(svr_, store_);
        // run server on a background thread
        thread_ = std::thread([this] { svr_.listen("127.0.0.1", port_); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // give server time to start
    }

    ~HealthServer()
    {
        svr_.stop();
        if (thread_.joinable())
            thread_.join();
    }

    // Rule-of-five clarity
    HealthServer(const HealthServer&)            = delete;
    HealthServer& operator=(const HealthServer&) = delete;
    HealthServer(HealthServer&&)                 = delete;
    HealthServer& operator=(HealthServer&&)      = delete;

    int get_port() const
    {
        return port_;
    }

  private:
    InMemoryStore   store_;
    httplib::Server svr_;
    std::thread     thread_;
    int             port_ = 18080;
};

// TO-DO: place unit tests in unit/ directory
TEST(CharizardAPI, HealthEndpoint)
{
    HealthServer const server;

    httplib::Client cli("127.0.0.1", server.get_port());
    auto            res = cli.Get("/health");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_TRUE(j.contains("ok"));
    EXPECT_TRUE(j["ok"].get<bool>());
    EXPECT_EQ(j["service"], "charizard");
}
