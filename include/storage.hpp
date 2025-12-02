#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "emission_factors.hpp"

struct TransitEvent
{
    std::string  user_id;
    std::string  mode;         // "car", "bus", "subway", "train", "bike", "walk", "taxi"
    std::string  fuel_type;    // for car/taxi: "petrol", "diesel", "electric", "hybrid"; empty for others
    std::string  vehicle_size; // for car/taxi: "small", "medium", "large"; empty for others
    double       occupancy = 1.0; // number of passengers
    double       distance_km = 0.0;
    std::int64_t ts          = 0;
    // Default and validating constructor. Implemented in src/transit_validator.cpp
    TransitEvent() = default;
    TransitEvent(const std::string& user_id_, const std::string& mode_,
                 double distance_km_, // NOLINT(bugprone-easily-swappable-parameters)
                 std::int64_t ts_ = 0) // NOLINT(bugprone-easily-swappable-parameters)
        ;
};

struct ApiLogRecord
{
    std::int64_t ts = 0; // epoch seconds
    std::string  method;
    std::string  path;
    int          status      = 0;
    double       duration_ms = 0.0;
    std::string  client_ip;
    std::string  user_id; // empty if unknown
};

struct FootprintSummary
{
    double lifetime_kg_co2 = 0.0;
    double week_kg_co2     = 0.0;
    double month_kg_co2    = 0.0;
};

// DEFRA-based emission calculation (kg CO2e per passenger·km)
// Implemented in src/emission_calculator.cpp
double calculate_co2_emissions(const std::string& mode, const std::string& fuel_type,
                               const std::string& vehicle_size, double occupancy, double distance_km);

// Legacy helper: deprecated but kept for backward compat (used in some aggregation code paths)
inline double emission_factor_for(const std::string& mode)
{
    // This is a simplified fallback; new code should use calculate_co2_emissions
    if (mode == "taxi" || mode == "car")
        return 0.18;
    if (mode == "bus")
        return 0.08;
    if (mode == "subway" || mode == "train")
        return 0.04;
    if (mode == "bike" || mode == "walk")
        return 0.0;
    return 0.1;
}

struct IStore
{
    virtual ~IStore() = default;
    // Accepts an optional app_name metadata field. The underlying db should store a
    // hashed version of the key rather than the plaintext key for security purposes.
    virtual void set_api_key(const std::string& user, const std::string& key,
                             const std::string& app_name = "")                        = 0;
    virtual bool check_api_key(const std::string& user, const std::string& key) const = 0;
    // Logging and admin operations
    virtual void                      append_log(const ApiLogRecord& rec)                 = 0;
    virtual std::vector<ApiLogRecord> get_logs(std::size_t limit = 100) const             = 0;
    virtual void                      clear_logs()                                        = 0;
    virtual std::vector<std::string>  get_clients() const                                 = 0;
    virtual std::vector<TransitEvent> get_client_data(const std::string& client_id) const = 0;
    virtual void                      clear_db_events()                                   = 0;
    virtual void                      clear_db()                                          = 0;
    virtual void                      add_event(const TransitEvent& ev)                   = 0;
    virtual std::vector<TransitEvent> get_events(const std::string& user) const           = 0;
    virtual FootprintSummary          summarize(const std::string& user)                  = 0;
    virtual double                    global_average_weekly()                             = 0;
    // Emission factor persistence (optional implementations)
    virtual void store_emission_factor(const EmissionFactor& factor) = 0;
    virtual std::optional<EmissionFactor> get_emission_factor(const std::string& mode,
                                                              const std::string& fuel_type,
                                                              const std::string& vehicle_size) const = 0;
    virtual std::vector<EmissionFactor> get_all_emission_factors() const = 0;
    virtual void clear_emission_factors() = 0;
};

class InMemoryStore : public IStore
{
  public:
    // API key management

    void set_api_key(const std::string& user, const std::string& key,
                     const std::string& app_name = "") override
    {
        std::scoped_lock lk(mu_);
        const auto       h = hash_plain(key);
        api_keys_[user]    = h;
        if (!app_name.empty())
            app_names_[user] = app_name;
    }

    bool check_api_key(const std::string& user, const std::string& key) const override
    {
        std::scoped_lock lk(mu_);
        auto             it = api_keys_.find(user);
        if (it == api_keys_.end())
            return false;
        const auto& h = it->second;
        return hash_plain(key) == h;
    }

    // Logging and admin operations

    void append_log(const ApiLogRecord& rec) override
    {
        std::scoped_lock lk(mu_);
        logs_.push_back(rec);
    }

    std::vector<ApiLogRecord> get_logs(std::size_t limit = 100) const override
    {
        std::scoped_lock lk(mu_);
        if (logs_.empty())
            return {};
        auto start = logs_.size() > limit ? logs_.size() - limit : 0;
        return std::vector<ApiLogRecord>(logs_.begin() + start, logs_.end());
    }

