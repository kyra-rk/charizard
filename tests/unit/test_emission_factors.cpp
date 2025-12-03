#include "emission_factors.hpp"
#include "storage.hpp"

#include <gtest/gtest.h>

// ===== Tests for calculate_co2_emissions (DEFRA 2024 factors) =====

TEST(EmissionCalculation, CarPetrolSmallSinglePassenger)
{
    // 0.167 kg/km * 10 km / 1 passenger = 1.67 kg CO2e
    auto result = calculate_co2_emissions("car", "petrol", "small", 1.0, 10.0);
    EXPECT_NEAR(result, 1.67, 0.01);
}

TEST(EmissionCalculation, CarPetrolSmallTwoPassengers)
{
    // 0.167 kg/km * 10 km / 2 passengers = 0.835 kg CO2e
    auto result = calculate_co2_emissions("car", "petrol", "small", 2.0, 10.0);
    EXPECT_NEAR(result, 0.835, 0.01);
}

TEST(EmissionCalculation, CarDieselMediumFractionalOccupancy)
{
    // 0.168 kg/km * 20 km / 1.5 passengers = 2.24 kg CO2e
    auto result = calculate_co2_emissions("car", "diesel", "medium", 1.5, 20.0);
    EXPECT_NEAR(result, 2.24, 0.01);
}

TEST(EmissionCalculation, CarElectricLarge)
{
    // 0.115 kg/km * 100 km / 1 passenger = 11.5 kg CO2e
    auto result = calculate_co2_emissions("car", "electric", "large", 1.0, 100.0);
    EXPECT_NEAR(result, 11.5, 0.01);
}

TEST(EmissionCalculation, CarHybridMedium)
{
    // 0.155 kg/km * 50 km / 1 passenger = 7.75 kg CO2e
    auto result = calculate_co2_emissions("car", "hybrid", "medium", 1.0, 50.0);
    EXPECT_NEAR(result, 7.75, 0.01);
}

TEST(EmissionCalculation, TaxiElectricMedium)
{
    // 0.088 kg/km * 15 km / 1 passenger = 1.32 kg CO2e
    auto result = calculate_co2_emissions("taxi", "electric", "medium", 1.0, 15.0);
    EXPECT_NEAR(result, 1.32, 0.01);
}

TEST(EmissionCalculation, BusPerPassenger)
{
    // 0.073 kg/km * 10 km (no occupancy division for buses) = 0.73 kg CO2e
    auto result = calculate_co2_emissions("bus", "", "", 1.0, 10.0);
    EXPECT_NEAR(result, 0.73, 0.01);
}

TEST(EmissionCalculation, SubwayPerPassenger)
{
    // 0.041 kg/km * 20 km = 0.82 kg CO2e
    auto result = calculate_co2_emissions("subway", "", "", 1.0, 20.0);
    EXPECT_NEAR(result, 0.82, 0.01);
}

TEST(EmissionCalculation, TrainPerPassenger)
{
    // 0.051 kg/km * 100 km = 5.1 kg CO2e
    auto result = calculate_co2_emissions("train", "", "", 1.0, 100.0);
    EXPECT_NEAR(result, 5.1, 0.01);
}

TEST(EmissionCalculation, BikeZeroEmissions)
{
    auto result = calculate_co2_emissions("bike", "", "", 1.0, 50.0);
    EXPECT_EQ(result, 0.0);
}

TEST(EmissionCalculation, WalkZeroEmissions)
{
    auto result = calculate_co2_emissions("walk", "", "", 1.0, 5.0);
    EXPECT_EQ(result, 0.0);
}

TEST(EmissionCalculation, ElectricVsPetrolComparison)
{
    auto petrol   = calculate_co2_emissions("car", "petrol", "medium", 1.0, 100.0);
    auto electric = calculate_co2_emissions("car", "electric", "medium", 1.0, 100.0);

    // Electric should be significantly lower (~43% of petrol for medium car)
    EXPECT_LT(electric, petrol);
    EXPECT_NEAR(electric / petrol, 0.43, 0.05);
}

TEST(EmissionCalculation, CarVsBusComparison)
{
    auto car = calculate_co2_emissions("car", "petrol", "medium", 1.0, 10.0);
    auto bus = calculate_co2_emissions("bus", "", "", 1.0, 10.0);

    // Bus should be lower emissions per passenger
    EXPECT_LT(bus, car);
}

TEST(EmissionCalculation, OccupancySharingEffect)
{
    auto single = calculate_co2_emissions("car", "petrol", "small", 1.0, 10.0);
    auto triple = calculate_co2_emissions("car", "petrol", "small", 3.0, 10.0);

    // Triple occupancy should be ~1/3 of single
    EXPECT_NEAR(triple / single, 1.0 / 3.0, 0.01);
}

