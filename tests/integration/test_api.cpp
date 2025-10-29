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
    json body = { { "mode", mode }, { "distance_km", distance_km }, { "ts", ts } };
    auto res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");
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
    const char* k     = std::getenv("ADMIN_API_KEY");
    std::string token = k ? k : "";
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
    TestServer server(mem);

    const httplib::Client cli("127.0.0.1", server.port);
    const auto            res = cli.Get("/health");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const auto j = json::parse(res->body);
    EXPECT_TRUE(j["ok"].get<bool>());
}

// Test helper: create a server, post two events for 'demo', and return the parsed array + timestamps
static std::tuple<json, std::int64_t, std::int64_t> setup_demo_with_two_events()
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Post("/users/register"); // literally no body

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "invalid_json");
}

TEST(ApiRegister, Register_InvalidJSON_Garbage)
{
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Post("/users/register", "not-json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "invalid_json");
}

TEST(ApiRegister, Register_MissingAppName_KeyAbsent)
{
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    json            req = json::object(); // {}

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "missing_app_name");
}

TEST(ApiRegister, Register_MissingAppName_WrongType)
{
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    json            req = { { "app_name", 123 } }; // not a string

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "missing_app_name");
}

// Form-encoded body should fail JSON parse
TEST(ApiRegister, Register_InvalidJSON_FormEncoded)
{
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client  cli("127.0.0.1", server.port);
    httplib::Headers headers = { { "Content-Type", "application/x-www-form-urlencoded" } };
    auto res = cli.Post("/users/register", headers, "app_name=myapp", "application/x-www-form-urlencoded");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);

    json j = json::parse(res->body);
    EXPECT_EQ(j.value("error", ""), "invalid_json");
}

// ---- Success cases ----

TEST(ApiRegister, Register_Success_Minimal)
{
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    const std::string app_name = "myapp";
    json              req      = { { "app_name", app_name } };

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
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    json req = { { "app_name", "widgetizer" }, { "noise", "ignored" }, { "version", 3 } };

    auto res = cli.Post("/users/register", req.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json j = json::parse(res->body);
    EXPECT_EQ(j.value("app_name", ""), "widgetizer");
    EXPECT_TRUE(j.contains("user_id"));
    EXPECT_TRUE(j.contains("api_key"));
}

// Content-Type not JSON but body IS valid JSON → still OK (server just parses req.body)
TEST(ApiRegister, Register_Success_TextPlainBody)
{
    InMemoryStore mem;
    TestServer    server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    json             req     = { { "app_name", "plain" } };
    httplib::Headers headers = { { "Content-Type", "text/plain" } };

    auto res = cli.Post("/users/register", headers, req.dump(), "text/plain");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);

    json j = json::parse(res->body);
    EXPECT_EQ(j.value("app_name", ""), "plain");
}

/* ============================================================= */
/* ---------- POST /users/{user_id}/transit Testcases ---------- */
/* ============================================================= */

/* ---------------- Path / Routing ---------------- */

TEST(ApiTransit, BadPath_NoUserInUrl)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users//transit", demo_auth_headers(), R"({"mode":"bus","distance_km":1})",
                        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiTransit, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", R"({"mode":"walk","distance_km":0.5})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(ApiTransit, Unauthorized_WrongKey)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers wrong = { { "X-API-Key", "not-the-key" }, { "Content-Type", "application/json" } };
    auto             res =
        cli.Post("/users/demo/transit", wrong, R"({"mode":"walk","distance_km":0.5})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---------------- JSON parsing / validation ---------------- */

TEST(ApiTransit, InvalidJson_EmptyBody)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), "", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid_json");
}

TEST(ApiTransit, InvalidJson_Garbage)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), "not-json", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid_json");
}

TEST(ApiTransit, MissingFields_ModeAbsent)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":123,"distance_km":1.0})",
                        "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_NE(res->status, 201); // expect 400 if guarded
}

TEST(ApiTransit, WrongTypes_DistanceNotNumber)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    json body = { { "mode", "subway" }, { "distance_km", 7.4 } };
    auto res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status", ""), "ok");
}

TEST(ApiTransit, Success_WithExplicitTs)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    json body = { { "mode", "bike" },
                  { "distance_km", 12.0 },
                  { "ts", static_cast<std::int64_t>(1730000000) } };
    auto res  = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status", ""), "ok");
}

TEST(ApiTransit, Success_IntegerDistanceAccepted)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers hdrs = { { "X-API-Key", "secret-demo-key" }, { "Content-Type", "text/plain" } };
    json             body = { { "mode", "tram" }, { "distance_km", 3.1 } };
    auto             res  = cli.Post("/users/demo/transit", hdrs, body.dump(), "text/plain");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid mode");
}

