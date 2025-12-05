#include <gtest/gtest.h>
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "api.hpp"
#include "storage.hpp"

#include <chrono>
#include <cstdlib>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <tuple>

using nlohmann::json;

static bool is_hex(const std::string& s)
{
    static const std::regex re("^[0-9a-fA-F]+$");
    return std::regex_match(s, re);
}

static httplib::Headers demo_auth_headers()
{
    return { { "X-API-Key", "secret-demo-key" }, { "Content-Type", "application/json" } };
}

// Small helper to ingest a transit event via the POST endpoint
static void post_transit(httplib::Client& cli, double distance_km, const std::string& mode,
                         std::int64_t ts /* epoch seconds */) // NOLINT(bugprone-easily-swappable-parameters)
{
    json const body = { { "mode", mode }, { "distance_km", distance_km }, { "ts", ts } };
    auto       res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");
    ASSERT_TRUE(res != nullptr) << "Transit POST returned null";
    ASSERT_EQ(res->status, 201) << "Transit POST failed, body: " << (res ? res->body : "");
}

static void set_admin_key(const char* v)
{
#ifdef _WIN32
    _putenv_s("ADMIN_API_KEY", v);
#else
    setenv("ADMIN_API_KEY", v, 1);
#endif
}

static httplib::Headers admin_auth_headers()
{
    const char*       k     = std::getenv("ADMIN_API_KEY");
    std::string const token = (k != nullptr) ? k : "";
    return { { "Authorization", "Bearer " + token } };
}

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

TEST(ApiHealth, HealthGet)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/health");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    auto j = json::parse(res->body);
    EXPECT_TRUE(j["ok"].get<bool>());
}

// Test helper: create a server, post two events for 'demo', and return the parsed array + timestamps
static std::tuple<json, std::int64_t, std::int64_t> setup_demo_with_two_events()
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    const auto t1 = static_cast<std::int64_t>(std::time(nullptr));
    const auto t2 = t1 + 5;

    post_transit(cli, 100.0, "car", t1);
    post_transit(cli, 100.0, "bike", t2);

    auto res = cli.Get("/admin/clients/demo/data", admin_auth_headers());
    if (!res)
        return { json::array(), t1, t2 };
    auto arr = json::parse(res->body);
    return { arr, t1, t2 };
}

/* =================================================== */
/* --------- POST /users/register Testcases ---------- */
/* =================================================== */

TEST(ApiRegister, Register_InvalidJSON_EmptyBody)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Post("/users/register"); // literally no body

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "invalid_json");
}

TEST(ApiRegister, Register_InvalidJSON_Garbage)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Post("/users/register", "not-json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "invalid_json");
}

TEST(ApiRegister, Register_MissingAppName_KeyAbsent)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    json const      req = json::object(); // {}

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "missing_app_name");
}

TEST(ApiRegister, Register_MissingAppName_WrongType)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    json const      req = { { "app_name", 123 } }; // not a string

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "missing_app_name");
}

// Form-encoded body should fail JSON parse
TEST(ApiRegister, Register_InvalidJSON_FormEncoded)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client        cli("127.0.0.1", server.port);
    httplib::Headers const headers = { { "Content-Type", "application/x-www-form-urlencoded" } };
    auto res = cli.Post("/users/register", headers, "app_name=myapp", "application/x-www-form-urlencoded");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "invalid_json");
}

// ---- Success cases ----

TEST(ApiRegister, Register_Success_Minimal)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    const std::string app_name = "myapp";
    json const        req      = { { "app_name", app_name } };

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json j = json::parse(res->body);

    // Required fields present & types
    ASSERT_TRUE(j.contains("user_id"));
    ASSERT_TRUE(j.contains("api_key"));
    ASSERT_TRUE(j.contains("app_name"));

    ASSERT_TRUE(j["user_id"].is_string());
    ASSERT_TRUE(j["api_key"].is_string());
    ASSERT_TRUE(j["app_name"].is_string());

    // Values
    EXPECT_EQ(j["app_name"].get<std::string>(), app_name);

    // Format checks: user_id starts with "u_" and has 8 hex chars after → total len 10
    const auto uid = j["user_id"].get<std::string>();
    ASSERT_GE(uid.size(), 3U);
    EXPECT_TRUE(uid.rfind("u_", 0) == 0); // prefix
    EXPECT_EQ(uid.size(), 10U);           // "u_" + 8 hex
    EXPECT_TRUE(is_hex(uid.substr(2)));

    // api_key is 32 hex chars
    const auto key = j["api_key"].get<std::string>();
    EXPECT_EQ(key.size(), 32U);
    EXPECT_TRUE(is_hex(key));
}

TEST(ApiRegister, Register_Success_IgnoresExtraFields)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    json const req = { { "app_name", "widgetizer" }, { "noise", "ignored" }, { "version", 3 } };

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("app_name", ""), "widgetizer");
    EXPECT_TRUE(j.contains("user_id"));
    EXPECT_TRUE(j.contains("api_key"));
}

// Content-Type not JSON but body IS valid JSON → still OK (server just parses req.body)
TEST(ApiRegister, Register_Success_TextPlainBody)
{
    InMemoryStore    mem;
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    json const             req     = { { "app_name", "plain" } };
    httplib::Headers const headers = { { "Content-Type", "text/plain" } };

    auto res = cli.Post("/users/register", headers, req.dump(), "text/plain");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("app_name", ""), "plain");
}

TEST(ApiRegister, Register_Success_EmptyAppNameAllowed)
{
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    json const body = { { "app_name", "" } }; // empty string is still a string
    auto       res  = cli.Post("/users/register", body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("app_name", "xxx"), "");
}

/* ============================================================= */
/* ---------- POST /users/{user_id}/transit Testcases ---------- */
/* ============================================================= */

