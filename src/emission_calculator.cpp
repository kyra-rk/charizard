#include "storage.hpp"
#include "emission_factors.hpp"

#include <stdexcept>
#include <cmath>

double calculate_co2_emissions(const std::string& mode, const std::string& fuel_type,
                               const std::string& vehicle_size, double occupancy, double distance_km)
{
    // Validate inputs
    if (distance_km < 0.0)
    {
        throw std::runtime_error("Distance cannot be negative");
    }

    if (occupancy < 1.0)
    {
        throw std::runtime_error("Occupancy must be at least 1.0");
    }

    // Get the appropriate emission factor
    // First try to get from database/external sources via DefaultEmissionFactors
    std::optional<EmissionFactor> factor_opt = DefaultEmissionFactors::get_default_factor(mode, fuel_type, vehicle_size);

    if (!factor_opt)
    {
        // Fallback to simplified defaults if not found
        if (mode == "car" || mode == "taxi")
        {
            factor_opt = EmissionFactor{ mode, fuel_type, vehicle_size, 0.18, "FALLBACK", 0 };
        }
        else if (mode == "bus")
        {
            factor_opt = EmissionFactor{ mode, "", "", 0.073, "FALLBACK", 0 };
        }
        else if (mode == "subway" || mode == "train" || mode == "underground" || mode == "rail")
        {
            factor_opt = EmissionFactor{ mode, "", "", 0.041, "FALLBACK", 0 };
        }
        else if (mode == "bike" || mode == "walk")
        {
            factor_opt = EmissionFactor{ mode, "", "", 0.0, "FALLBACK", 0 };
        }
        else
        {
            // Unknown mode, use conservative estimate
            factor_opt = EmissionFactor{ mode, "", "", 0.1, "FALLBACK", 0 };
        }
    }

    const auto& factor = factor_opt.value();

    // Calculate total CO2 emissions
    double total_kg_co2 = factor.kg_co2_per_km * distance_km;

    // For private vehicles (car, taxi), divide by occupancy (sharing emissions)
    // For public transit, occupancy already factored into the per-passenger factor
    if (mode == "car" || mode == "taxi")
    {
        total_kg_co2 = total_kg_co2 / occupancy;
    }

    return total_kg_co2;
}