TEST(ApiTransit, InvalidJson_FormEncoded)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers hdrs = { { "X-API-Key", "secret-demo-key" },
                              { "Content-Type", "application/x-www-form-urlencoded" } };
    auto             res  = cli.Post("/users/demo/transit", hdrs, "mode=bus&distance_km=2.5",
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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // Note: using auth headers even for bad path to isolate routing
    auto res = cli.Get("/users//lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiFootprint, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Get("/users/demo/lifetime-footprint/extra", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

// -------------- Auth ----------------

TEST(ApiFootprint, Unauthorized_NoHeader)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Get("/users/demo/lifetime-footprint");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(ApiFootprint, Unauthorized_WrongKey)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers wrong = { { "X-API-Key", "not-the-key" } };
    auto             res   = cli.Get("/users/demo/lifetime-footprint", wrong);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

// -------------- Success: empty store ----------------

TEST(ApiFootprint, Success_ZeroWhenNoEvents)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // Create three events:
    // - 2 days ago: distance 1
    // - 10 days ago: distance 2
    // - 40 days ago: distance 3
    const std::time_t now = std::time(nullptr);
    post_transit(cli, 1.0, "bus", now - (2 * 24 * 3600));
    post_transit(cli, 2.0, "bus", now - (10 * 24 * 3600));
    post_transit(cli, 3.0, "bus", now - (40 * 24 * 3600));

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // Post an event for u_other (should NOT count for demo)
    {
        json             body = { { "mode", "car" }, { "distance_km", 100.0 } };
        httplib::Headers hdr  = { { "X-API-Key", "k_other" }, { "Content-Type", "application/json" } };
        auto             res  = cli.Post("/users/u_other/transit", hdr, body.dump(), "application/json");
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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), R"({"mode":"walk","distance_km":-3.5})",
                        "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "Negative value for distance_km is not allowed.");
}

TEST(ApiTransit, Validation_InvalidMode)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":"spaceship","distance_km":1.0})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "invalid mode");
}