    void clear_logs() override
    {
        std::scoped_lock lk(mu_);
        logs_.clear();
    }

    // Emission factor persistence
    void store_emission_factor(const EmissionFactor& factor) override
    {
        std::scoped_lock lk(mu_);
        for (auto &f : emission_factors_)
        {
            if (f.mode == factor.mode && f.fuel_type == factor.fuel_type && f.vehicle_size == factor.vehicle_size)
            {
                f = factor;
                return;
            }
        }
        emission_factors_.push_back(factor);
    }

    std::optional<EmissionFactor> get_emission_factor(const std::string& mode,
                                                      const std::string& fuel_type,
                                                      const std::string& vehicle_size) const override
    {
        std::scoped_lock lk(mu_);
        for (const auto &f : emission_factors_)
        {
            if (f.mode == mode && f.fuel_type == fuel_type && f.vehicle_size == vehicle_size)
                return f;
        }
        return std::nullopt;
    }

    std::vector<EmissionFactor> get_all_emission_factors() const override
    {
        std::scoped_lock lk(mu_);
        return emission_factors_;
    }

    void clear_emission_factors() override
    {
        std::scoped_lock lk(mu_);
        emission_factors_.clear();
    }

    std::vector<std::string> get_clients() const override
    {
        std::scoped_lock         lk(mu_);
        std::vector<std::string> out;
        out.reserve(events_.size());
        for (auto& [k, _] : events_)
            out.push_back(k);
        return out;
    }

    std::vector<TransitEvent> get_client_data(const std::string& client_id) const override
    {
        return get_events(client_id);
    }

    void clear_db_events() override
    {
        std::scoped_lock lk(mu_);
        events_.clear();
    }

    void clear_db() override
    {
        std::scoped_lock lk(mu_);
        events_.clear();
        api_keys_.clear();
        app_names_.clear();
        cache_.clear();
        logs_.clear();
        emission_factors_.clear();
    }

    // Helpers for client API calls

    void add_event(const TransitEvent& ev) override
    {
        std::scoped_lock lk(mu_);
        events_[ev.user_id].push_back(ev);
        // invalidate tiny cache
        cache_.erase(ev.user_id);
    }

    std::vector<TransitEvent> get_events(const std::string& user) const override
    {
        std::scoped_lock lk(mu_);
        auto             it = events_.find(user);
        if (it == events_.end())
            return {};
        return it->second;
    }

    FootprintSummary summarize(const std::string& user) override
    {
        using clock = std::chrono::system_clock;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count();

        std::scoped_lock lk(mu_);

        // serve cache if present (very simple)
        auto itc = cache_.find(user);
        if (itc != cache_.end())
            return itc->second;

        FootprintSummary s{};
        auto             it = events_.find(user);
        if (it == events_.end())
            return s;

        auto week_start  = now - (7 * 24 * 3600);
        auto month_start = now - (30 * 24 * 3600);

        for (const auto& ev : it->second)
        {
            double kg = emission_factor_for(ev.mode) * ev.distance_km;
            s.lifetime_kg_co2 += kg;
            if (ev.ts >= week_start)
                s.week_kg_co2 += kg;
            if (ev.ts >= month_start)
                s.month_kg_co2 += kg;
        }

        cache_[user] = s; // tiny cache
        return s;
    }

    // naive anonymized aggregate (just totals)
    double global_average_weekly() override
    {
        using clock = std::chrono::system_clock;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count();

        std::scoped_lock lk(mu_);
        std::size_t      users_with_data = 0;
        double           total           = 0.0;
        auto             week_start      = now - (7 * 24 * 3600);

        for (auto& [user, vec] : events_)
        {
            double u_week = 0.0;
            bool   has    = false;
            for (auto& ev : vec)
            {
                if (ev.ts >= week_start)
                {
                    u_week += emission_factor_for(ev.mode) * ev.distance_km;
                    has = true;
                }
            }
            if (has)
            {
                total += u_week;
                users_with_data++;
            }
        }
        if (users_with_data == 0)
            return 0.0;
        return total / static_cast<double>(users_with_data);
    }

  private:
    mutable std::mutex                                         mu_;
    std::unordered_map<std::string, std::string>               api_keys_;
    std::unordered_map<std::string, std::string>               app_names_;
    std::unordered_map<std::string, std::vector<TransitEvent>> events_;
    std::unordered_map<std::string, FootprintSummary>          cache_;
    std::vector<ApiLogRecord>                                  logs_;
        std::vector<EmissionFactor>                                 emission_factors_;

    static std::string hash_plain(const std::string& key)
    {
        // TO-DO: replace std::hash with something stronger later (eg. Argon2/bcrypt)
        std::hash<std::string> h;
        auto                   v = h(key);
        std::ostringstream     oss;
        oss << std::hex << v;
        return oss.str();
    }
};