/* ---------------- Path / Routing ---------------- */

TEST(ApiTransit, BadPath_NoUserInUrl)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users//transit", demo_auth_headers(), R"({"mode":"bus","distance_km":1})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiTransit, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit/extra", demo_auth_headers(), R"({"mode":"bus","distance_km":1})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

/* ---------------- Auth (explicit) ---------------- */

TEST(ApiTransit, Unauthorized_NoHeader)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", R"({"mode":"walk","distance_km":0.5})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(ApiTransit, Unauthorized_WrongKey)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const wrong = { { "X-API-Key", "not-the-key" }, { "Content-Type", "application/json" } };
    auto                   res =
        cli.Post("/users/demo/transit", wrong, R"({"mode":"walk","distance_km":0.5})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---------------- JSON parsing / validation ---------------- */

TEST(ApiTransit, InvalidJson_EmptyBody)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), "", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid_json");
}

TEST(ApiTransit, InvalidJson_Garbage)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), "not-json", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid_json");
}

TEST(ApiTransit, MissingFields_ModeAbsent)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res =
        cli.Post("/users/demo/transit", demo_auth_headers(), R"({"distance_km": 3.4})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "missing_fields");
}

TEST(ApiTransit, MissingFields_DistanceAbsent)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"bus"})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "missing_fields");
}

/* If you add type guards before get<>() (recommended), these should be 400.
   Without guards, get<>() will throw and likely crash the handler. */
TEST(ApiTransit, WrongTypes_ModeNotString)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":123,"distance_km":1.0})",
                        "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_NE(res->status, 201); // expect 400 if guarded
}

TEST(ApiTransit, WrongTypes_DistanceNotNumber)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"walk","distance_km":"far"})",
                        "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_NE(res->status, 201); // expect 400 if guarded
}

/* ---------------- Success paths ---------------- */

TEST(ApiTransit, Success_Minimal_UsesServerTs)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    json const body = { { "mode", "subway" }, { "distance_km", 7.4 } };
    auto       res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status", ""), "ok");
}

TEST(ApiTransit, Success_WithExplicitTs)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    json const body = { { "mode", "bike" },
                        { "distance_km", 12.0 },
                        { "ts", static_cast<std::int64_t>(1730000000) } };
    auto       res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status", ""), "ok");
}

TEST(ApiTransit, Success_IntegerDistanceAccepted)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // integer literal should parse; get<double>() will coerce fine
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"car","distance_km":5})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status", ""), "ok");
}

/* Content-Type variations: server parses req.body regardless of header */
TEST(ApiTransit, Failure_TextPlainWithJsonBody)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const hdrs = { { "X-API-Key", "secret-demo-key" }, { "Content-Type", "text/plain" } };
    json const             body = { { "mode", "tram" }, { "distance_km", 3.1 } };
    auto                   res  = cli.Post("/users/demo/transit", hdrs, body.dump(), "text/plain");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid mode");
}

TEST(ApiTransit, InvalidJson_FormEncoded)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const hdrs = { { "X-API-Key", "secret-demo-key" },
                                    { "Content-Type", "application/x-www-form-urlencoded" } };
    auto                   res  = cli.Post("/users/demo/transit", hdrs, "mode=bus&distance_km=2.5",
                                           "application/x-www-form-urlencoded");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid_json");
}

/* ======================================================================== */
/* ---------- POST /users/{user_id}/lifetime-footprint Testcases ---------- */
/* ======================================================================== */

// -------------- Path / routing ----------------

TEST(ApiFootprint, BadPath_NoUserInUrl)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Note: using auth headers even for bad path to isolate routing
    auto res = cli.Get("/users//lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiFootprint, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Get("/users/demo/lifetime-footprint/extra", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

// -------------- Auth ----------------

TEST(ApiFootprint, Unauthorized_NoHeader)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Get("/users/demo/lifetime-footprint");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(ApiFootprint, Unauthorized_WrongKey)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const wrong = { { "X-API-Key", "not-the-key" } };
    auto                   res   = cli.Get("/users/demo/lifetime-footprint", wrong);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

// -------------- Success: empty store ----------------

TEST(ApiFootprint, Success_ZeroWhenNoEvents)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_EQ(j.value("user_id", ""), "demo");
    // Expect zeros when there are no events
    EXPECT_DOUBLE_EQ(j.value("lifetime_kg_co2", 123.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("last_7d_kg_co2", 123.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("last_30d_kg_co2", 123.0), 0.0);
}

// -------------- Success: with events ----------------

TEST(ApiFootprint, Success_AccumulatesAndRespectsWindows)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Create three events:
    // - 2 days ago: distance 1
    // - 10 days ago: distance 2
    // - 40 days ago: distance 3
    const std::time_t now = std::time(nullptr);
    post_transit(cli, 1.0, "bus", now - static_cast<std::time_t>(2 * 24 * 3600));
    post_transit(cli, 2.0, "bus", now - static_cast<std::time_t>(10 * 24 * 3600));
    post_transit(cli, 3.0, "bus", now - static_cast<std::time_t>(40 * 24 * 3600));

    auto res = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);

    // Basic shape
    EXPECT_EQ(j.value("user_id", ""), "demo");
    ASSERT_TRUE(j.contains("lifetime_kg_co2"));
    ASSERT_TRUE(j.contains("last_7d_kg_co2"));
    ASSERT_TRUE(j.contains("last_30d_kg_co2"));

    const double life = j["lifetime_kg_co2"].get<double>();
    const double w7   = j["last_7d_kg_co2"].get<double>();
    const double w30  = j["last_30d_kg_co2"].get<double>();

    // Non-negativity
    EXPECT_GE(life, 0.0);
    EXPECT_GE(w30, 0.0);
    EXPECT_GE(w7, 0.0);

    // lifetime > 30d > 7d3
    EXPECT_GT(life, w30);
    EXPECT_GT(w30, w7);
    EXPECT_GT(w7, 0.0);
}

