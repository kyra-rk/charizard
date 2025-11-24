#include "storage.hpp"

#include <gtest/gtest.h>

TEST(EmissionFactor, KnownModes_ReturnCorrectFactors)
{
    EXPECT_DOUBLE_EQ(emission_factor_for("taxi"), 0.18);
    EXPECT_DOUBLE_EQ(emission_factor_for("car"), 0.18);
    EXPECT_DOUBLE_EQ(emission_factor_for("bus"), 0.08);
    EXPECT_DOUBLE_EQ(emission_factor_for("subway"), 0.04);
    EXPECT_DOUBLE_EQ(emission_factor_for("train"), 0.04);
    EXPECT_DOUBLE_EQ(emission_factor_for("bike"), 0.0);
    EXPECT_DOUBLE_EQ(emission_factor_for("walk"), 0.0);
}

TEST(EmissionFactor, UnknownMode_ReturnsDefaultFactor)
{
    // Test the default fallback for unknown modes (line 54)
    EXPECT_DOUBLE_EQ(emission_factor_for("unknown"), 0.1);
    EXPECT_DOUBLE_EQ(emission_factor_for("rocket"), 0.1);
    EXPECT_DOUBLE_EQ(emission_factor_for("teleport"), 0.1);
    EXPECT_DOUBLE_EQ(emission_factor_for(""), 0.1);
}
