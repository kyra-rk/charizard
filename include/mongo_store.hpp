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
#include <unordered_set>
#include <vector>

class MongoStore : public IStore
{
  public:
    explicit MongoStore(std::string uri, std::string dbname = "charizard")
        : instance_{}, client_{ mongocxx::uri{ uri } }, db_{ client_[dbname] }
    {
    }

    // API key management

    void set_api_key(const std::string& user, const std::string& key,
                     const std::string& app_name = "") override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;
        // Store a hashed API key.
        std::hash<std::string> hsh;
        auto                   h = [&hsh](const std::string& k)
        {
            std::ostringstream oss;
            oss << std::hex << hsh(k);
            return oss.str();
        }(key);

        auto coll = db_["api_keys"];
        // Persist the hash and optional app_name metadata.
        coll.update_one(
            make_document(kvp("_id", user)),
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

        auto view    = doc->view();
        auto it_hash = view.find("api_key_hash");
        if (it_hash == view.end())
            return false;
        const auto             stored_hash = std::string{ it_hash->get_string().value };
        std::hash<std::string> h;
        std::ostringstream     oss;
        oss << std::hex << h(key);
        return oss.str() == stored_hash;
    }

    // Logging and admin operations

    void append_log(const ApiLogRecord& rec) override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;
        auto coll = db_["api_logs"];
        coll.insert_one(make_document(kvp("ts", static_cast<long long>(rec.ts)), kvp("method", rec.method),
                                      kvp("path", rec.path), kvp("status", rec.status),
                                      kvp("duration_ms", rec.duration_ms), kvp("client_ip", rec.client_ip),
                                      kvp("user_id", rec.user_id)));
    }

    std::vector<ApiLogRecord> get_logs(std::size_t limit = 100) const override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;
        std::vector<ApiLogRecord> out;
        auto                      coll = db_["api_logs"];
        mongocxx::options::find   opts;
        opts.sort(make_document(kvp("ts", 1)));
        opts.limit(static_cast<std::int64_t>(limit));
        auto cursor = coll.find({}, opts);
        for (auto&& d : cursor)
        {
            ApiLogRecord r;
            r.ts          = static_cast<std::int64_t>(d["ts"].get_int64().value);
            r.method      = std::string{ d["method"].get_string().value };
            r.path        = std::string{ d["path"].get_string().value };
            r.status      = static_cast<int>(d["status"].get_int32().value);
            r.duration_ms = d["duration_ms"].get_double();
            r.client_ip   = std::string{ d["client_ip"].get_string().value };
            r.user_id     = std::string{ d["user_id"].get_string().value };
            out.push_back(std::move(r));
        }
        return out;
    }

    void clear_logs() override
    {
        auto coll = db_["api_logs"];
        coll.delete_many({});
    }

    std::vector<std::string> get_clients() const override
    {
        std::vector<std::string> out;
        auto                     coll = db_["events"];
        // naive: iterate events and collect distinct user_id
        std::unordered_set<std::string> seen;
        auto                            cursor = coll.find({});
        for (auto&& d : cursor)
        {
            const auto uid = std::string{ d["user_id"].get_string().value };
            if (seen.insert(uid).second)
                out.push_back(uid);
        }
        return out;
    }

    std::vector<TransitEvent> get_client_data(const std::string& client_id) const override
    {
        return get_events(client_id);
    }

    void clear_db_events() override
    {
        auto coll = db_["events"];
        coll.delete_many({});
    }

    void clear_db() override
    {
        db_["events"].delete_many({});
        db_["api_keys"].delete_many({});
        db_["api_logs"].delete_many({});
        db_["emission_factors"].delete_many({});
    }

    // Helpers for client API calls

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

    // Emission factor persistence
    void store_emission_factor(const EmissionFactor& factor) override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;
        auto coll = db_["emission_factors"];
        // Use a compound key as _id: mode|fuel_type|vehicle_size
        std::string id = factor.mode + "|" + factor.fuel_type + "|" + factor.vehicle_size;
        mongocxx::options::update opts;
        opts.upsert(true);

        bsoncxx::builder::basic::document filter_doc{};
        filter_doc.append(bsoncxx::builder::basic::kvp("_id", id));

        bsoncxx::builder::basic::document set_doc{};
        set_doc.append(bsoncxx::builder::basic::kvp("mode", factor.mode));
        set_doc.append(bsoncxx::builder::basic::kvp("fuel_type", factor.fuel_type));
        set_doc.append(bsoncxx::builder::basic::kvp("vehicle_size", factor.vehicle_size));
        set_doc.append(bsoncxx::builder::basic::kvp("kg_co2_per_km", factor.kg_co2_per_km));
        set_doc.append(bsoncxx::builder::basic::kvp("source", factor.source));
        set_doc.append(bsoncxx::builder::basic::kvp("updated_at", static_cast<long long>(factor.updated_at)));

        bsoncxx::builder::basic::document update_doc{};
        update_doc.append(bsoncxx::builder::basic::kvp("$set", set_doc.extract()));

        coll.update_one(filter_doc.extract(), update_doc.extract(), opts);
    }

    std::optional<EmissionFactor> get_emission_factor(const std::string& mode,
                                                      const std::string& fuel_type,
                                                      const std::string& vehicle_size) const override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;
        auto coll = db_["emission_factors"];
        std::string id = mode + "|" + fuel_type + "|" + vehicle_size;
        auto doc = coll.find_one(make_document(kvp("_id", id)));
        if (!doc)
            return std::nullopt;
        auto view = doc->view();
        EmissionFactor f;
        f.mode = std::string{ view["mode"].get_string().value };
        f.fuel_type = std::string{ view["fuel_type"].get_string().value };
        f.vehicle_size = std::string{ view["vehicle_size"].get_string().value };
        f.kg_co2_per_km = view["kg_co2_per_km"].get_double();
        f.source = std::string{ view["source"].get_string().value };
        f.updated_at = static_cast<std::int64_t>(view["updated_at"].get_int64().value);
        return f;
    }

    std::vector<EmissionFactor> get_all_emission_factors() const override
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;
        std::vector<EmissionFactor> out;
        auto coll = db_["emission_factors"];
        auto cursor = coll.find({});
        for (auto&& d : cursor)
        {
            EmissionFactor f;
            f.mode = std::string{ d["mode"].get_string().value };
            f.fuel_type = std::string{ d["fuel_type"].get_string().value };
            f.vehicle_size = std::string{ d["vehicle_size"].get_string().value };
            f.kg_co2_per_km = d["kg_co2_per_km"].get_double();
            f.source = std::string{ d["source"].get_string().value };
            f.updated_at = static_cast<std::int64_t>(d["updated_at"].get_int64().value);
            out.push_back(std::move(f));
        }
        return out;
    }

    void clear_emission_factors() override
    {
        auto coll = db_["emission_factors"];
        coll.delete_many({});
    }

  private:
    mutable mongocxx::instance instance_;
    mongocxx::client           client_;
    mongocxx::database         db_;
};