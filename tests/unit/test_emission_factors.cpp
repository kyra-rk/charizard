#include <gtest/gtest.h>

#include "emission_calculation.hpp"
#include "emission_factors.hpp"
#include "memory_emission_factors.hpp"

class EmissionCalculationTest : public ::testing::Test
{
  protected:
    InMemoryEmissionFactorStore store;
};

// ===== Tests for calculate_emissions_from_store =====

TEST_F(EmissionCalculationTest, CarPetrolSmallSinglePassenger)
{
    auto result = calculate_emissions_from_store(store, "car", "petrol", "small", 1.0, 10.0);
    ASSERT_TRUE(result.has_value());
    // 0.167 kg/km * 10 km / 1 passenger = 1.67 kg CO2e
    EXPECT_NEAR(result.value(), 1.67, 0.01);
}

TEST_F(EmissionCalculationTest, CarPetrolSmallTwoPassengers)
{
    auto result = calculate_emissions_from_store(store, "car", "petrol", "small", 2.0, 10.0);
    ASSERT_TRUE(result.has_value());
    // 0.167 kg/km * 10 km / 2 passengers = 0.835 kg CO2e
    EXPECT_NEAR(result.value(), 0.835, 0.01);
}

TEST_F(EmissionCalculationTest, CarDieselMediumFractionalOccupancy)
{
    auto result = calculate_emissions_from_store(store, "car", "diesel", "medium", 1.5, 20.0);
    ASSERT_TRUE(result.has_value());
    // 0.168 kg/km * 20 km / 1.5 passengers = 2.24 kg CO2e
    EXPECT_NEAR(result.value(), 2.24, 0.01);
}

TEST_F(EmissionCalculationTest, CarElectricLarge)
{
    auto result = calculate_emissions_from_store(store, "car", "electric", "large", 1.0, 100.0);
    ASSERT_TRUE(result.has_value());
    // 0.115 kg/km * 100 km / 1 passenger = 11.5 kg CO2e
    EXPECT_NEAR(result.value(), 11.5, 0.01);
}

TEST_F(EmissionCalculationTest, CarHybridMedium)
{
    auto result = calculate_emissions_from_store(store, "car", "hybrid", "medium", 1.0, 50.0);
    ASSERT_TRUE(result.has_value());
    // 0.155 kg/km * 50 km / 1 passenger = 7.75 kg CO2e
    EXPECT_NEAR(result.value(), 7.75, 0.01);
}

TEST_F(EmissionCalculationTest, TaxiElectricMedium)
{
    auto result = calculate_emissions_from_store(store, "taxi", "electric", "medium", 1.0, 15.0);
    ASSERT_TRUE(result.has_value());
    // 0.088 kg/km * 15 km / 1 passenger = 1.32 kg CO2e
    EXPECT_NEAR(result.value(), 1.32, 0.01);
}

TEST_F(EmissionCalculationTest, BusPerPassenger)
{
    auto result = calculate_emissions_from_store(store, "bus", "", "", 1.0, 10.0);
    ASSERT_TRUE(result.has_value());
    // 0.073 kg/km * 10 km (no occupancy division) = 0.73 kg CO2e
    EXPECT_NEAR(result.value(), 0.73, 0.01);
}

TEST_F(EmissionCalculationTest, SubwayPerPassenger)
{
    auto result = calculate_emissions_from_store(store, "subway", "", "", 1.0, 20.0);
    ASSERT_TRUE(result.has_value());
    // 0.041 kg/km * 20 km = 0.82 kg CO2e
    EXPECT_NEAR(result.value(), 0.82, 0.01);
}

TEST_F(EmissionCalculationTest, TrainPerPassenger)
{
    auto result = calculate_emissions_from_store(store, "train", "", "", 1.0, 100.0);
    ASSERT_TRUE(result.has_value());
    // 0.051 kg/km * 100 km = 5.1 kg CO2e
    EXPECT_NEAR(result.value(), 5.1, 0.01);
}

TEST_F(EmissionCalculationTest, BikeZeroEmissions)
{
    auto result = calculate_emissions_from_store(store, "bike", "", "", 1.0, 50.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0.0);
}

TEST_F(EmissionCalculationTest, WalkZeroEmissions)
{
    auto result = calculate_emissions_from_store(store, "walk", "", "", 1.0, 5.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0.0);
}

TEST_F(EmissionCalculationTest, ElectricVsPetrolComparison)
{
    auto petrol = calculate_emissions_from_store(store, "car", "petrol", "medium", 1.0, 100.0);
    auto electric = calculate_emissions_from_store(store, "car", "electric", "medium", 1.0, 100.0);

    ASSERT_TRUE(petrol.has_value());
    ASSERT_TRUE(electric.has_value());

    // Electric should be significantly lower (~43% of petrol for medium car)
    EXPECT_LT(electric.value(), petrol.value());
    EXPECT_NEAR(electric.value() / petrol.value(), 0.43, 0.05);
}

