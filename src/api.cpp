#include "api.hpp"

#include <ctime>
#include <nlohmann/json.hpp>
#include <regex>

using nlohmann::json;

static std::int64_t now_epoch()
{
    return std::time(nullptr);
}

static void json_response(httplib::Response& res, const json& j, int status = 200)
{
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

static bool check_auth(IStore& store, const httplib::Request& req, const std::string& user_id)
{
    auto it = req.headers.find("X-API-Key");
    if (it == req.headers.end())
        return false;
    return store.check_api_key(user_id, it->second);
}

void configure_routes(httplib::Server& svr, IStore& store)
{
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res)
            { json_response(res, { { "ok", true }, { "service", "charizard" }, { "time", now_epoch() } }); });

    svr.Post(R"(/users/([A-Za-z0-9_\-]+)/transit)",
             [&store](const httplib::Request& req, httplib::Response& res)
             {
                 std::smatch m;
                 std::regex  re(R"(/users/([A-Za-z0-9_\-]+)/transit)");
                 if (!std::regex_match(req.path, m, re) || m.size() < 2)
                     return json_response(res, { { "error", "bad_path" } }, 404);
                 const std::string user_id = m[1].str();

                 if (!check_auth(store, req, user_id))
                     return json_response(res, { { "error", "unauthorized" } }, 401);

                 nlohmann::json body;
                 try
                 {
                     body = nlohmann::json::parse(req.body);
                 }
                 catch (...)
                 {
                     return json_response(res, { { "error", "invalid_json" } }, 400);
                 }

                 if (!body.contains("mode") || !body.contains("distance_km"))
                     return json_response(res, { { "error", "missing_fields" } }, 400);

                 TransitEvent ev;
                 ev.user_id     = user_id;
                 ev.mode        = body["mode"].get<std::string>();
                 ev.distance_km = body["distance_km"].get<double>();
                 ev.ts          = body.value("ts", static_cast<std::int64_t>(now_epoch()));

                 store.add_event(ev);
                 return json_response(res, { { "status", "ok" } }, 201);
             });

    svr.Get(R"(/users/([A-Za-z0-9_\-]+)/lifetime-footprint)",
            [&store](const httplib::Request& req, httplib::Response& res)
            {
                std::smatch m;
                std::regex  re(R"(/users/([A-Za-z0-9_\-]+)/lifetime-footprint)");
                if (!std::regex_match(req.path, m, re) || m.size() < 2)
                    return json_response(res, { { "error", "bad_path" } }, 404);
                const std::string user_id = m[1].str();

                if (!check_auth(store, req, user_id))
                    return json_response(res, { { "error", "unauthorized" } }, 401);

                auto s   = store.summarize(user_id);
                json out = { { "user_id", user_id },
                             { "lifetime_kg_co2", s.lifetime_kg_co2 },
                             { "last_7d_kg_co2", s.week_kg_co2 },
                             { "last_30d_kg_co2", s.month_kg_co2 } };
                return json_response(res, out);
            });

    svr.Get(R"(/users/([A-Za-z0-9_\-]+)/suggestions)",
            [&store](const httplib::Request& req, httplib::Response& res)
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
                return json_response(res, { { "user_id", user_id }, { "suggestions", suggestions } });
            });

    svr.Get(R"(/users/([A-Za-z0-9_\-]+)/analytics)",
            [&store](const httplib::Request& req, httplib::Response& res)
            {
                std::smatch m;
                std::regex  re(R"(/users/([A-Za-z0-9_\-]+)/analytics)");
                if (!std::regex_match(req.path, m, re) || m.size() < 2)
                    return json_response(res, { { "error", "bad_path" } }, 404);
                const std::string user_id = m[1].str();
                if (!check_auth(store, req, user_id))
                    return json_response(res, { { "error", "unauthorized" } }, 401);

                auto   s        = store.summarize(user_id);
                double peer_avg = store.global_average_weekly();
                json   out      = { { "user_id", user_id },
                                    { "this_week_kg_co2", s.week_kg_co2 },
                                    { "peer_week_avg_kg_co2", peer_avg },
                                    { "above_peer_avg", s.week_kg_co2 > peer_avg } };
                return json_response(res, out);
            });

    svr.Get("/",
            [](const httplib::Request&, httplib::Response& res)
            {
                json_response(
                    res, { { "service", "charizard" },
                           { "version", "v1" },
                           { "endpoints",
                             { "/health", "/users/:id/transit (POST)", "/users/:id/lifetime-footprint (GET)",
                               "/users/:id/suggestions (GET)", "/users/:id/analytics (GET)" } } });
            });
}