// -------------- Success: unaffected by other users ----------------

TEST(ApiFootprint, IgnoresOtherUsersEvents)
{
    InMemoryStore mem;
    // Register demo and another user
    mem.set_api_key("demo", "secret-demo-key");
    mem.set_api_key("u_other", "k_other");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Post an event for u_other (should NOT count for demo)
    {
        json const             body = { { "mode", "car" }, { "distance_km", 100.0 } };
        httplib::Headers const hdr  = { { "X-API-Key", "k_other" }, { "Content-Type", "application/json" } };
        auto                   res = cli.Post("/users/u_other/transit", hdr, body.dump(), "application/json");
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->status, 201);
    }

    // Demo still has zero
    auto res2 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res2 != nullptr);
    EXPECT_EQ(res2->status, 200);

    auto j = json::parse(res2->body);
    EXPECT_EQ(j.value("user_id", ""), "demo");
    EXPECT_DOUBLE_EQ(j.value("lifetime_kg_co2", 123.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("last_7d_kg_co2", 123.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("last_30d_kg_co2", 123.0), 0.0);
}

// Integration tests that exercise the TransitEvent validation checks.

TEST(ApiTransit, Validation_NegativeDistance)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"walk","distance_km":-3.5})",
                        "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "Negative value for distance_km is not allowed.");
}

TEST(ApiTransit, Validation_ZeroDistanceAllowed)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"walk","distance_km":0.0})",
                        "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201); // Should succeed
}

TEST(ApiTransit, Validation_InvalidMode)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":"spaceship","distance_km":1.0})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid mode");
}

TEST(ApiTransit, MissingFields_BothModeAndDistanceAbsent)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    json const body = { { "ts", 123456789 } }; // neither mode nor distance_km
    auto       res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    auto const j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "missing_fields");
}

// unit-style integration for empty user_id validation (constructor throws)
TEST(TransitEventUnit, Validation_EmptyUserIdThrows)
{
    try
    {
        TransitEvent const ev("", "walk", 1.0, 0);
        FAIL() << "Expected std::runtime_error to be thrown for empty user_id";
    }
    catch (const std::runtime_error& e)
    {
        std::string const msg(e.what());
        EXPECT_NE(msg.find("user_id must not be empty"), std::string::npos);
    }
    catch (...)
    {
        FAIL() << "Expected std::runtime_error";
    }
}

/* ========================================================= */
/* ---------- GET /users/{user_id}/suggestions ------------- */
/* ========================================================= */

TEST(ApiSuggestions, BadPath_NoUserInUrl)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users//suggestions", demo_auth_headers());

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiSuggestions, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users/demo/suggestions/extra", demo_auth_headers());

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

/* ---------------- Auth ---------------- */

TEST(ApiSuggestions, Unauthorized_NoHeader)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key"); // register user for realism
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users/demo/suggestions"); // no X-API-Key

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(ApiSuggestions, Unauthorized_WrongKey)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client        cli("127.0.0.1", server.port);
    httplib::Headers const wrong = { { "X-API-Key", "not-the-key" } };
    auto                   res   = cli.Get("/users/demo/suggestions", wrong);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---------------- Behavior: low vs high weekly emissions ---------------- */

TEST(ApiSuggestions, Success_LowEmissions_NoEvents)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    // No events posted --> expect "Nice work!" branch
    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users/demo/suggestions", demo_auth_headers());

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_EQ(j.value("user_id", ""), "demo");
    ASSERT_TRUE(j.contains("suggestions"));
    ASSERT_TRUE(j["suggestions"].is_array());

    const auto& arr = j["suggestions"];
    ASSERT_EQ(arr.size(), 1U);
    EXPECT_EQ(arr[0].get<std::string>(), "Nice work! Consider biking or walking for short hops.");
}

TEST(ApiSuggestions, Success_HighEmissions_AboveThresholdThisWeek)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    // Create enough "car" distance this week to push week_kg_co2 > 20.0 --> expect "Try switching..." branch
    const std::time_t now = std::time(nullptr);
    post_transit(cli, /*distance_km=*/200.0, "car", static_cast<std::int64_t>(now));

    auto res = cli.Get("/users/demo/suggestions", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_EQ(j.value("user_id", ""), "demo");
    ASSERT_TRUE(j.contains("suggestions"));
    ASSERT_TRUE(j["suggestions"].is_array());

    const auto& arr = j["suggestions"];
    ASSERT_EQ(arr.size(), 2U);
    EXPECT_EQ(arr[0].get<std::string>(), "Try switching short taxi rides to subway or bus.");
    EXPECT_EQ(arr[1].get<std::string>(), "Batch trips to reduce total distance.");
}

/* ---------------- Isolation: other users' events don't leak ---------------- */

TEST(ApiSuggestions, IgnoresOtherUsersWeeklyEmissions)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    mem.set_api_key("u_other", "k_other");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    // Post a huge event for u_other this week (should not affect demo)
    {
        json const             body = { { "mode", "car" },
                                        { "distance_km", 10000.0 },
                                        { "ts", static_cast<std::int64_t>(std::time(nullptr)) } };
        httplib::Headers const hdr  = { { "X-API-Key", "k_other" }, { "Content-Type", "application/json" } };
        auto res_post = cli.Post("/users/u_other/transit", hdr, body.dump(), "application/json");
        ASSERT_TRUE(res_post != nullptr);
        ASSERT_EQ(res_post->status, 201);
    }

    // demo is separate and should have no events --> expect the low-emissions suggestion branch
    auto res = cli.Get("/users/demo/suggestions", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_EQ(j.value("user_id", ""), "demo");
    ASSERT_TRUE(j["suggestions"].is_array());
    ASSERT_EQ(j["suggestions"].size(), 1U);
    EXPECT_EQ(j["suggestions"][0].get<std::string>(),
              "Nice work! Consider biking or walking for short hops.");
}

