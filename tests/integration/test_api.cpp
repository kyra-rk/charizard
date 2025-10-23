#include <gtest/gtest.h>
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "api.hpp"
#include "storage.hpp"

#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>

using nlohmann::json;

static bool is_hex(const std::string& s) {
    static const std::regex re("^[0-9a-fA-F]+$");
    return std::regex_match(s, re);
}

static httplib::Headers demo_auth_headers() {
    return {{"X-API-Key", "secret-demo-key"}, {"Content-Type", "application/json"}};
}

// Small helper to ingest a transit event via the POST endpoint
static void post_transit(httplib::Client& cli,
                         double distance_km,
                         const std::string& mode,
                         std::int64_t ts /* epoch seconds */)
{
    json body = {{"mode", mode}, {"distance_km", distance_km}, {"ts", ts}};
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");
    ASSERT_TRUE(res != nullptr) << "Transit POST returned null";
    ASSERT_EQ(res->status, 201) << "Transit POST failed, body: " << (res ? res->body : "");
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

    httplib::Client cli("127.0.0.1", server.port);
    auto            res = cli.Get("/health");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    auto j = json::parse(res->body);
    EXPECT_TRUE(j["ok"].get<bool>());
}

/* =================================================== */
/* --------- POST /users/register Testcases ---------- */
/* =================================================== */

TEST(ApiRegister, Register_InvalidJSON_EmptyBody) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);
	auto res = cli.Post("/users/register");  // literally no body

	ASSERT_TRUE(res != nullptr);
	EXPECT_EQ(res->status, 400);

	json j = json::parse(res->body);
	EXPECT_EQ(j.value("error", ""), "invalid_json");
}

TEST(ApiRegister, Register_InvalidJSON_Garbage) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);
	auto res = cli.Post("/users/register", "not-json", "application/json");

	ASSERT_TRUE(res != nullptr);
	EXPECT_EQ(res->status, 400);

	json j = json::parse(res->body);
	EXPECT_EQ(j.value("error", ""), "invalid_json");
}

TEST(ApiRegister, Register_MissingAppName_KeyAbsent) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);
	json req = json::object(); // {}

	auto res = cli.Post("/users/register", req.dump(), "application/json");

	ASSERT_TRUE(res != nullptr);
	EXPECT_EQ(res->status, 400);

	json j = json::parse(res->body);
	EXPECT_EQ(j.value("error", ""), "missing_app_name");
}

TEST(ApiRegister, Register_MissingAppName_WrongType) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);
	json req = {{"app_name", 123}};  // not a string

	auto res = cli.Post("/users/register", req.dump(), "application/json");

	ASSERT_TRUE(res != nullptr);
	EXPECT_EQ(res->status, 400);

	json j = json::parse(res->body);
	EXPECT_EQ(j.value("error", ""), "missing_app_name");
}

// Form-encoded body should fail JSON parse
TEST(ApiRegister, Register_InvalidJSON_FormEncoded) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);
	httplib::Headers headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
	auto res = cli.Post("/users/register", headers, "app_name=myapp", "application/x-www-form-urlencoded");

	ASSERT_TRUE(res != nullptr);
	EXPECT_EQ(res->status, 400);

	json j = json::parse(res->body);
	EXPECT_EQ(j.value("error", ""), "invalid_json");
}

// ---- Success cases ----

TEST(ApiRegister, Register_Success_Minimal) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);

	const std::string app_name = "myapp";
	json req = {{"app_name", app_name}};

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
	ASSERT_GE(uid.size(), 3u);
	EXPECT_TRUE(uid.rfind("u_", 0) == 0);           // prefix
	EXPECT_EQ(uid.size(), 10u);                     // "u_" + 8 hex
	EXPECT_TRUE(is_hex(uid.substr(2)));

	// api_key is 32 hex chars
	const auto key = j["api_key"].get<std::string>();
	EXPECT_EQ(key.size(), 32u);
	EXPECT_TRUE(is_hex(key));
}

