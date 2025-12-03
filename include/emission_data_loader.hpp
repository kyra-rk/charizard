#pragma once
#include "emission_factors.hpp"

#include <string>
#include <vector>

/**
 * Data loader for emission factors from external sources.
 * Can fetch from online APIs or static sources and convert to our format.
 *
 * Supports:
 * - DEFRA UK Government conversion factors
 * (https://www.gov.uk/guidance/greenhouse-gas-reporting-conversion-factors-2024)
 * - Other sources can be added as needed
 */
class EmissionDataLoader
{
  public:
    /**
     * Load DEFRA 2024 factors (currently from hardcoded defaults).
     * In production, this could fetch from the official DEFRA API or CSV download.
     *
     * @return Vector of EmissionFactor structs with DEFRA 2024 data
     */
    static std::vector<EmissionFactor> load_defra_2024();

    /**
     * Load factors from a JSON string.
     * Expected format: array of objects with keys: mode, fuel_type, vehicle_size, kg_co2_per_km, source
     *
     * @param json_str JSON string containing factors
     * @return Vector of EmissionFactor structs parsed from JSON
     */
    static std::vector<EmissionFactor> load_from_json(const std::string& json_str);

    /**
     * Load factors from a CSV string.
     * Expected format: mode,fuel_type,vehicle_size,kg_co2_per_km,source
     *
     * @param csv_str CSV string containing factors (first line should be headers)
     * @return Vector of EmissionFactor structs parsed from CSV
     */
    static std::vector<EmissionFactor> load_from_csv(const std::string& csv_str);
};
