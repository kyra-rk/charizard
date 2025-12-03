#include "emission_factors.hpp"

std::vector<EmissionFactor> DefaultEmissionFactors::basic_defaults()
{
    // Simple, conservative default emission factors (used until DEFRA data is loaded)
    // These are basic approximations to provide a working service without external data

    std::vector<EmissionFactor> factors;

    // ===== CARS (simplified: all sizes use same factor) =====
    factors.push_back({ "car", "petrol", "small", 0.200, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "petrol", "medium", 0.200, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "petrol", "large", 0.200, "BASIC-DEFAULT", 0 });

    factors.push_back({ "car", "diesel", "small", 0.180, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "diesel", "medium", 0.180, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "diesel", "large", 0.180, "BASIC-DEFAULT", 0 });

    factors.push_back({ "car", "electric", "small", 0.100, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "electric", "medium", 0.100, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "electric", "large", 0.100, "BASIC-DEFAULT", 0 });

    factors.push_back({ "car", "hybrid", "small", 0.150, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "hybrid", "medium", 0.150, "BASIC-DEFAULT", 0 });
    factors.push_back({ "car", "hybrid", "large", 0.150, "BASIC-DEFAULT", 0 });

    // ===== TAXIS (simplified) =====
    factors.push_back({ "taxi", "petrol", "medium", 0.200, "BASIC-DEFAULT", 0 });
    factors.push_back({ "taxi", "diesel", "medium", 0.180, "BASIC-DEFAULT", 0 });
    factors.push_back({ "taxi", "electric", "medium", 0.100, "BASIC-DEFAULT", 0 });
    factors.push_back({ "taxi", "hybrid", "medium", 0.150, "BASIC-DEFAULT", 0 });

    // ===== PUBLIC TRANSIT (simplified estimates) =====
    factors.push_back({ "bus", "", "", 0.100, "BASIC-DEFAULT", 0 });
    factors.push_back({ "subway", "", "", 0.050, "BASIC-DEFAULT", 0 });
    factors.push_back({ "train", "", "", 0.070, "BASIC-DEFAULT", 0 });

    // ===== ZERO-EMISSION MODES =====
    factors.push_back({ "bike", "", "", 0.0, "BASIC-DEFAULT", 0 });
    factors.push_back({ "walk", "", "", 0.0, "BASIC-DEFAULT", 0 });

    return factors;
}

std::vector<EmissionFactor> DefaultEmissionFactors::defra_2024_factors()
{
    // DEFRA 2024 UK Government Greenhouse Gas Conversion Factors
    // Source: https://www.gov.uk/guidance/greenhouse-gas-reporting-conversion-factors-2024
    // All factors in kg CO2e per passenger·km (well-to-wheel, including fuel production)

    std::vector<EmissionFactor> factors;

    // ===== CARS (private vehicles, per passenger with occupancy adjustment) =====
    // Petrol cars
    factors.push_back({ "car", "petrol", "small", 0.167, "DEFRA-2024", 0 });
    factors.push_back({ "car", "petrol", "medium", 0.203, "DEFRA-2024", 0 });
    factors.push_back({ "car", "petrol", "large", 0.291, "DEFRA-2024", 0 });

    // Diesel cars (more efficient than petrol for same size)
    factors.push_back({ "car", "diesel", "small", 0.142, "DEFRA-2024", 0 });
    factors.push_back({ "car", "diesel", "medium", 0.168, "DEFRA-2024", 0 });
    factors.push_back({ "car", "diesel", "large", 0.241, "DEFRA-2024", 0 });

    // Electric cars (much lower; reflects 2024 UK grid ~50% renewable)
    factors.push_back({ "car", "electric", "small", 0.074, "DEFRA-2024", 0 });
    factors.push_back({ "car", "electric", "medium", 0.088, "DEFRA-2024", 0 });
    factors.push_back({ "car", "electric", "large", 0.115, "DEFRA-2024", 0 });

    // Hybrid cars (between electric and petrol)
    factors.push_back({ "car", "hybrid", "small", 0.132, "DEFRA-2024", 0 });
    factors.push_back({ "car", "hybrid", "medium", 0.155, "DEFRA-2024", 0 });
    factors.push_back({ "car", "hybrid", "large", 0.210, "DEFRA-2024", 0 });

    // ===== TAXIS (similar to cars, but often higher occupancy) =====
    // For taxis, use same factors as cars (occupancy adjustment applied at calculation time)
    factors.push_back({ "taxi", "petrol", "medium", 0.203, "DEFRA-2024", 0 });
    factors.push_back({ "taxi", "diesel", "medium", 0.168, "DEFRA-2024", 0 });
    factors.push_back({ "taxi", "electric", "medium", 0.088, "DEFRA-2024", 0 });
    factors.push_back({ "taxi", "hybrid", "medium", 0.155, "DEFRA-2024", 0 });

    // ===== PUBLIC TRANSIT (per passenger, already averaged) =====
    // Bus (averaged across ~40 passengers per vehicle)
    factors.push_back({ "bus", "", "", 0.073, "DEFRA-2024", 0 });

    // Underground/Subway (electric, very efficient)
    factors.push_back({ "subway", "", "", 0.041, "DEFRA-2024", 0 });

    // Train/Rail (mix of electric and diesel)
    factors.push_back({ "train", "", "", 0.051, "DEFRA-2024", 0 });

    // ===== ZERO-EMISSION MODES =====
    factors.push_back({ "bike", "", "", 0.0, "DEFRA-2024", 0 });
    factors.push_back({ "walk", "", "", 0.0, "DEFRA-2024", 0 });

    return factors;
}

std::optional<EmissionFactor> DefaultEmissionFactors::get_default_factor(const std::string& mode,
                                                                         const std::string& fuel_type,
                                                                         const std::string& vehicle_size)
{
    // Return DEFRA 2024 factors (the detailed ones)
    const auto all_factors = defra_2024_factors();
    for (const auto& factor : all_factors)
    {
        if (factor.mode == mode && factor.fuel_type == fuel_type && factor.vehicle_size == vehicle_size)
        {
            return factor;
        }
    }
    return std::nullopt;
}
