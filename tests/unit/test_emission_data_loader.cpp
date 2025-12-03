#include "emission_data_loader.hpp"

#include <gtest/gtest.h>

class EmissionDataLoaderTest : public ::testing::Test
{
};

// ===== DEFRA 2024 Loading =====

TEST_F(EmissionDataLoaderTest, LoadDEFRA2024)
{
    auto factors = EmissionDataLoader::load_defra_2024();

    EXPECT_GT(factors.size(), 0);

    // Check that we have car factors
    bool found_car = false;
    for (const auto& f : factors)
    {
        if (f.mode == "car")
        {
            found_car = true;
            break;
        }
    }
    EXPECT_TRUE(found_car);
}

TEST_F(EmissionDataLoaderTest, DEFRA2024IncludesAllFuelTypes)
{
    auto factors = EmissionDataLoader::load_defra_2024();

    bool has_petrol   = false;
    bool has_diesel   = false;
    bool has_electric = false;
    bool has_hybrid   = false;

    for (const auto& f : factors)
    {
        if (f.mode == "car")
        {
            if (f.fuel_type == "petrol")
                has_petrol = true;
            if (f.fuel_type == "diesel")
                has_diesel = true;
            if (f.fuel_type == "electric")
                has_electric = true;
            if (f.fuel_type == "hybrid")
                has_hybrid = true;
        }
    }

    EXPECT_TRUE(has_petrol);
    EXPECT_TRUE(has_diesel);
    EXPECT_TRUE(has_electric);
    EXPECT_TRUE(has_hybrid);
}

TEST_F(EmissionDataLoaderTest, DEFRA2024IncludesPublicTransit)
{
    auto factors = EmissionDataLoader::load_defra_2024();

    bool has_bus    = false;
    bool has_subway = false;
    bool has_train  = false;

    for (const auto& f : factors)
    {
        if (f.mode == "bus")
            has_bus = true;
        if (f.mode == "subway")
            has_subway = true;
        if (f.mode == "train")
            has_train = true;
    }

    EXPECT_TRUE(has_bus);
    EXPECT_TRUE(has_subway);
    EXPECT_TRUE(has_train);
}

// ===== JSON Loading =====

TEST_F(EmissionDataLoaderTest, LoadFromJSON)
{
    std::string json_str = R"([
      {
        "mode": "car",
        "fuel_type": "petrol",
        "vehicle_size": "small",
        "kg_co2_per_km": 0.167,
        "source": "TEST-SOURCE"
      }
    ])";

    auto factors = EmissionDataLoader::load_from_json(json_str);

    EXPECT_EQ(factors.size(), 1);
    EXPECT_EQ(factors[0].mode, "car");
    EXPECT_EQ(factors[0].fuel_type, "petrol");
    EXPECT_EQ(factors[0].vehicle_size, "small");
    EXPECT_NEAR(factors[0].kg_co2_per_km, 0.167, 0.001);
    EXPECT_EQ(factors[0].source, "TEST-SOURCE");
}

TEST_F(EmissionDataLoaderTest, LoadFromJSONMultipleFactors)
{
    std::string json_str = R"([
      {
        "mode": "car",
        "fuel_type": "petrol",
        "vehicle_size": "small",
        "kg_co2_per_km": 0.167,
        "source": "TEST"
      },
      {
        "mode": "bus",
        "fuel_type": "",
        "vehicle_size": "",
        "kg_co2_per_km": 0.073,
        "source": "TEST"
      }
    ])";

    auto factors = EmissionDataLoader::load_from_json(json_str);

    EXPECT_EQ(factors.size(), 2);
    EXPECT_EQ(factors[0].mode, "car");
    EXPECT_EQ(factors[1].mode, "bus");
}

TEST_F(EmissionDataLoaderTest, LoadFromJSONWithOptionalFields)
{
    std::string json_str = R"([
      {
        "mode": "bus",
        "kg_co2_per_km": 0.073
      }
    ])";

    auto factors = EmissionDataLoader::load_from_json(json_str);

    EXPECT_EQ(factors.size(), 1);
    EXPECT_EQ(factors[0].fuel_type, "");
    EXPECT_EQ(factors[0].vehicle_size, "");
    EXPECT_EQ(factors[0].source, "UNKNOWN");
}

TEST_F(EmissionDataLoaderTest, LoadFromJSONInvalidArray)
{
    std::string json_str = R"({ "mode": "car" })";

    EXPECT_THROW(EmissionDataLoader::load_from_json(json_str), std::runtime_error);
}

TEST_F(EmissionDataLoaderTest, LoadFromJSONMissingRequiredField)
{
    std::string json_str = R"([
      {
        "mode": "car",
        "fuel_type": "petrol"
      }
    ])";

    EXPECT_THROW(EmissionDataLoader::load_from_json(json_str), std::runtime_error);
}

// ===== CSV Loading =====

TEST_F(EmissionDataLoaderTest, LoadFromCSV)
{
    std::string csv_str = R"(mode,fuel_type,vehicle_size,kg_co2_per_km,source
car,petrol,small,0.167,TEST-SOURCE
bus,,, 0.073,TEST-SOURCE)";

    auto factors = EmissionDataLoader::load_from_csv(csv_str);

    EXPECT_EQ(factors.size(), 2);
    EXPECT_EQ(factors[0].mode, "car");
    EXPECT_EQ(factors[0].fuel_type, "petrol");
    EXPECT_NEAR(factors[0].kg_co2_per_km, 0.167, 0.001);

    EXPECT_EQ(factors[1].mode, "bus");
    EXPECT_EQ(factors[1].fuel_type, "");
}

TEST_F(EmissionDataLoaderTest, LoadFromCSVWithWhitespace)
{
    std::string csv_str = R"(mode,fuel_type,vehicle_size,kg_co2_per_km,source
  car  ,  petrol  ,  small  ,  0.167  ,  TEST-SOURCE  )";

    auto factors = EmissionDataLoader::load_from_csv(csv_str);

    EXPECT_EQ(factors.size(), 1);
    EXPECT_EQ(factors[0].mode, "car");
    EXPECT_EQ(factors[0].fuel_type, "petrol");
}

TEST_F(EmissionDataLoaderTest, LoadFromCSVSkipsEmptyLines)
{
    std::string csv_str = R"(mode,fuel_type,vehicle_size,kg_co2_per_km,source
car,petrol,small,0.167,TEST

bus,,,0.073,TEST)";

    auto factors = EmissionDataLoader::load_from_csv(csv_str);

    EXPECT_EQ(factors.size(), 2);
    EXPECT_EQ(factors[0].mode, "car");
    EXPECT_EQ(factors[1].mode, "bus");
}

TEST_F(EmissionDataLoaderTest, LoadFromCSVInvalidNumber)
{
    std::string csv_str = R"(mode,fuel_type,vehicle_size,kg_co2_per_km,source
car,petrol,small,invalid,TEST-SOURCE)";

    EXPECT_THROW(EmissionDataLoader::load_from_csv(csv_str), std::runtime_error);
}

TEST_F(EmissionDataLoaderTest, LoadFromCSVMissingColumns)
{
    std::string csv_str = R"(mode,fuel_type,vehicle_size,kg_co2_per_km,source
car,petrol,small)";

    EXPECT_THROW(EmissionDataLoader::load_from_csv(csv_str), std::runtime_error);
}

TEST_F(EmissionDataLoaderTest, LoadFromCSVEmpty)
{
    std::string csv_str = "";

    EXPECT_THROW(EmissionDataLoader::load_from_csv(csv_str), std::runtime_error);
}