/* ===================================================== */
/* -------- GET /users/{user_id}/analytics ------------- */
/* ===================================================== */

/* ---------------- Path / Routing ---------------- */

TEST(ApiAnalytics, BadPath_NoUserInUrl)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users//analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiAnalytics, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users/demo/analytics/extra", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

/* ---------------- Auth ---------------- */

TEST(ApiAnalytics, Unauthorized_NoHeader)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users/demo/analytics"); // no X-API-Key
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(ApiAnalytics, Unauthorized_WrongKey)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client        cli("127.0.0.1", server.port);
    httplib::Headers const wrong = { { "X-API-Key", "not-the-key" } };
    auto                   res   = cli.Get("/users/demo/analytics", wrong);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---------------- Behavior: edge cases ---------------- */

// No one has events --> peer avg = 0, user week = 0, above_peer_avg = false
TEST(ApiAnalytics, Success_NoEvents_AllZero)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_EQ(j.value("user_id", ""), "demo");
    EXPECT_DOUBLE_EQ(j.value("this_week_kg_co2", -1.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("peer_week_avg_kg_co2", -1.0), 0.0);
    EXPECT_FALSE(j.value("above_peer_avg", true));
}

// Only demo has events this week --> peer avg includes demo; equal to demo's week
TEST(ApiAnalytics, Success_OnlyDemoHasEvents_PeerAvgEqualsUser)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    const std::time_t now = std::time(nullptr);

    // post event for demo
    post_transit(cli, 10.0, "bus", static_cast<std::int64_t>(now));

    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto         j = json::parse(res->body);
    double const u = j.value("this_week_kg_co2", -1.0);
    double const p = j.value("peer_week_avg_kg_co2", -1.0);

    ASSERT_GE(u, 0.0);
    ASSERT_GE(p, 0.0);

    // With only one active user, average == user
    EXPECT_NEAR(u, p, 1e-9);
    EXPECT_FALSE(j.value("above_peer_avg", true));
}