TEST(ApiRegister, Register_Success_IgnoresExtraFields) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);

	json req = {
		{"app_name", "widgetizer"},
		{"noise", "ignored"},
		{"version", 3}
	};

	auto res = cli.Post("/users/register", req.dump(), "application/json");

	ASSERT_TRUE(res != nullptr);
	EXPECT_EQ(res->status, 201);

	json j = json::parse(res->body);
	EXPECT_EQ(j.value("app_name", ""), "widgetizer");
	EXPECT_TRUE(j.contains("user_id"));
	EXPECT_TRUE(j.contains("api_key"));
}

// Content-Type not JSON but body IS valid JSON → still OK (server just parses req.body)
TEST(ApiRegister, Register_Success_TextPlainBody) {
	InMemoryStore mem;
	TestServer server(mem);

	httplib::Client cli("127.0.0.1", server.port);

	json req = {{"app_name", "plain"}};
	httplib::Headers headers = {{"Content-Type", "text/plain"}};

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

TEST(ApiTransit, BadPath_NoUserInUrl) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users//transit", demo_auth_headers(),
                        R"({"mode":"bus","distance_km":1})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiTransit, BadPath_ExtraSegment) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit/extra", demo_auth_headers(),
                        R"({"mode":"bus","distance_km":1})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

/* ---------------- Auth (explicit) ---------------- */

TEST(ApiTransit, Unauthorized_NoHeader) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit",
                        R"({"mode":"walk","distance_km":0.5})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error",""), "unauthorized");
}

TEST(ApiTransit, Unauthorized_WrongKey) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers wrong = {{"X-API-Key", "not-the-key"}, {"Content-Type","application/json"}};
    auto res = cli.Post("/users/demo/transit", wrong,
                        R"({"mode":"walk","distance_km":0.5})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

/* ---------------- JSON parsing / validation ---------------- */

TEST(ApiTransit, InvalidJson_EmptyBody) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), "", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error",""), "invalid_json");
}

TEST(ApiTransit, InvalidJson_Garbage) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), "not-json", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error",""), "invalid_json");
}

TEST(ApiTransit, MissingFields_ModeAbsent) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"distance_km": 3.4})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error",""), "missing_fields");
}

TEST(ApiTransit, MissingFields_DistanceAbsent) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":"bus"})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error",""), "missing_fields");
}

/* If you add type guards before get<>() (recommended), these should be 400.
   Without guards, get<>() will throw and likely crash the handler. */
TEST(ApiTransit, WrongTypes_ModeNotString) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":123,"distance_km":1.0})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_NE(res->status, 201); // expect 400 if guarded
}

TEST(ApiTransit, WrongTypes_DistanceNotNumber) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":"walk","distance_km":"far"})", "application/json");
    ASSERT_TRUE(res != nullptr);
    EXPECT_NE(res->status, 201); // expect 400 if guarded
}

/* ---------------- Success paths ---------------- */

TEST(ApiTransit, Success_Minimal_UsesServerTs) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    json body = {{"mode","subway"}, {"distance_km", 7.4}};
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status",""), "ok");
}

TEST(ApiTransit, Success_WithExplicitTs) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    json body = {{"mode","bike"}, {"distance_km", 12.0}, {"ts", static_cast<std::int64_t>(1730000000)}};
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(), body.dump(), "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status",""), "ok");
}

TEST(ApiTransit, Success_IntegerDistanceAccepted) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // integer literal should parse; get<double>() will coerce fine
    auto res = cli.Post("/users/demo/transit", demo_auth_headers(),
                        R"({"mode":"car","distance_km":5})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status",""), "ok");
}

/* Content-Type variations: server parses req.body regardless of header */
TEST(ApiTransit, Success_TextPlainWithJsonBody) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers hdrs = {{"X-API-Key","secret-demo-key"}, {"Content-Type","text/plain"}};
    json body = {{"mode","tram"}, {"distance_km", 3.1}};
    auto res = cli.Post("/users/demo/transit", hdrs, body.dump(), "text/plain");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 201);
    EXPECT_EQ(json::parse(res->body).value("status",""), "ok");
}

