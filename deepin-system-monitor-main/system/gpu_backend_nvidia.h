// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_SYSTEM_NVIDIA_GPU_BACKEND_H
#define CORE_SYSTEM_NVIDIA_GPU_BACKEND_H

#include "gpu.h"
#include <QLibrary>
#include <memory>

namespace core {
namespace system {

// Enhanced GPU stats structure for NVIDIA-specific features
struct NvidiaGpuStats : public GpuStats {
    int powerUsageWatts { -1 };        // Power consumption in Watts
    int memoryClock { -1 };            // Memory clock in MHz
    int graphicsClock { -1 };          // Graphics clock in MHz
    int fanSpeedPercent { -1 };        // Fan speed percentage
    int performanceState { -1 };       // P-State (0-12)
    QString driverVersion;             // NVIDIA driver version
    QString vbiosVersion;              // VBIOS version
    
    // Per-process GPU usage (for advanced monitoring)
    struct ProcessUsage {
        unsigned int pid;
        unsigned long long memoryUsed; // in bytes
        unsigned int type;             // 0=Graphics, 1=Compute
        QString processName;
    };
    QVector<ProcessUsage> processUsages;
};

/**
 * @brief NVIDIA GPU Backend using NVML (NVIDIA Management Library)
 * 
 * This backend provides comprehensive monitoring for NVIDIA GPUs using
 * the official NVML library. Supports all modern NVIDIA GPUs with proper
 * driver installation.
 * 
 * Features:
 * - GPU utilization monitoring
 * - Memory usage tracking
 * - Temperature monitoring
 * - Power consumption tracking
 * - Clock frequencies (memory, graphics)
 * - Fan speed monitoring
 * - P-State reporting
 * - Per-process GPU usage tracking
 * - Driver and VBIOS version information
 */
class GpuBackendNvidia
{
public:
    explicit GpuBackendNvidia();
    ~GpuBackendNvidia();

    // Core interface
    bool supports(const GpuDevice &device) const;
    bool readStats(const GpuDevice &device, GpuStats &out);
    
    // Extended NVIDIA-specific interface
    bool readExtendedStats(const GpuDevice &device, NvidiaGpuStats &out);
    bool readProcessUsages(const GpuDevice &device, QVector<NvidiaGpuStats::ProcessUsage> &out);
    
    // System information
    QString getDriverVersion() const;
    QString getNvmlVersion() const;
    int getDeviceCount() const;
    
    // Performance tuning
    bool isThrottled(const GpuDevice &device) const;
    QString getThrottleReasons(const GpuDevice &device) const;

private:
    class NvidiaBackendPrivate;
    std::unique_ptr<NvidiaBackendPrivate> d_ptr;
    
    // Initialization and cleanup
    bool initializeNvml();
    void shutdownNvml();
    bool isNvmlAvailable() const;
    
    // Helper functions
    bool getNvmlDevice(const GpuDevice &device, void** nvmlDevice) const;
    void* findNvmlLibrary() const;
    QStringList getNvmlLibraryCandidates() const;
};

} // namespace system
} // namespace core

#endif // CORE_SYSTEM_NVIDIA_GPU_BACKEND_H