// Peers have events, demo has none --> avg > 0, user == 0, above_peer_avg = false
TEST(ApiAnalytics, Success_PeersOnly_DemoZeroAboveFalse)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    mem.set_api_key("u1", "k1");
    mem.set_api_key("u2", "k2");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    const std::time_t now = std::time(nullptr);
    // Post weekly events for two peers
    {
        json const             b1 = { { "mode", "bus" },
                                      { "distance_km", 20.0 },
                                      { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers const h1 = { { "X-API-Key", "k1" }, { "Content-Type", "application/json" } };
        auto                   r1 = cli.Post("/users/u1/transit", h1, b1.dump(), "application/json");
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 201);

        json const             b2 = { { "mode", "bus" },
                                      { "distance_km", 40.0 },
                                      { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers const h2 = { { "X-API-Key", "k2" }, { "Content-Type", "application/json" } };
        auto                   r2 = cli.Post("/users/u2/transit", h2, b2.dump(), "application/json");
        ASSERT_TRUE(r2 != nullptr);
        ASSERT_EQ(r2->status, 201);
    }

    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_DOUBLE_EQ(j.value("this_week_kg_co2", -1.0), 0.0);
    EXPECT_GT(j.value("peer_week_avg_kg_co2", -1.0), 0.0);
    EXPECT_FALSE(j.value("above_peer_avg", true));
}

/* ---------------- Behavior: core comparisons ---------------- */

// Demo higher than peers
TEST(ApiAnalytics, Success_AbovePeerAvg_WhenHigherThanPeers)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    mem.set_api_key("u1", "k1");
    mem.set_api_key("u2", "k2");
    TestServer const  server(mem);
    httplib::Client   cli("127.0.0.1", server.port);
    const std::time_t now = std::time(nullptr);

    // Peers each at 100 km, demo at 200 km
    {
        json const             b1 = { { "mode", "bus" },
                                      { "distance_km", 100.0 },
                                      { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers const h1 = { { "X-API-Key", "k1" }, { "Content-Type", "application/json" } };
        auto                   r1 = cli.Post("/users/u1/transit", h1, b1.dump(), "application/json");
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 201);

        json const             b2 = { { "mode", "bus" },
                                      { "distance_km", 100.0 },
                                      { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers const h2 = { { "X-API-Key", "k2" }, { "Content-Type", "application/json" } };
        auto                   r2 = cli.Post("/users/u2/transit", h2, b2.dump(), "application/json");
        ASSERT_TRUE(r2 != nullptr);
        ASSERT_EQ(r2->status, 201);

        post_transit(cli, 200.0, "bus", static_cast<std::int64_t>(now)); // demo
    }

    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto         j = json::parse(res->body);
    double const u = j.value("this_week_kg_co2", -1.0);
    double const p = j.value("peer_week_avg_kg_co2", -1.0);

    ASSERT_GT(u, 0.0);
    ASSERT_GT(p, 0.0);
    EXPECT_TRUE(j.value("above_peer_avg", false));

    EXPECT_NEAR(u / p, 1.5, 1e-6);
}

// All users have identical weekly totals --> peer avg == user; above_peer_avg = false
TEST(ApiAnalytics, Success_EqualToPeerAvg_WhenAllEqual)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    mem.set_api_key("u1", "k1");
    mem.set_api_key("u2", "k2");
    TestServer const  server(mem);
    httplib::Client   cli("127.0.0.1", server.port);
    const std::time_t now = std::time(nullptr);

    // Give each user the same weekly footprint (same mode, same distance)
    {
        post_transit(cli, 50.0, "bus", static_cast<std::int64_t>(now)); // demo

        json const             b1 = { { "mode", "bus" },
                                      { "distance_km", 50.0 },
                                      { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers const h1 = { { "X-API-Key", "k1" }, { "Content-Type", "application/json" } };
        auto                   r1 = cli.Post("/users/u1/transit", h1, b1.dump(), "application/json");
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 201);

        json const             b2 = { { "mode", "bus" },
                                      { "distance_km", 50.0 },
                                      { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers const h2 = { { "X-API-Key", "k2" }, { "Content-Type", "application/json" } };
        auto                   r2 = cli.Post("/users/u2/transit", h2, b2.dump(), "application/json");
        ASSERT_TRUE(r2 != nullptr);
        ASSERT_EQ(r2->status, 201);
    }

    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto         j = json::parse(res->body);
    double const u = j.value("this_week_kg_co2", -1.0);
    double const p = j.value("peer_week_avg_kg_co2", -1.0);

    EXPECT_NEAR(u, p, 1e-9);
    EXPECT_FALSE(j.value("above_peer_avg", true));
}

/* ---------------- Windowing sanity check ---------------- */

// Events older than 7 days should not affect this_week nor peer average
TEST(ApiAnalytics, Success_IgnoresEventsOlderThan7Days)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    mem.set_api_key("u_old", "k_old");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    const std::time_t now            = std::time(nullptr);
    const std::time_t eight_days_ago = now - static_cast<std::time_t>(8 * 24 * 3600);

    // Post an old event for demo and another user; both should be ignored
    post_transit(cli, /*distance_km=*/500.0, "bus", static_cast<std::int64_t>(eight_days_ago));

    json const             b = { { "mode", "bus" },
                                 { "distance_km", 500.0 },
                                 { "ts", static_cast<std::int64_t>(eight_days_ago) } };
    httplib::Headers const h = { { "X-API-Key", "k_old" }, { "Content-Type", "application/json" } };
    auto                   r = cli.Post("/users/u_old/transit", h, b.dump(), "application/json");
    ASSERT_TRUE(r != nullptr);
    ASSERT_EQ(r->status, 201);

    // Now analytics should show zeros
    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_DOUBLE_EQ(j.value("this_week_kg_co2", -1.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("peer_week_avg_kg_co2", -1.0), 0.0);
    EXPECT_FALSE(j.value("above_peer_avg", true));
}

/* =================================================== */
/* ----------------- Admin: Auth --------------------- */
/* =================================================== */

TEST(AdminAuth, Unauthorized_NoHeader)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Get("/admin/clients"); // no Authorization
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(AdminAuth, Unauthorized_WrongBearer)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const bad = { { "Authorization", "Bearer not-it" } };
    auto                   res = cli.Get("/admin/clients", bad);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

TEST(AdminAuth, Unauthorized_EnvKeyNotSet)
{
// Explicitly unset the ADMIN_API_KEY environment variable
#ifdef _WIN32
    _putenv("ADMIN_API_KEY=");
#else
    unsetenv("ADMIN_API_KEY");
#endif

    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const hdr = { { "Authorization", "Bearer anything" } };
    auto                   res = cli.Get("/admin/clients", hdr);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);

    // Restore for other tests
    set_admin_key("super-secret");
}

/* =================================================== */
/* ---------------- /admin/logs GET ------------------ */
/* ---------------- /admin/logs DELETE --------------- */
/* =================================================== */

TEST(AdminLogs, GetLogs_EmptyWhenNoRequests)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // No prior requests, logs should be empty
    auto res = cli.Get("/admin/logs", admin_auth_headers());
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    auto arr = json::parse(res->body);
    ASSERT_TRUE(arr.is_array());
    EXPECT_EQ(arr.size(), 0U);
}

TEST(AdminLogs, Logs_AppearAfterValidRequests_ThenCanBeCleared)
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Make a couple of valid, loggable requests
    {
        // 1) Lifetime footprint (GET)
        auto r1 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 200);

        // 2) Transit post (POST)
        json const body = { { "mode", "bus" }, { "distance_km", 1.2 } };
        auto       r2 = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");
        ASSERT_TRUE(r2 != nullptr);
        ASSERT_EQ(r2->status, 201);
    }

    // Admin fetch logs --> expect an array with at least the two entries above
    auto res = cli.Get("/admin/logs", admin_auth_headers());
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    auto arr = json::parse(res->body);
    ASSERT_TRUE(arr.is_array());
    ASSERT_GE(arr.size(), 2U);

    // Spot-check a couple of fields on the latest log entries
    bool saw_transit  = false;
    bool saw_lifetime = false;
    for (const auto& item : arr)
    {
        if (!item.is_object())
            continue;
        std::string const path = item.value("path", "");
        std::string const meth = item.value("method", "");
        int const         code = item.value("status", -1);
        if (path == "/users/demo/transit" && meth == "POST" && code == 201)
            saw_transit = true;
        if (path == "/users/demo/lifetime-footprint" && meth == "GET" && code == 200)
            saw_lifetime = true;
    }
    EXPECT_TRUE(saw_transit);
    EXPECT_TRUE(saw_lifetime);

    // Clear logs
    auto del = cli.Delete("/admin/logs", admin_auth_headers());
    ASSERT_TRUE(del != nullptr);
    EXPECT_EQ(del->status, 200);
    EXPECT_EQ(json::parse(del->body).value("status", ""), "ok");

    // Logs should now be empty --> expect empty array
    auto res2 = cli.Get("/admin/logs", admin_auth_headers());
    ASSERT_TRUE(res2 != nullptr);
    ASSERT_EQ(res2->status, 200);
    auto arr2 = json::parse(res2->body);
    ASSERT_TRUE(arr2.is_array());
    EXPECT_EQ(arr2.size(), 0U);
}

/* =================================================== */
/* ------------- /admin/clients (list) --------------- */
/* ------ /admin/clients/{id}/data (per-user) -------- */
/* =================================================== */

TEST(AdminClients, Clients_EmptyUntilTransitEventsExist)
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // No events yet --> expect empty list
    auto res0 = cli.Get("/admin/clients", admin_auth_headers());
    ASSERT_TRUE(res0 != nullptr);
    ASSERT_EQ(res0->status, 200);
    auto list0 = json::parse(res0->body);
    ASSERT_TRUE(list0.is_array());
    EXPECT_EQ(list0.size(), 0U);

    // Create a transit event for 'demo'
    const std::time_t now = std::time(nullptr);
    post_transit(cli, 100.0, "car", static_cast<std::int64_t>(now));

    // Now /admin/clients should include "demo"
    auto res1 = cli.Get("/admin/clients", admin_auth_headers());
    ASSERT_TRUE(res1 != nullptr);
    ASSERT_EQ(res1->status, 200);
    auto list1 = json::parse(res1->body);
    ASSERT_TRUE(list1.is_array());
    ASSERT_GE(list1.size(), 1U);
    bool has_demo = false;
    for (const auto& v : list1)
        if (v.is_string() && v.get<std::string>() == "demo")
            has_demo = true;
    EXPECT_TRUE(has_demo);
}

TEST(AdminClients, ClientData_ReturnsUserTransitEvents_Count)
{
    auto [arr, t1, t2] = setup_demo_with_two_events();
    ASSERT_TRUE(arr.is_array());
    ASSERT_EQ(arr.size(), 2U);
}

TEST(AdminClients, ClientData_ReturnsUserTransitEvents_Content)
{
    auto [arr, t1, t2] = setup_demo_with_two_events();

    bool saw_car  = false;
    bool saw_bike = false;
    for (const auto& e : arr)
    {
        if (!e.is_object())
            continue;
        std::string const  mode = e.value("mode", "");
        double const       dist = e.value("distance_km", -1.0);
        std::int64_t const ts   = e.value("ts", static_cast<std::int64_t>(-1));
        if (mode == "car" && dist == 100.0 && ts == t1)
            saw_car = true;
        if (mode == "bike" && dist == 100.0 && ts == t2)
            saw_bike = true;
    }
    EXPECT_TRUE(saw_car);
    EXPECT_TRUE(saw_bike);
}

TEST(AdminClients, ClientData_UnknownUser_ReturnsEmptyArray)
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Get("/admin/clients/nope/data", admin_auth_headers());
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    auto arr = json::parse(res->body);
    ASSERT_TRUE(arr.is_array());
    EXPECT_EQ(arr.size(), 0U);
}

