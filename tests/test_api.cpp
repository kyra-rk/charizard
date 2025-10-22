#include <gtest/gtest.h>
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "api.hpp"
#include "storage.hpp"

#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>

using nlohmann::json;

// Helper: run server in background and stop at scope end
struct TestServer
{
    httplib::Server svr;
    std::thread     th;
    int             port = 18080; // fixed test port; change if needed

    TestServer(IStore& store)
    {
        configure_routes(svr, store);
        th = std::thread(
            [this]
            {
                svr.listen("127.0.0.1", port); // blocks until stop()
            });
        // wait a moment for the server to bind
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    ~TestServer()
    {
        svr.stop();
        if (th.joinable())
            th.join();
    }
};

// TO-DO: place integration tests in integration/ directory
TEST(Api, Health)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/health");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    auto j = json::parse(res->body);
    EXPECT_TRUE(j["ok"].get<bool>());
}