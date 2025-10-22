#include <gtest/gtest.h>
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "api.hpp"
#include "storage.hpp"

#include <chrono>
#include <httplib.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using nlohmann::json;

// Helper to find a free ephemeral port
static int find_free_port()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0; // Let OS assign a port

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        throw std::runtime_error("Failed to bind socket");
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr*)&addr, &addr_len) < 0)
    {
        close(sock);
        throw std::runtime_error("Failed to get socket name");
    }

    int port = ntohs(addr.sin_port);
    close(sock);
    return port;
}

// Helper: run server in background and stop at scope end
struct TestServer
{
    httplib::Server svr;
    std::thread     th;
    int             port;

    TestServer(IStore& store) : port(find_free_port())
    {
        configure_routes(svr, store);
        th = std::thread(
            [this]
            {
                svr.listen("127.0.0.1", port); // blocks until stop()
            });

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