// unit-style integration for empty user_id validation (constructor throws)
TEST(TransitEventUnit, Validation_EmptyUserIdThrows)
{
    try
    {
        TransitEvent ev("", "walk", 1.0, 0);
        FAIL() << "Expected std::runtime_error to be thrown for empty user_id";
    }
    catch (const std::runtime_error& e)
    {
        std::string msg(e.what());
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
    TestServer server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users//suggestions", demo_auth_headers());

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiSuggestions, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);

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
    TestServer server(mem);

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
    TestServer server(mem);

    httplib::Client  cli("127.0.0.1", server.port);
    httplib::Headers wrong = { { "X-API-Key", "not-the-key" } };
    auto             res   = cli.Get("/users/demo/suggestions", wrong);

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---------------- Behavior: low vs high weekly emissions ---------------- */

TEST(ApiSuggestions, Success_LowEmissions_NoEvents)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);

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
    TestServer server(mem);

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
    TestServer server(mem);

    httplib::Client cli("127.0.0.1", server.port);

    // Post a huge event for u_other this week (should not affect demo)
    {
        json             body     = { { "mode", "car" },
                                      { "distance_km", 10000.0 },
                                      { "ts", static_cast<std::int64_t>(std::time(nullptr)) } };
        httplib::Headers hdr      = { { "X-API-Key", "k_other" }, { "Content-Type", "application/json" } };
        auto             res_post = cli.Post("/users/u_other/transit", hdr, body.dump(), "application/json");
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
    TestServer server(mem);

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/users//analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiAnalytics, BadPath_ExtraSegment)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);

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
    TestServer server(mem);

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
    TestServer server(mem);

    httplib::Client  cli("127.0.0.1", server.port);
    httplib::Headers wrong = { { "X-API-Key", "not-the-key" } };
    auto             res   = cli.Get("/users/demo/analytics", wrong);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---------------- Behavior: edge cases ---------------- */

// No one has events --> peer avg = 0, user week = 0, above_peer_avg = false
TEST(ApiAnalytics, Success_NoEvents_AllZero)
{
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    const std::time_t now = std::time(nullptr);

    // post event for demo
    post_transit(cli, 10.0, "bus", static_cast<std::int64_t>(now));

    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto   j = json::parse(res->body);
    double u = j.value("this_week_kg_co2", -1.0);
    double p = j.value("peer_week_avg_kg_co2", -1.0);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    const std::time_t now = std::time(nullptr);
    // Post weekly events for two peers
    {
        json b1 = { { "mode", "bus" }, { "distance_km", 20.0 }, { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers h1 = { { "X-API-Key", "k1" }, { "Content-Type", "application/json" } };
        auto             r1 = cli.Post("/users/u1/transit", h1, b1.dump(), "application/json");
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 201);

        json b2 = { { "mode", "bus" }, { "distance_km", 40.0 }, { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers h2 = { { "X-API-Key", "k2" }, { "Content-Type", "application/json" } };
        auto             r2 = cli.Post("/users/u2/transit", h2, b2.dump(), "application/json");
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
    TestServer        server(mem);
    httplib::Client   cli("127.0.0.1", server.port);
    const std::time_t now = std::time(nullptr);

    // Peers each at 100 km, demo at 200 km
    {
        json b1 = { { "mode", "bus" }, { "distance_km", 100.0 }, { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers h1 = { { "X-API-Key", "k1" }, { "Content-Type", "application/json" } };
        auto             r1 = cli.Post("/users/u1/transit", h1, b1.dump(), "application/json");
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 201);

        json b2 = { { "mode", "bus" }, { "distance_km", 100.0 }, { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers h2 = { { "X-API-Key", "k2" }, { "Content-Type", "application/json" } };
        auto             r2 = cli.Post("/users/u2/transit", h2, b2.dump(), "application/json");
        ASSERT_TRUE(r2 != nullptr);
        ASSERT_EQ(r2->status, 201);

        post_transit(cli, 200.0, "bus", static_cast<std::int64_t>(now)); // demo
    }

    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto   j = json::parse(res->body);
    double u = j.value("this_week_kg_co2", -1.0);
    double p = j.value("peer_week_avg_kg_co2", -1.0);

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
    TestServer        server(mem);
    httplib::Client   cli("127.0.0.1", server.port);
    const std::time_t now = std::time(nullptr);

    // Give each user the same weekly footprint (same mode, same distance)
    {
        post_transit(cli, 50.0, "bus", static_cast<std::int64_t>(now)); // demo

        json b1 = { { "mode", "bus" }, { "distance_km", 50.0 }, { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers h1 = { { "X-API-Key", "k1" }, { "Content-Type", "application/json" } };
        auto             r1 = cli.Post("/users/u1/transit", h1, b1.dump(), "application/json");
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 201);

        json b2 = { { "mode", "bus" }, { "distance_km", 50.0 }, { "ts", static_cast<std::int64_t>(now) } };
        httplib::Headers h2 = { { "X-API-Key", "k2" }, { "Content-Type", "application/json" } };
        auto             r2 = cli.Post("/users/u2/transit", h2, b2.dump(), "application/json");
        ASSERT_TRUE(r2 != nullptr);
        ASSERT_EQ(r2->status, 201);
    }

    auto res = cli.Get("/users/demo/analytics", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);

    auto   j = json::parse(res->body);
    double u = j.value("this_week_kg_co2", -1.0);
    double p = j.value("peer_week_avg_kg_co2", -1.0);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    const std::time_t now            = std::time(nullptr);
    const std::time_t eight_days_ago = now - (8 * 24 * 3600);

    // Post an old event for demo and another user; both should be ignored
    post_transit(cli, /*distance_km=*/500.0, "bus", static_cast<std::int64_t>(eight_days_ago));

    json             b = { { "mode", "bus" },
                           { "distance_km", 500.0 },
                           { "ts", static_cast<std::int64_t>(eight_days_ago) } };
    httplib::Headers h = { { "X-API-Key", "k_old" }, { "Content-Type", "application/json" } };
    auto             r = cli.Post("/users/u_old/transit", h, b.dump(), "application/json");
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
    InMemoryStore   mem;
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Get("/admin/clients"); // no Authorization
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error", ""), "unauthorized");
}

TEST(AdminAuth, Unauthorized_WrongBearer)
{
    set_admin_key("super-secret");
    InMemoryStore   mem;
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers bad = { { "Authorization", "Bearer not-it" } };
    auto             res = cli.Get("/admin/clients", bad);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* =================================================== */
/* ---------------- /admin/logs GET ------------------ */
/* ---------------- /admin/logs DELETE --------------- */
/* =================================================== */

TEST(AdminLogs, Logs_AppearAfterValidRequests_ThenCanBeCleared)
{
    set_admin_key("super-secret");
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // Make a couple of valid, loggable requests
    {
        // 1) Lifetime footprint (GET)
        auto r1 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
        ASSERT_TRUE(r1 != nullptr);
        ASSERT_EQ(r1->status, 200);

        // 2) Transit post (POST)
        json body = { { "mode", "bus" }, { "distance_km", 1.2 } };
        auto r2   = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");
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
        std::string path = item.value("path", "");
        std::string meth = item.value("method", "");
        int         code = item.value("status", -1);
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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
        std::string  mode = e.value("mode", "");
        double       dist = e.value("distance_km", -1.0);
        std::int64_t ts   = e.value("ts", static_cast<std::int64_t>(-1));
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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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
    TestServer      server(mem);
    httplib::Client cli("127.0.0.1", server.port);

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