TEST(ApiTransit, InvalidJson_FormEncoded) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers hdrs = {{"X-API-Key","secret-demo-key"}, {"Content-Type","application/x-www-form-urlencoded"}};
    auto res = cli.Post("/users/demo/transit", hdrs, "mode=bus&distance_km=2.5", "application/x-www-form-urlencoded");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    EXPECT_EQ(json::parse(res->body).value("error",""), "invalid_json");
}

/* ======================================================================== */
/* ---------- POST /users/{user_id}/lifetime-footprint Testcases ---------- */
/* ======================================================================== */

// -------------- Path / routing ----------------

TEST(ApiFootprint, BadPath_NoUserInUrl) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // Note: using auth headers even for bad path to isolate routing
    auto res = cli.Get("/users//lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

TEST(ApiFootprint, BadPath_ExtraSegment) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Get("/users/demo/lifetime-footprint/extra", demo_auth_headers());
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
}

// -------------- Auth ----------------

TEST(ApiFootprint, Unauthorized_NoHeader) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    auto res = cli.Get("/users/demo/lifetime-footprint");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
    EXPECT_EQ(json::parse(res->body).value("error",""), "unauthorized");
}

TEST(ApiFootprint, Unauthorized_WrongKey) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    httplib::Headers wrong = {{"X-API-Key","not-the-key"}};
    auto res = cli.Get("/users/demo/lifetime-footprint", wrong);
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 401);
}

// -------------- Success: empty store ----------------

TEST(ApiFootprint, Success_ZeroWhenNoEvents) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
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

TEST(ApiFootprint, Success_AccumulatesAndRespectsWindows) {
    InMemoryStore mem;
    mem.set_api_key("demo", "secret-demo-key");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // Create three events:
    // - 2 days ago: distance 1
    // - 10 days ago: distance 2
    // - 40 days ago: distance 3
    // Using same mode so footprint scales proportionally with distance
    const std::time_t now = std::time(nullptr);
    post_transit(cli, /*distance=*/1.0, "bus",  now - 2 * 24 * 3600);
    post_transit(cli, /*distance=*/2.0, "bus",  now - 10 * 24 * 3600);
    post_transit(cli, /*distance=*/3.0, "bus",  now - 40 * 24 * 3600);

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
    EXPECT_GE(w30,  0.0);
    EXPECT_GE(w7,   0.0);

	// lifetime > 30d > 7d
    EXPECT_GT(life, w30);
    EXPECT_GT(w30,  w7);
    EXPECT_GT(w7,   0.0);
}

// -------------- Success: unaffected by other users ----------------

TEST(ApiFootprint, IgnoresOtherUsersEvents) {
    InMemoryStore mem;
    // Register demo and another user
    mem.set_api_key("demo", "secret-demo-key");
    mem.set_api_key("u_other", "k_other");
    TestServer server(mem);
    httplib::Client cli("127.0.0.1", server.port);

    // Post an event for u_other (should NOT count for demo)
    {
        json body = {{"mode","car"}, {"distance_km", 100.0}};
        httplib::Headers hdr = {{"X-API-Key","k_other"}, {"Content-Type","application/json"}};
        auto res = cli.Post("/users/u_other/transit", hdr, body.dump(), "application/json");
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->status, 201);
    }

    // Demo still has zero
    auto res2 = cli.Get("/users/demo/lifetime-footprint", demo_auth_headers());
    ASSERT_TRUE(res2 != nullptr);
    EXPECT_EQ(res2->status, 200);

    auto j = json::parse(res2->body);
    EXPECT_EQ(j.value("user_id",""), "demo");
    EXPECT_DOUBLE_EQ(j.value("lifetime_kg_co2", 123.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("last_7d_kg_co2", 123.0), 0.0);
    EXPECT_DOUBLE_EQ(j.value("last_30d_kg_co2", 123.0), 0.0);
}