/* =================================================== */
/* ---- /admin/clear-db-events & /admin/clear-db ----- */
/* =================================================== */

TEST(AdminDb, ClearDbEvents_RemovesOnlyEvents_AfterwardsClientsEmpty)
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Seed some events
    post_transit(cli, 5.0, "bus", static_cast<std::int64_t>(std::time(nullptr)));

    // Sanity: clients non-empty
    auto before = cli.Get("/admin/clients", admin_auth_headers());
    ASSERT_TRUE(before != nullptr);
    ASSERT_EQ(before->status, 200);
    auto list_before = json::parse(before->body);
    ASSERT_TRUE(list_before.is_array());
    ASSERT_GE(list_before.size(), 1U);

    // Clear just events
    auto clr = cli.Get("/admin/clear-db-events", admin_auth_headers());
    ASSERT_TRUE(clr != nullptr);
    EXPECT_EQ(clr->status, 200);
    EXPECT_EQ(json::parse(clr->body).value("status", ""), "ok");

    // Clients should now be empty (no events --> no active clients)
    auto after = cli.Get("/admin/clients", admin_auth_headers());
    ASSERT_TRUE(after != nullptr);
    ASSERT_EQ(after->status, 200);
    auto list_after = json::parse(after->body);
    ASSERT_TRUE(list_after.is_array());
    EXPECT_EQ(list_after.size(), 0U);
}

TEST(AdminDb, ClearDb_RemovesEverything_ClientsEmpty)
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Seed some events and make at least one other request to generate logs
    post_transit(cli, 3.0, "walk", static_cast<std::int64_t>(std::time(nullptr)));
    auto lf = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(lf != nullptr);
    ASSERT_EQ(lf->status, 200);

    // Clear the whole DB
    auto clr = cli.Get("/admin/clear-db", admin_auth_headers());
    ASSERT_TRUE(clr != nullptr);
    EXPECT_EQ(clr->status, 200);
    EXPECT_EQ(json::parse(clr->body).value("status", ""), "ok");

    // Clients empty
    auto clients = cli.Get("/admin/clients", admin_auth_headers());
    ASSERT_TRUE(clients != nullptr);
    ASSERT_EQ(clients->status, 200);
    auto list = json::parse(clients->body);
    ASSERT_TRUE(list.is_array());
    EXPECT_EQ(list.size(), 0U);

    // Logs empty
    auto logs = cli.Get("/admin/logs", admin_auth_headers());
    ASSERT_TRUE(logs != nullptr);
    ASSERT_EQ(logs->status, 200);
    auto arr = json::parse(logs->body);
    ASSERT_TRUE(arr.is_array());
    EXPECT_EQ(arr.size(), 0U);
}