// ===== Validation Tests =====

TEST(EmissionCalculation, InvalidNegativeOccupancy)
{
    EXPECT_THROW(calculate_co2_emissions("car", "petrol", "small", -1.0, 10.0), std::runtime_error);
}

TEST(EmissionCalculation, InvalidZeroOccupancy)
{
    EXPECT_THROW(calculate_co2_emissions("car", "petrol", "small", 0.0, 10.0), std::runtime_error);
}

TEST(EmissionCalculation, InvalidNegativeDistance)
{
    EXPECT_THROW(calculate_co2_emissions("car", "petrol", "small", 1.0, -10.0), std::runtime_error);
}

TEST(EmissionCalculation, ValidZeroDistance)
{
    auto result = calculate_co2_emissions("car", "petrol", "small", 1.0, 0.0);
    EXPECT_EQ(result, 0.0);
}

// ===== Edge Cases =====

TEST(EmissionCalculation, LargeDistance)
{
    auto result = calculate_co2_emissions("car", "petrol", "medium", 1.0, 1000.0);
    // 0.203 kg/km * 1000 km = 203.0 kg CO2e
    EXPECT_NEAR(result, 203.0, 0.1);
}

TEST(EmissionCalculation, SmallFractionalDistance)
{
    auto result = calculate_co2_emissions("car", "petrol", "small", 1.0, 0.5);
    // 0.167 kg/km * 0.5 km = 0.0835 kg CO2e
    EXPECT_NEAR(result, 0.0835, 0.001);
}

// ===== Default Factors Tests =====

TEST(DefaultEmissionFactors, BasicDefaultsNotEmpty)
{
    auto factors = DefaultEmissionFactors::basic_defaults();
    EXPECT_GT(factors.size(), 0);
}

TEST(DefaultEmissionFactors, Defra2024FactorsNotEmpty)
{
    auto factors = DefaultEmissionFactors::defra_2024_factors();
    EXPECT_GT(factors.size(), 0);
}

TEST(DefaultEmissionFactors, GetDefaultFactorForCarPetrol)
{
    auto factor = DefaultEmissionFactors::get_default_factor("car", "petrol", "small");
    ASSERT_TRUE(factor.has_value());
    EXPECT_EQ(factor.value().mode, "car");
    EXPECT_EQ(factor.value().fuel_type, "petrol");
    EXPECT_EQ(factor.value().vehicle_size, "small");
    // Should return DEFRA 2024 factor (0.167), not basic default (0.2)
    EXPECT_NEAR(factor.value().kg_co2_per_km, 0.167, 0.001);
}

TEST(DefaultEmissionFactors, GetDefaultFactorNonExistent)
{
    auto factor = DefaultEmissionFactors::get_default_factor("invalid_mode", "", "");
    EXPECT_FALSE(factor.has_value());
}

TEST(DefaultEmissionFactors, BasicDefaultCarPetrolSmall)
{
    auto factors = DefaultEmissionFactors::basic_defaults();
    bool found   = false;
    for (const auto& f : factors)
    {
        if (f.mode == "car" && f.fuel_type == "petrol" && f.vehicle_size == "small")
        {
            found = true;
            EXPECT_NEAR(f.kg_co2_per_km, 0.2, 0.001);
            EXPECT_EQ(f.source, "BASIC-DEFAULT");
        }
    }
    EXPECT_TRUE(found);
}

TEST(DefaultEmissionFactors, Defra2024CarPetrolSmall)
{
    auto factors = DefaultEmissionFactors::defra_2024_factors();
    bool found   = false;
    for (const auto& f : factors)
    {
        if (f.mode == "car" && f.fuel_type == "petrol" && f.vehicle_size == "small")
        {
            found = true;
            EXPECT_NEAR(f.kg_co2_per_km, 0.167, 0.001);
            EXPECT_EQ(f.source, "DEFRA-2024");
        }
    }
    EXPECT_TRUE(found);
}

TEST(DefaultEmissionFactors, BasicDefaultBus)
{
    auto factors = DefaultEmissionFactors::basic_defaults();
    bool found   = false;
    for (const auto& f : factors)
    {
        if (f.mode == "bus")
        {
            found = true;
            EXPECT_NEAR(f.kg_co2_per_km, 0.1, 0.001);
        }
    }
    EXPECT_TRUE(found);
}

TEST(DefaultEmissionFactors, Defra2024Bus)
{
    auto factors = DefaultEmissionFactors::defra_2024_factors();
    bool found   = false;
    for (const auto& f : factors)
    {
        if (f.mode == "bus")
        {
            found = true;
            EXPECT_NEAR(f.kg_co2_per_km, 0.073, 0.001);
        }
    }
    EXPECT_TRUE(found);
}