TEST_F(EmissionCalculationTest, CarVsBusComparison)
{
    auto car = calculate_emissions_from_store(store, "car", "petrol", "medium", 1.0, 10.0);
    auto bus = calculate_emissions_from_store(store, "bus", "", "", 1.0, 10.0);

    ASSERT_TRUE(car.has_value());
    ASSERT_TRUE(bus.has_value());

    // Bus should be lower emissions per passenger
    EXPECT_LT(bus.value(), car.value());
}

TEST_F(EmissionCalculationTest, OccupancySharingEffect)
{
    auto single = calculate_emissions_from_store(store, "car", "petrol", "small", 1.0, 10.0);
    auto triple = calculate_emissions_from_store(store, "car", "petrol", "small", 3.0, 10.0);

    ASSERT_TRUE(single.has_value());
    ASSERT_TRUE(triple.has_value());

    // Triple occupancy should be ~1/3 of single
    EXPECT_NEAR(triple.value() / single.value(), 1.0 / 3.0, 0.01);
}

// ===== Validation Tests =====

TEST_F(EmissionCalculationTest, InvalidNegativeOccupancy)
{
    EXPECT_THROW(
        calculate_emissions_from_store(store, "car", "petrol", "small", -1.0, 10.0),
        std::runtime_error
    );
}

TEST_F(EmissionCalculationTest, InvalidZeroOccupancy)
{
    EXPECT_THROW(
        calculate_emissions_from_store(store, "car", "petrol", "small", 0.0, 10.0),
        std::runtime_error
    );
}

TEST_F(EmissionCalculationTest, InvalidNegativeDistance)
{
    EXPECT_THROW(
        calculate_emissions_from_store(store, "car", "petrol", "small", 1.0, -10.0),
        std::runtime_error
    );
}

TEST_F(EmissionCalculationTest, ValidZeroDistance)
{
    auto result = calculate_emissions_from_store(store, "car", "petrol", "small", 1.0, 0.0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0.0);
}

// ===== Edge Cases =====

TEST_F(EmissionCalculationTest, LargeDistance)
{
    auto result = calculate_emissions_from_store(store, "car", "petrol", "medium", 1.0, 1000.0);
    ASSERT_TRUE(result.has_value());
    // 0.203 kg/km * 1000 km = 203.0 kg CO2e
    EXPECT_NEAR(result.value(), 203.0, 0.1);
}

TEST_F(EmissionCalculationTest, SmallFractionalDistance)
{
    auto result = calculate_emissions_from_store(store, "car", "petrol", "small", 1.0, 0.5);
    ASSERT_TRUE(result.has_value());
    // 0.167 kg/km * 0.5 km = 0.0835 kg CO2e
    EXPECT_NEAR(result.value(), 0.0835, 0.001);
}

// ===== Factor Store Tests =====

TEST_F(EmissionCalculationTest, StoreHasInitialFactors)
{
    EXPECT_GT(store.factor_count(), 0);
}

TEST_F(EmissionCalculationTest, GetFactorByQuery)
{
    auto factor = store.get_factor("car", "petrol", "small");
    ASSERT_TRUE(factor.has_value());
    EXPECT_EQ(factor.value().mode, "car");
    EXPECT_EQ(factor.value().fuel_type, "petrol");
    EXPECT_EQ(factor.value().vehicle_size, "small");
    EXPECT_NEAR(factor.value().kg_co2_per_km, 0.167, 0.001);
}

TEST_F(EmissionCalculationTest, GetNonExistentFactor)
{
    auto factor = store.get_factor("invalid_mode", "", "");
    EXPECT_FALSE(factor.has_value());
}

TEST_F(EmissionCalculationTest, GetFactorsByMode)
{
    auto factors = store.get_factors_by_mode("car");
    EXPECT_GT(factors.size(), 0);
    for (const auto& f : factors)
    {
        EXPECT_EQ(f.mode, "car");
    }
}

TEST_F(EmissionCalculationTest, StoreAndRetrieveFactor)
{
    EmissionFactor custom_factor;
    custom_factor.mode = "hovercraft";
    custom_factor.fuel_type = "hydrogen";
    custom_factor.vehicle_size = "medium";
    custom_factor.kg_co2_per_km = 0.001; // Very low emissions
    custom_factor.source = "CUSTOM-TEST";
    custom_factor.updated_at = 0;

    store.store_factor(custom_factor);

    auto retrieved = store.get_factor("hovercraft", "hydrogen", "medium");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved.value().mode, "hovercraft");
    EXPECT_NEAR(retrieved.value().kg_co2_per_km, 0.001, 0.0001);
}
