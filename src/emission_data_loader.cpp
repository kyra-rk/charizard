#include "emission_data_loader.hpp"
#include "emission_factors.hpp"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

std::vector<EmissionFactor> EmissionDataLoader::load_defra_2024()
{
    // Currently returns hardcoded defaults from DefaultEmissionFactors
    // In production, this could:
    // 1. Fetch from https://www.gov.uk/guidance/greenhouse-gas-reporting-conversion-factors-2024
    // 2. Parse the official DEFRA CSV/Excel file
    // 3. Cache the result in the database
    return DefaultEmissionFactors::defra_2024_factors();
}

std::vector<EmissionFactor> EmissionDataLoader::load_from_json(const std::string& json_str)
{
    std::vector<EmissionFactor> factors;

    try
    {
        const auto j = json::parse(json_str);

        if (!j.is_array())
        {
            throw std::runtime_error("Expected JSON array of factors");
        }

        for (const auto& item : j)
        {
            if (!item.is_object())
            {
                throw std::runtime_error("Each factor item must be a JSON object");
            }

            EmissionFactor factor;
            factor.mode = item.at("mode").get<std::string>();
            factor.fuel_type = item.value("fuel_type", "");
            factor.vehicle_size = item.value("vehicle_size", "");
            factor.kg_co2_per_km = item.at("kg_co2_per_km").get<double>();
            factor.source = item.value("source", "UNKNOWN");
            factor.updated_at = item.value("updated_at", static_cast<std::int64_t>(0));

            factors.push_back(std::move(factor));
        }
    }
    catch (const json::exception& e)
    {
        throw std::runtime_error(std::string("JSON parsing error: ") + e.what());
    }

    return factors;
}

std::vector<EmissionFactor> EmissionDataLoader::load_from_csv(const std::string& csv_str)
{
    std::vector<EmissionFactor> factors;
    std::istringstream iss(csv_str);
    std::string line;

    // Skip header
    if (!std::getline(iss, line))
    {
        throw std::runtime_error("CSV is empty");
    }

    // Parse data rows
    int row_num = 1;
    while (std::getline(iss, line))
    {
        row_num++;

        // Skip empty lines
        if (line.empty())
            continue;

        std::istringstream row_stream(line);
        std::string mode, fuel_type, vehicle_size, kg_str, source;

        if (!std::getline(row_stream, mode, ',') ||
            !std::getline(row_stream, fuel_type, ',') ||
            !std::getline(row_stream, vehicle_size, ',') ||
            !std::getline(row_stream, kg_str, ',') ||
            !std::getline(row_stream, source, ','))
        {
            throw std::runtime_error("CSV format error at row " + std::to_string(row_num));
        }

        // Trim whitespace
        auto trim = [](std::string& s)
        {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };

        trim(mode);
        trim(fuel_type);
        trim(vehicle_size);
        trim(kg_str);
        trim(source);

        try
        {
            double kg_co2_per_km = std::stod(kg_str);

            EmissionFactor factor;
            factor.mode = mode;
            factor.fuel_type = fuel_type;
            factor.vehicle_size = vehicle_size;
            factor.kg_co2_per_km = kg_co2_per_km;
            factor.source = source;
            factor.updated_at = 0; // CSV doesn't have timestamps

            factors.push_back(std::move(factor));
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("Failed to parse kg_co2_per_km at row " + std::to_string(row_num) + ": " + e.what());
        }
    }

    return factors;
}