/* =================================================== */
/* ---- Additional Branch Coverage Tests ------------ */
/* =================================================== */

// Test admin endpoints without ADMIN_API_KEY environment variable set
TEST(AdminAuth, Unauthorized_NoAdminKeyEnvVar)
{
    // Temporarily unset ADMIN_API_KEY
#ifdef _WIN32
    _putenv_s("ADMIN_API_KEY", "");
#else
    unsetenv("ADMIN_API_KEY");
#endif

    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const headers = { { "Authorization", "Bearer some-token" } };
    auto                   res     = cli.Get("/admin/clients", headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

// Test Authorization header without "Bearer " prefix
TEST(AdminAuth, Unauthorized_MissingBearerPrefix)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers const headers = { { "Authorization", "super-secret" } }; // Missing "Bearer "
    auto                   res     = cli.Get("/admin/clients", headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

// Test user endpoints with missing user_id parameter
TEST(ApiTransit, Unauthorized_UserNotRegistered)
{
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Try to post transit for a user that doesn't exist
    auto res = cli.Post("/users/nonexistent/transit", demo_auth_headers(),
                        R"({"mode":"bus","distance_km":1.0})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401); // Should be unauthorized
}

/* =================================================== */
/* ---- Additional Edge Cases for Branch Coverage ---- */
/* =================================================== */

// Test JSON type error when getting wrong type from JSON field
TEST(ApiTransit, JsonTypeError_ModeAsArray)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // mode is an array instead of string - should trigger json::exception
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":[],"distance_km":1.0})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    auto j = json::parse(res->body);
    // Should catch json::exception
    EXPECT_TRUE(j.contains("error"));
}

// Test JSON type error when distance_km is wrong type
TEST(ApiTransit, JsonTypeError_DistanceAsObject)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // distance_km is an object instead of number - should trigger json::exception
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"bus","distance_km":{}})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    auto j = json::parse(res->body);
    EXPECT_TRUE(j.contains("error"));
}

// Test JSON type error when ts is wrong type
TEST(ApiTransit, JsonTypeError_TsAsString)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // ts is a string instead of number - should trigger json::exception
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":"bus","distance_km":5.0,"ts":"not-a-number"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    auto j = json::parse(res->body);
    EXPECT_TRUE(j.contains("error"));
}

// Test all valid transit modes to ensure they work
TEST(ApiTransit, AllValidModes_Success)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    const std::vector<std::string> modes = { "taxi", "car", "bus", "subway", "train", "bike", "walk" };

    for (const auto& mode : modes)
    {
        json const body = { { "mode", mode }, { "distance_km", 1.0 } };
        auto res = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

        ASSERT_TRUE(res != nullptr) << "Failed for mode: " << mode;
        EXPECT_EQ(res->status, 201) << "Failed for mode: " << mode;
    }
}

// Test case-sensitive mode validation
TEST(ApiTransit, Validation_InvalidMode_CaseSensitive)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // "Bus" with capital B should fail (modes are lowercase)
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"Bus","distance_km":1.0})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid mode");
}

// Test zero timestamp explicitly (should use server time)
TEST(ApiTransit, Success_ZeroTimestamp_UsesServerTime)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    json const body = { { "mode", "walk" }, { "distance_km", 2.0 }, { "ts", 0 } };
    auto       res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
}

// Test exact zero distance
TEST(ApiTransit, Success_ExactZeroDistance)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"walk","distance_km":0.0})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
}

// Test very large but valid distance
TEST(ApiTransit, Success_VeryLargeDistance)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":"train","distance_km":99999.99})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
}

// Test fractional distance values
TEST(ApiTransit, Success_FractionalDistance)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"bike","distance_km":0.001})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
}

// Test register with very long app_name
TEST(ApiRegister, Register_Success_LongAppName)
{
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    const std::string long_name(200, 'a'); // 200 character app name
    json const        req = { { "app_name", long_name } };

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("app_name", ""), long_name);
}

// path that does NOT match /users/{id}/transit
TEST(ApiTransit, BadPath_InvalidUserSegment)
{
    // (here the {id} segment is empty, so the route regex shouldn't match)
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    json const body = { { "mode", "car" }, { "distance_km", 10.0 } };

    auto res = cli.Post("/users//transit",
                        demo_auth_headers(),
                        body.dump(),
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}


// Test register with special characters in app_name
TEST(ApiRegister, Register_Success_SpecialCharsInAppName)
{
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    json const req = { { "app_name", "my-app_name.test@2024!" } };

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json const j = json::parse(res->body);
    EXPECT_EQ(j.value("app_name", ""), "my-app_name.test@2024!");
}

// Test footprint with subway mode to cover emission_factor_for subway branch
TEST(ApiFootprint, Success_SubwayEmissions)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Add subway transit event
    auto res1 = cli.Post("/users/demo/transit", demo_auth_headers(),
                         R"({"mode":"subway","distance_km":10.0})", "application/json");
    ASSERT_TRUE(res1 != nullptr);
    ASSERT_EQ(res1->status, 201);

    // Get footprint - should calculate emissions using subway factor (DEFRA 2024: 0.041)
    auto res2 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res2 != nullptr);
    EXPECT_EQ(res2->status, 200);

    json const j = json::parse(res2->body);
    // 10 km * 0.041 = 0.41 kg CO2
    EXPECT_NEAR(j["lifetime_kg_co2"].get<double>(), 0.41, 0.001);
}

