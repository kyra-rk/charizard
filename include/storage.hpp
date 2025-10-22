#pragma once
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct TransitEvent
{
    std::string  user_id;
    std::string  mode;
    double       distance_km = 0.0;
    std::int64_t ts          = 0;
};

struct FootprintSummary
{
    double lifetime_kg_co2 = 0.0;
    double week_kg_co2     = 0.0;
    double month_kg_co2    = 0.0;
};

// Extremely simplified “emission factors” (kg CO2 per km). Replace with real data later.
inline double emission_factor_for(const std::string& mode)
{
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
    virtual ~IStore()                                                                 = default;
    virtual void set_api_key(const std::string& user, const std::string& key)         = 0;
    virtual bool check_api_key(const std::string& user, const std::string& key) const = 0;
    virtual void add_event(const TransitEvent& ev)                                    = 0;
    virtual std::vector<TransitEvent> get_events(const std::string& user) const       = 0;
    virtual FootprintSummary          summarize(const std::string& user)              = 0;
    virtual double                    global_average_weekly()                         = 0;
};

class InMemoryStore : public IStore
{
  public:
    // Upsert API key for a user (for demo)
    void set_api_key(const std::string& user, const std::string& key)
    {
        std::scoped_lock lk(mu_);
        api_keys_[user] = key;
    }

    bool check_api_key(const std::string& user, const std::string& key) const
    {
        std::scoped_lock lk(mu_);
        auto             it = api_keys_.find(user);
        return (it != api_keys_.end() && it->second == key);
    }

    void add_event(const TransitEvent& ev)
    {
        std::scoped_lock lk(mu_);
        events_[ev.user_id].push_back(ev);
        // invalidate tiny cache
        cache_.erase(ev.user_id);
    }

    std::vector<TransitEvent> get_events(const std::string& user) const
    {
        std::scoped_lock lk(mu_);
        auto             it = events_.find(user);
        if (it == events_.end())
            return {};
        return it->second;
    }

    FootprintSummary summarize(const std::string& user)
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

        auto week_start  = now - 7 * 24 * 3600;
        auto month_start = now - 30 * 24 * 3600;

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
    double global_average_weekly()
    {
        using clock = std::chrono::system_clock;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count();

        std::scoped_lock lk(mu_);
        std::size_t      users_with_data = 0;
        double           total           = 0.0;
        auto             week_start      = now - 7 * 24 * 3600;

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
    std::unordered_map<std::string, std::vector<TransitEvent>> events_;
    std::unordered_map<std::string, FootprintSummary>          cache_;
};