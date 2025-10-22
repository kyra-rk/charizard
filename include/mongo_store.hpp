#pragma once
#include "storage.hpp"

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <chrono>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/uri.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class MongoStore : public IStore
{
  public:
    explicit MongoStore(std::string uri, std::string dbname = "charizard")
        : instance_{}, client_{ mongocxx::uri{ uri } }, db_{ client_[dbname] }
    {
    }

    void set_api_key(const std::string& user, const std::string& key,
                     const std::string& app_name = "") override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;
        // Store a hashed API key.
        std::hash<std::string> hsh;
        auto                    h = [&hsh](const std::string& k) {
            std::ostringstream oss;
            oss << std::hex << hsh(k);
            return oss.str();
        }(key);

        auto coll = db_["api_keys"];
        // Persist the hash and optional app_name metadata.
        coll.update_one(make_document(kvp("_id", user)),
                        make_document(kvp("$set", make_document(kvp("api_key_hash", h), kvp("app_name", app_name)))),
                        mongocxx::options::update{}.upsert(true));
    }

    bool check_api_key(const std::string& user, const std::string& key) const override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;

        auto coll = db_["api_keys"];
        auto doc  = coll.find_one(make_document(kvp("_id", user)));
        if (!doc)
            return false;

        auto view = doc->view();
        auto it_hash = view.find("api_key_hash");
        if (it_hash == view.end())
            return false;
        const auto stored_hash = std::string{ it_hash->get_string().value };
        std::hash<std::string> h;
        std::ostringstream      oss;
        oss << std::hex << h(key);
        return oss.str() == stored_hash;
    }

    void add_event(const TransitEvent& ev) override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;

        auto coll = db_["events"];
        coll.insert_one(make_document(kvp("user_id", ev.user_id), kvp("mode", ev.mode),
                                      kvp("distance_km", ev.distance_km),
                                      kvp("ts", static_cast<long long>(ev.ts))));
    }

    std::vector<TransitEvent> get_events(const std::string& user) const override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;

        std::vector<TransitEvent> out;
        auto                      coll = db_["events"];

        mongocxx::options::find opts;
        opts.sort(make_document(kvp("ts", 1)));

        auto cursor = coll.find(make_document(kvp("user_id", user)), opts);
        for (auto&& d : cursor)
        {
            TransitEvent e;
            e.user_id     = user;
            e.mode        = std::string{ d["mode"].get_string().value };
            e.distance_km = d["distance_km"].get_double();
            e.ts          = static_cast<std::int64_t>(d["ts"].get_int64().value);
            out.push_back(std::move(e));
        }
        return out;
    }

    FootprintSummary summarize(const std::string& user) override
    {
        using clock = std::chrono::system_clock;
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count();
        const auto week_start  = now - 7 * 24 * 3600;
        const auto month_start = now - 30 * 24 * 3600;

        FootprintSummary s{};
        for (const auto& ev : get_events(user))
        {
            const double kg = emission_factor_for(ev.mode) * ev.distance_km;
            s.lifetime_kg_co2 += kg;
            if (ev.ts >= week_start)
                s.week_kg_co2 += kg;
            if (ev.ts >= month_start)
                s.month_kg_co2 += kg;
        }
        return s;
    }

    double global_average_weekly() override
    {
        using clock = std::chrono::system_clock;
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count();
        const auto week_start = now - 7 * 24 * 3600;

        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;

        auto                                    coll = db_["events"];
        std::unordered_map<std::string, double> user_week;

        auto cursor = coll.find(
            make_document(kvp("ts", make_document(kvp("$gte", static_cast<long long>(week_start))))));
        for (auto&& d : cursor)
        {
            const auto user = std::string{ d["user_id"].get_string().value };
            const auto mode = std::string{ d["mode"].get_string().value };
            const auto dist = d["distance_km"].get_double();
            user_week[user] += emission_factor_for(mode) * dist;
        }

        if (user_week.empty())
            return 0.0;
        double tot = 0.0;
        for (auto& [_, v] : user_week)
            tot += v;
        return tot / static_cast<double>(user_week.size());
    }

  private:
    mutable mongocxx::instance instance_;
    mongocxx::client           client_;
    mongocxx::database         db_;
};