// Test footprint with train mode to cover emission_factor_for train branch
TEST(ApiFootprint, Success_TrainEmissions)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Add train transit event
    auto res1 = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"train","distance_km":50.0})",
                         "application/json");
    ASSERT_TRUE(res1 != nullptr);
    ASSERT_EQ(res1->status, 201);

    // Get footprint - should calculate emissions using train factor (DEFRA 2024: 0.051)
    auto res2 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res2 != nullptr);
    EXPECT_EQ(res2->status, 200);

    json const j = json::parse(res2->body);
    // 50 km * 0.051 = 2.55 kg CO2
    EXPECT_NEAR(j["lifetime_kg_co2"].get<double>(), 2.55, 0.001);
}

// Test cache functionality - second call should hit cache
TEST(ApiFootprint, Success_CacheHit)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Add one event
    auto res1 = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"car","distance_km":10.0})",
                         "application/json");
    ASSERT_TRUE(res1 != nullptr);
    ASSERT_EQ(res1->status, 201);

    // First call - calculates and caches
    auto res2 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res2 != nullptr);
    EXPECT_EQ(res2->status, 200);

    // Second call - should hit cache (line 187)
    auto res3 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res3 != nullptr);
    EXPECT_EQ(res3->status, 200);

    json const j = json::parse(res3->body);
    // 10 km * 0.18 = 1.8 kg CO2
    EXPECT_DOUBLE_EQ(j["lifetime_kg_co2"].get<double>(), 1.8);
}

// Test get_logs with more logs than limit to cover line 117
TEST(AdminLogs, Logs_ExceedLimit_ReturnsLastN)
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Generate more than 1000 logs by making many requests
    // The admin endpoint calls get_logs(1000), so we need > 1000 to test the branch
    for (int i = 0; i < 1050; i++)
    {
        cli.Get("/health");
    }

    // Get logs - should only return last 1000
    auto res = cli.Get("/admin/logs", admin_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    json const j = json::parse(res->body);
    ASSERT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 1000); // Should be capped at limit of 1000
}

// Test admin endpoints - unauthorized paths (true branches of check_admin)
TEST(AdminLogs, Unauthorized_GetLogs)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    // Try to access admin endpoint with wrong auth
    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res         = cli.Get("/admin/logs", bad_headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

TEST(AdminLogs, Unauthorized_DeleteLogs)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res         = cli.Delete("/admin/logs", bad_headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

TEST(AdminClients, Unauthorized_GetClients)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res         = cli.Get("/admin/clients", bad_headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

TEST(AdminClients, Unauthorized_GetClientData)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res         = cli.Get("/admin/clients/demo/data", bad_headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

TEST(AdminDb, Unauthorized_ClearDbEvents)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res         = cli.Get("/admin/clear-db-events", bad_headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

TEST(AdminDb, Unauthorized_ClearDb)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res         = cli.Get("/admin/clear-db", bad_headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---- Emission Factors Admin Tests ---- */

TEST(AdminEmissionFactors, GetDefaults_ReturnsBasicDefaults)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Get("/admin/emission-factors", admin_auth_headers());

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_TRUE(j.is_array());
    EXPECT_GT(j.size(), 0);

    // Check that we have basic defaults (not empty)
    // All factors should have: mode, fuel_type, vehicle_size, kg_co2_per_km, source, updated_at
    for (const auto& factor : j)
    {
        EXPECT_TRUE(factor.contains("mode"));
        EXPECT_TRUE(factor.contains("kg_co2_per_km"));
        EXPECT_TRUE(factor.contains("source"));
    }

    // Verify at least one car petrol small factor exists
    bool found_car_petrol = false;
    for (const auto& factor : j)
    {
        if (factor["mode"] == "car" && factor["fuel_type"] == "petrol" && factor["vehicle_size"] == "small")
        {
            found_car_petrol = true;
            // DEFRA 2024 car petrol small should be ~0.167 (from defra_2024_factors())
            EXPECT_NEAR(factor["kg_co2_per_km"].get<double>(), 0.167, 0.01);
        }
    }
    EXPECT_TRUE(found_car_petrol) << "Should have car/petrol/small factor";
}

TEST(AdminEmissionFactors, LoadDefra2024_ReturnsCount)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    auto res = cli.Post("/admin/emission-factors/load", admin_auth_headers(), "", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto j = json::parse(res->body);
    EXPECT_TRUE(j.contains("loaded"));
    EXPECT_GT(j["loaded"].get<int>(), 0);

    // Verify we got DEFRA factors now (should be more precise than basic)
    // Get them back to verify
    auto factors_res = cli.Get("/admin/emission-factors", admin_auth_headers());
    ASSERT_TRUE(factors_res != nullptr);
    EXPECT_EQ(factors_res->status, 200);

    auto factors_j = json::parse(factors_res->body);
    EXPECT_TRUE(factors_j.is_array());
    EXPECT_GT(factors_j.size(), 0);

    bool found_defra_car_petrol = false;
    for (const auto& factor : factors_j)
    {
        if (factor["mode"] == "car" && factor["fuel_type"] == "petrol" && factor["vehicle_size"] == "small")
        {
            found_defra_car_petrol = true;
            // DEFRA 2024 car petrol small should be ~0.167
            EXPECT_NEAR(factor["kg_co2_per_km"].get<double>(), 0.167, 0.01);
        }
    }
    EXPECT_TRUE(found_defra_car_petrol) << "Should have DEFRA car/petrol/small factor";
}

TEST(AdminEmissionFactors, LoadDefra2024_Unauthorized)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res = cli.Post("/admin/emission-factors/load", bad_headers, "", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

TEST(AdminEmissionFactors, GetFactors_Unauthorized)
{
    set_admin_key("super-secret");
    InMemoryStore    mem;
    TestServer const server(mem);
    httplib::Client  cli("127.0.0.1", server.port);

    httplib::Headers bad_headers = { { "Authorization", "Bearer wrong-key" } };
    auto             res         = cli.Get("/admin/emission-factors", bad_headers);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}
