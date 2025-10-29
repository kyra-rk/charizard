#include "api.hpp"

#include "storage.hpp"

#include <cstdlib>
#include <ctime>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

using nlohmann::json;

static std::int64_t now_epoch()
{
    return std::time(nullptr);
}

static void json_response(httplib::Response& res, const json& j,
                          int status = 200) // NOLINT(bugprone-easily-swappable-parameters)
{
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

static bool check_auth(IStore& store, const httplib::Request& req, const std::string& user_id)
{
    auto it = req.headers.find("X-API-Key");
    if (it == req.headers.end())
    {
        return false;
    }
    return store.check_api_key(user_id, it->second);
}

static bool check_admin(const httplib::Request& req)
{
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end())
    {
        return false;
    }
    const std::string prefix = "Bearer ";
    if (it->second.rfind(prefix, 0) != 0)
    {
        return false;
    }
    const char* env_key = std::getenv("ADMIN_API_KEY");
    if (env_key == nullptr)
    {
        return false;
    }
    const std::string token = it->second.substr(prefix.size());
    return token == std::string(env_key);
}

// Helper to log a completed request
static void record_log(IStore& store, const httplib::Request& req, const httplib::Response& res,
                       const std::string& user_id, std::int64_t start_ts,
                       double duration_ms) // NOLINT(bugprone-easily-swappable-parameters)
{
    ApiLogRecord r;
    r.ts          = start_ts;
    r.method      = req.method;
    r.path        = req.path;
    r.status      = res.status;
    r.duration_ms = duration_ms;
    r.client_ip   = req.remote_addr.empty() ? std::string("unknown") : req.remote_addr;
    r.user_id     = user_id;
    store.append_log(r);
}

