#include <gtest/gtest.h>
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "api.hpp"
#include "storage.hpp"

#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <stdexcept>

// POSIX socket headers used to pick an ephemeral free port.
// This keeps tests from colliding on a fixed port in CI / parallel runs.
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using nlohmann::json;

// Find a free ephemeral port by binding to port 0 on loopback and reading the assigned port.
// Note: there is a small race between closing this socket and the server binding to the port,
// but this approach is commonly used in tests and reduces fixed-port collisions.
static int find_free_port()
{
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        throw std::runtime_error("socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    addr.sin_port = 0;                              // ask OS for a free port

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

// Helper: run server in background and stop at scope end
struct TestServer
{
    httplib::Server svr;
    std::thread     th;
    int             port = 0; // will be assigned a free ephemeral port

    TestServer(IStore& store)
    {
        // pick an ephemeral port to avoid collisions in CI / parallel runs
        port = find_free_port();

        configure_routes(svr, store);
        th = std::thread([this] {
            // blocks until stop() is called
            svr.listen("127.0.0.1", port);
        });

        // readiness poll: wait until /health responds or timeout
        httplib::Client cli("127.0.0.1", port);
        const auto start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() - start < timeout) {
            auto res = cli.Get("/health");
            if (res && res->status == 200) {
                return; // server is ready
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // startup failed: stop server and join thread to avoid leaking threads in the test harness
        svr.stop();
        if (th.joinable())
            th.join();
        throw std::runtime_error("TestServer failed to start within timeout");
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