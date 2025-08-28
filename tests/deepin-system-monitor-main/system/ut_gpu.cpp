// Copyright (C) 2025
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/gpu.h"
#include <gtest/gtest.h>
#include <QDebug>

using namespace core::system;

TEST(UT_GpuReader, EnumerateDoesNotCrash)
{
    auto devices = GpuReader::enumerate();
    // Should not crash; zero devices is acceptable
    SUCCEED();
}

TEST(UT_GpuReader, ReadStatsGraceful)
{
    auto devices = GpuReader::enumerate();
    if (devices.isEmpty()) {
        // Gracefully handle no GPU environment
        SUCCEED();
        return;
    }
    for (const auto &dev : devices) {
        GpuStats stats;
        bool ok = GpuReader::readStats(dev, stats);
        // We accept ok==false on systems where backends unavailable, but must not crash
        (void)ok;
        // Values should be within reasonable ranges when available
        if (stats.utilizationPercent >= 0) {
            EXPECT_LE(stats.utilizationPercent, 100);
        }
        if (stats.temperatureC >= 0) {
            EXPECT_GE(stats.temperatureC, 0);
            EXPECT_LE(stats.temperatureC, 120);
        }
        if (stats.memoryTotalBytes > 0) {
            EXPECT_LE(stats.memoryUsedBytes, stats.memoryTotalBytes);
        }
    }
}
