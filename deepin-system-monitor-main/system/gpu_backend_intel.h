// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_SYSTEM_INTEL_GPU_BACKEND_H
#define CORE_SYSTEM_INTEL_GPU_BACKEND_H

#include "gpu.h"
#include <QDir>
#include <QMap>

namespace core {
namespace system {

// Extended GPU stats structure for Intel-specific features
struct IntelGpuStats : public GpuStats {
    int powerUsageWatts { -1 };        // Power consumption in Watts (from hwmon)
    QString driverVersion;             // Intel driver version (i915/xe)
    QString platformName;              // GPU platform name (Gen9, Gen12, etc.)
    
    // Engine-specific utilization
    struct EngineStats {
        QString name;                  // Engine name (rcs0, bcs0, vcs0, etc.)
        QString className;             // Engine class (render, copy, video, etc.)
        int utilizationPercent { -1 }; // Engine-specific utilization
        quint64 busyNs { 0 };          // Total busy time in nanoseconds
        int instances { 1 };           // Number of engine instances
    };
    QVector<EngineStats> engines;
    
    // Memory information (if available)
    quint64 memoryShared { 0 };        // Shared memory usage
    quint64 memoryResident { 0 };      // Resident memory usage
    
    // GPU frequency information
    int currentFreqMHz { -1 };         // Current GPU frequency
    int maxFreqMHz { -1 };             // Maximum GPU frequency
    int minFreqMHz { -1 };             // Minimum GPU frequency
};

/**
 * @brief Intel GPU Backend using i915/xe sysfs interfaces
 * 
 * This backend provides comprehensive monitoring for Intel integrated GPUs
 * using the Linux kernel's i915/xe driver sysfs interface.
 * 
 * Features:
 * - Engine-specific utilization monitoring (render, copy, video, etc.)
 * - Temperature monitoring (hwmon)
 * - Power consumption tracking (hwmon)
 * - GPU frequency monitoring
 * - Driver information
 * - Memory usage tracking (limited)
 * - Multi-engine support with per-engine statistics
 */
class GpuBackendIntel
{
public:
    explicit GpuBackendIntel();
    ~GpuBackendIntel();

    // Core interface
    bool supports(const GpuDevice &device) const;
    bool readStats(const GpuDevice &device, GpuStats &out);
    
    // Extended Intel-specific interface
    bool readExtendedStats(const GpuDevice &device, IntelGpuStats &out);
    
    // System information
    QString getDriverVersion(const GpuDevice &device) const;
    QString getPlatformName(const GpuDevice &device) const;
    QStringList getAvailableEngines(const GpuDevice &device) const;
    
    // Advanced monitoring
    bool readFrequencyInfo(const GpuDevice &device, int &current, int &min, int &max) const;
    bool readEngineUtilization(const GpuDevice &device, QVector<IntelGpuStats::EngineStats> &engines) const;
    bool readMemoryInfo(const GpuDevice &device, quint64 &shared, quint64 &resident) const;

private:
    // Helper functions for reading sysfs files
    bool readFirstLine(const QString &filePath, QString &out) const;
    bool readIntegerFile(const QString &filePath, qint64 &outValue) const;
    bool readFloatFile(const QString &filePath, double &outValue) const;
    
    // Hardware monitoring functions
    bool readHwmonAttribute(const QString &devicePath, const QString &attribute, qint64 &value) const;
    QDir findHwmonDir(const QString &devicePath) const;
    QStringList findHwmonFiles(const QDir &hwmonDir, const QString &pattern) const;
    
    // Intel-specific sysfs readers
    bool readEngineDirectory(const QString &devicePath, QVector<IntelGpuStats::EngineStats> &engines) const;
    bool readSingleEngineStats(const QString &enginePath, IntelGpuStats::EngineStats &engine) const;
    bool readTemperature(const QString &devicePath, int &temperatureC) const;
    bool readPowerUsage(const QString &devicePath, int &powerWatts) const;
    bool readGpuFrequency(const QString &devicePath, int &current, int &min, int &max) const;
    
    // Driver and device info
    QString readDriverName(const QString &devicePath) const;
    QString readDriverVersion(const QString &devicePath) const;
    QString readPlatformInfo(const QString &devicePath) const;
    QString readDeviceId(const QString &devicePath) const;
    
    // Engine parsing helpers
    QString parseEngineClass(const QString &engineName) const;
    QString formatEngineStats(const IntelGpuStats::EngineStats &engine) const;
    int calculateAverageUtilization(const QVector<IntelGpuStats::EngineStats> &engines) const;
    
    // Cache for engine discovery (engines don't change during runtime)
    QMap<QString, QVector<IntelGpuStats::EngineStats>> m_engineCache;
};

} // namespace system
} // namespace core

#endif // CORE_SYSTEM_INTEL_GPU_BACKEND_H
