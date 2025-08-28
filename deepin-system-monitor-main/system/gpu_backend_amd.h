// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_SYSTEM_AMD_GPU_BACKEND_H
#define CORE_SYSTEM_AMD_GPU_BACKEND_H

#include "gpu.h"
#include <QDir>

namespace core {
namespace system {

// Extended GPU stats structure for AMD-specific features
struct AmdGpuStats : public GpuStats {
    int powerUsageWatts { -1 };        // Power consumption in Watts (from hwmon)
    int memoryClock { -1 };            // Memory clock in MHz
    int graphicsClock { -1 };          // Graphics clock in MHz
    int fanSpeedRpm { -1 };            // Fan speed in RPM
    int fanSpeedPercent { -1 };        // Fan speed percentage
    int powerLimit { -1 };             // Power limit in Watts
    QString driverVersion;             // AMD driver version
    quint64 vramUsed { 0 };            // VRAM used (from mem_info_vram_used)
    quint64 vramTotal { 0 };           // VRAM total (from mem_info_vram_total)
    quint64 gttUsed { 0 };             // GTT used (from mem_info_gtt_used)
    quint64 gttTotal { 0 };            // GTT total (from mem_info_gtt_total)
};

/**
 * @brief AMD GPU Backend using sysfs interfaces
 * 
 * This backend provides comprehensive monitoring for AMD GPUs using
 * the Linux kernel's sysfs interface. Supports AMDGPU driver.
 * 
 * Features:
 * - GPU utilization monitoring (gpu_busy_percent)
 * - Memory usage tracking (mem_info_vram_*)
 * - Temperature monitoring (hwmon)
 * - Power consumption tracking (hwmon power sensors)
 * - Clock frequencies (pp_dpm_sclk, pp_dpm_mclk)
 * - Fan speed monitoring (hwmon)
 * - Driver information
 * - GTT memory tracking
 */
class GpuBackendAmd
{
public:
    explicit GpuBackendAmd();
    ~GpuBackendAmd();

    // Core interface
    bool supports(const GpuDevice &device) const;
    bool readStats(const GpuDevice &device, GpuStats &out);
    
    // Extended AMD-specific interface
    bool readExtendedStats(const GpuDevice &device, AmdGpuStats &out);
    
    // System information
    QString getDriverVersion(const GpuDevice &device) const;
    QStringList getAvailableClockLevels(const GpuDevice &device, const QString &clockType) const;
    QStringList getSupportedPowerProfiles(const GpuDevice &device) const;
    
    // Advanced monitoring
    bool readPowerCap(const GpuDevice &device, int &currentWatts, int &maxWatts) const;
    bool readClockFrequencies(const GpuDevice &device, int &memoryClock, int &graphicsClock) const;
    bool readFanInfo(const GpuDevice &device, int &speedRpm, int &speedPercent) const;

private:
    // Helper functions for reading sysfs files
    bool readFirstLine(const QString &filePath, QString &out) const;
    bool readIntegerFile(const QString &filePath, qint64 &outValue) const;
    bool readFloatFile(const QString &filePath, double &outValue) const;
    
    // Hardware monitoring functions
    bool readHwmonAttribute(const QString &devicePath, const QString &attribute, qint64 &value) const;
    QDir findHwmonDir(const QString &devicePath) const;
    QStringList findHwmonFiles(const QDir &hwmonDir, const QString &pattern) const;
    
    // AMD-specific sysfs readers
    bool readGpuUtilization(const QString &devicePath, int &utilization) const;
    bool readVramInfo(const QString &devicePath, quint64 &used, quint64 &total) const;
    bool readGttInfo(const QString &devicePath, quint64 &used, quint64 &total) const;
    bool readTemperature(const QString &devicePath, int &temperatureC) const;
    bool readPowerUsage(const QString &devicePath, int &powerWatts) const;
    bool readClockInfo(const QString &devicePath, int &memoryClock, int &graphicsClock) const;
    bool readFanSpeed(const QString &devicePath, int &speedRpm, int &speedPercent) const;
    
    // Driver and device info
    QString readDriverVersion(const QString &devicePath) const;
    QString readDeviceId(const QString &devicePath) const;
    QString readSubsystemId(const QString &devicePath) const;
    
    // Clock management helpers
    int parseCurrentClockFromDpm(const QString &dpmContent) const;
    QStringList parseClockLevels(const QString &dpmContent) const;
};

} // namespace system
} // namespace core

#endif // CORE_SYSTEM_AMD_GPU_BACKEND_H
