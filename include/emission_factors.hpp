#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Represents a single emission factor entry.
 * Stores CO2 emissions data indexed by mode, fuel type, and vehicle size.
 */
struct EmissionFactor
{
    std::string mode;          // "car", "bus", "subway", "train", "bike", "walk", "taxi"
    std::string fuel_type;     // for car/taxi: "petrol", "diesel", "electric", "hybrid"; empty for others
    std::string vehicle_size;  // for car/taxi: "small", "medium", "large"; empty for others
    double      kg_co2_per_km; // per-passenger kg CO2e per km
    std::string source;        // e.g., "DEFRA-2024", "EPA-2023"
    std::int64_t updated_at = 0; // epoch seconds when this was last updated
};

/**
 * Interface for managing emission factors in persistent storage.
 * Implementations should support loading factors from external sources and caching locally.
 */
class IEmissionFactorStore
{
  public:
    virtual ~IEmissionFactorStore() = default;

    // Store or update a single emission factor
    virtual void store_factor(const EmissionFactor& factor) = 0;

    // Retrieve a factor by mode, fuel_type, vehicle_size
    // Returns std::nullopt if not found
    virtual std::optional<EmissionFactor> get_factor(const std::string& mode,
                                                       const std::string& fuel_type,
                                                       const std::string& vehicle_size) const = 0;

    // Retrieve all factors for a given mode
    virtual std::vector<EmissionFactor> get_factors_by_mode(const std::string& mode) const = 0;

    // Retrieve all stored factors
    virtual std::vector<EmissionFactor> get_all_factors() const = 0;

    // Clear all factors (useful when reloading from source)
    virtual void clear_factors() = 0;

    // Check if a factor exists
    virtual bool has_factor(const std::string& mode, const std::string& fuel_type,
                            const std::string& vehicle_size) const = 0;

    // Get the count of stored factors
    virtual std::size_t factor_count() const = 0;
};

/**
 * Default emission factors with multiple precision levels.
 * basic_defaults(): Simple conservative factors, used until detailed data loads
 * defra_2024_factors(): Full DEFRA 2024 UK Government factors for production use
 * Source: https://www.gov.uk/guidance/greenhouse-gas-reporting-conversion-factors-2024
 */
class DefaultEmissionFactors
{
  public:
    /**
     * Returns basic default factors (simplified, conservative estimates).
     * Used on startup and as fallback when detailed factors not available.
     */
    static std::vector<EmissionFactor> basic_defaults();

    /**
     * Returns detailed DEFRA 2024 emission factors.
     * These are well-to-wheel factors including fuel production.
     * More accurate than basic_defaults(), but only use when explicitly loaded.
     */
    static std::vector<EmissionFactor> defra_2024_factors();

    /**
     * Returns default factor for a mode.
     * Looks up in basic_defaults (not DEFRA).
     * Used as fallback when specific factor not found in database.
     */
    static std::optional<EmissionFactor> get_default_factor(const std::string& mode,
                                                             const std::string& fuel_type,
                                                             const std::string& vehicle_size);
};