void configure_routes(httplib::Server& svr, IStore& store)
{
    // Health
    svr.Get("/health",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                const auto start = now_epoch();
                json_response(res, { { "ok", true }, { "service", "charizard" }, { "time", start } });
                record_log(store, req, res, "", start, 0.0);
            });

    // Register
    svr.Post("/users/register",
             [&](const httplib::Request& req, httplib::Response& res)
             {
                 nlohmann::json body;
                 try
                 {
                     body = nlohmann::json::parse(req.body);
                 }
                 catch (const std::exception&)
                 {
                     return json_response(res, { { "error", "invalid_json" } }, 400);
                 }
                 if (!body.contains("app_name") || !body["app_name"].is_string())
                     return json_response(res, { { "error", "missing_app_name" } }, 400);
                 const std::string app_name = body["app_name"].get<std::string>();

                 auto rnd_hex = [](size_t len)
                 {
                     std::random_device                                rd;
                     std::uniform_int_distribution<unsigned long long> dist(0, ULLONG_MAX);
                     std::ostringstream                                oss;
                     while (oss.str().size() < len)
                         oss << std::hex << dist(rd);
                     auto s = oss.str();
                     return s.substr(0, len);
                 };

                 const std::string user_id = std::string("u_") + rnd_hex(8);
                 const std::string api_key = rnd_hex(32);
                 store.set_api_key(user_id, api_key, app_name);

                 json out = { { "user_id", user_id }, { "api_key", api_key }, { "app_name", app_name } };
                 json_response(res, out, 201);
                 record_log(store, req, res, user_id, now_epoch(), 0.0);
             });

    // Transit
    svr.Post(R"(/users/([A-Za-z0-9_\-]+)/transit)",
             [&](const httplib::Request& req, httplib::Response& res)
             {
                 std::smatch m;
                 std::regex  re(R"(/users/([A-Za-z0-9_\-]+)/transit)");
                 if (!std::regex_match(req.path, m, re) || m.size() < 2)
                     return json_response(res, { { "error", "bad_path" } }, 404);
                 const std::string user_id = m[1].str();

                 const auto start = now_epoch();
                 if (!check_auth(store, req, user_id))
                     return json_response(res, { { "error", "unauthorized" } }, 401);

                 nlohmann::json body;
                 try
                 {
                     body = nlohmann::json::parse(req.body);
                 }
                 catch (const std::exception&)
                 {
                     return json_response(res, { { "error", "invalid_json" } }, 400);
                 }
                 // TO-DO: we should probably have an enum on this `mode` field...
                 // if (!body.contains("mode") || !body.contains("distance_km")) return json_response(res,
                 // {{"error","missing_fields"}}, 400);

                 // TransitEvent ev; ev.user_id = user_id; ev.mode = body["mode"].get<std::string>();
                 // ev.distance_km = body["distance_km"].get<double>(); ev.ts = body.value("ts",
                 // static_cast<std::int64_t>(now_epoch()));
                 try
                 {
                     if (!body.contains("mode") || !body.contains("distance_km"))
                         return json_response(res, { { "error", "missing_fields" } }, 400);

                     TransitEvent ev(user_id, body["mode"].get<std::string>(),
                                     body["distance_km"].get<double>(),
                                     body.value("ts", static_cast<std::int64_t>(now_epoch())));

                     store.add_event(ev);
                 }
                 catch (const std::runtime_error& e)
                 {
                     return json_response(res, { { "error", e.what() } }, 400);
                 }
                 catch (const nlohmann::json::exception&)
                 {
                     return json_response(res, { { "error", "invalid JSON payload" } }, 400);
                 }

                 // store.add_event(ev);
                 json_response(res, { { "status", "ok" } }, 201);
                 const auto end = now_epoch();
                 record_log(store, req, res, user_id, start, static_cast<double>((end - start) * 1000));
             });

    // svr.Post(R"(/users/([A-Za-z0-9_\-]+)/transit)", [&](const httplib::Request& req, httplib::Response&
    // res) {
    // /* regex, auth, and parameter checks */
    // try {
    //     if (!body.contains("mode") || !body.contains("distance_km"))
    //     return json_response(res, {{"error","missing required fields"}}, 400);

    //     TransitEvent ev(
    //     user_id,
    //     body["mode"].get<std::string>(),
    //     body["distance_km"].get<double>(),
    //     body.value("ts", static_cast<std::int64_t>(now_epoch()))
    //     );

    //     store->add_event(ev);
    // } catch (const std::runtime_error& e) {
    //     return json_response(res, {{"error", e.what()}}, 400);
    // } catch (const nlohmann::json::exception&) {
    //     return json_response(res, {{"error","invalid JSON payload"}}, 400);
    // }

    // return json_response(res, {{"status","ok"}}, 201);
    // });

    // Lifetime
    svr.Get(R"(/users/([A-Za-z0-9_\-]+)/lifetime-footprint)",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                std::smatch m;
                std::regex  re(R"(/users/([A-Za-z0-9_\-]+)/lifetime-footprint)");
                if (!std::regex_match(req.path, m, re) || m.size() < 2)
                    return json_response(res, { { "error", "bad_path" } }, 404);
                const std::string user_id = m[1].str();
                const auto        start   = now_epoch();
                if (!check_auth(store, req, user_id))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                auto s   = store.summarize(user_id);
                json out = { { "user_id", user_id },
                             { "lifetime_kg_co2", s.lifetime_kg_co2 },
                             { "last_7d_kg_co2", s.week_kg_co2 },
                             { "last_30d_kg_co2", s.month_kg_co2 } };
                json_response(res, out);
                const auto end = now_epoch();
                record_log(store, req, res, user_id, start, static_cast<double>((end - start) * 1000));
            });

    // Suggestions
    svr.Get(R"(/users/([A-Za-z0-9_\-]+)/suggestions)",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                std::smatch m;
                std::regex  re(R"(/users/([A-Za-z0-9_\-]+)/suggestions)");
                if (!std::regex_match(req.path, m, re) || m.size() < 2)
                    return json_response(res, { { "error", "bad_path" } }, 404);
                const std::string user_id = m[1].str();
                if (!check_auth(store, req, user_id))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                auto s           = store.summarize(user_id);
                json suggestions = json::array();
                if (s.week_kg_co2 > 20.0)
                {
                    suggestions.push_back("Try switching short taxi rides to subway or bus.");
                    suggestions.push_back("Batch trips to reduce total distance.");
                }
                else
                {
                    suggestions.push_back("Nice work! Consider biking or walking for short hops.");
                }
                json_response(res, { { "user_id", user_id }, { "suggestions", suggestions } });
                record_log(store, req, res, user_id, now_epoch(), 0.0);
            });

    // Analytics
    svr.Get(R"(/users/([A-Za-z0-9_\-]+)/analytics)",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                std::smatch m;
                std::regex  re(R"(/users/([A-Za-z0-9_\-]+)/analytics)");
                if (!std::regex_match(req.path, m, re) || m.size() < 2)
                    return json_response(res, { { "error", "bad_path" } }, 404);
                const std::string user_id = m[1].str();
                if (!check_auth(store, req, user_id))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                const auto start    = now_epoch();
                auto       s        = store.summarize(user_id);
                double     peer_avg = store.global_average_weekly();
                json       out      = { { "user_id", user_id },
                                        { "this_week_kg_co2", s.week_kg_co2 },
                                        { "peer_week_avg_kg_co2", peer_avg },
                                        { "above_peer_avg", s.week_kg_co2 > peer_avg } };
                json_response(res, out);
                const auto end = now_epoch();
                record_log(store, req, res, user_id, start, static_cast<double>((end - start) * 1000));
            });

    // Admin endpoints
    svr.Get("/admin/logs",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                if (!check_admin(req))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                auto logs = store.get_logs(1000);
                json arr  = json::array();
                for (auto& l : logs)
                    arr.push_back({ { "ts", l.ts },
                                    { "method", l.method },
                                    { "path", l.path },
                                    { "status", l.status },
                                    { "duration_ms", l.duration_ms },
                                    { "client_ip", l.client_ip },
                                    { "user_id", l.user_id } });
                json_response(res, arr);
            });

    svr.Delete("/admin/logs",
               [&](const httplib::Request& req, httplib::Response& res)
               {
                   if (!check_admin(req))
                       return json_response(res, { { "error", "unauthorized" } }, 401);
                   store.clear_logs();
                   json_response(res, { { "status", "ok" } });
               });

    svr.Get("/admin/clients",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                if (!check_admin(req))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                auto clients = store.get_clients();
                json_response(res, clients);
            });

    svr.Get(R"(/admin/clients/([A-Za-z0-9_\-]+)/data)",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                if (!check_admin(req))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                std::smatch m;
                std::regex  re(R"(/admin/clients/([A-Za-z0-9_\-]+)/data)");
                if (!std::regex_match(req.path, m, re) || m.size() < 2)
                    return json_response(res, { { "error", "bad_path" } }, 404);
                const std::string client_id = m[1].str();
                auto              data      = store.get_client_data(client_id);
                json              arr       = json::array();
                for (auto& e : data)
                    arr.push_back({ { "mode", e.mode }, { "distance_km", e.distance_km }, { "ts", e.ts } });
                json_response(res, arr);
            });

    svr.Get("/admin/clear-db-events",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                if (!check_admin(req))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                store.clear_db_events();
                json_response(res, { { "status", "ok" } });
            });

    svr.Get("/admin/clear-db",
            [&](const httplib::Request& req, httplib::Response& res)
            {
                if (!check_admin(req))
                    return json_response(res, { { "error", "unauthorized" } }, 401);
                store.clear_db();
                json_response(res, { { "status", "ok" } });
            });
}
