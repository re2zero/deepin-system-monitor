// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_SYSTEM_GPU_H
#define CORE_SYSTEM_GPU_H

#include <QtGlobal>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QLibrary;
QT_END_NAMESPACE

namespace core {
namespace system {

enum class GpuVendor {
    Nvidia,
    AMD,
    Intel,
    Unknown
};

struct GpuDevice {
    QString cardPath;    // e.g. /sys/class/drm/card0
    QString devicePath;  // e.g. /sys/class/drm/card0/device
    QString pciBusId;    // domain:bus:device.function
    QString name;        // human readable
    GpuVendor vendor {GpuVendor::Unknown};
};

struct GpuStats {
    int utilizationPercent { -1 };         // -1 means unavailable
    quint64 memoryUsedBytes { 0 };         // 0 means unknown if memoryTotalBytes is also 0
    quint64 memoryTotalBytes { 0 };
    int temperatureC { -1 };               // -1 means unavailable
    
    // Enhanced properties for better GPU monitoring
    int powerUsageWatts { -1 };            // Current power usage in Watts (-1 means unavailable)
    int maxPowerWatts { -1 };              // Maximum power limit in Watts (-1 means unavailable)
    int coreClockkHz { -1 };               // Core/Graphics clock in MHz (-1 means unavailable)
    int memoryClockkHz { -1 };             // Memory clock in MHz (-1 means unavailable)
    int fanSpeedPercent { -1 };            // Fan speed percentage (-1 means unavailable)
    int fanSpeedRPM { -1 };                // Fan speed in RPM (-1 means unavailable)
    
    // Engine-specific utilization (for detailed monitoring)
    int graphicsUtilPercent { -1 };        // Graphics/3D engine utilization
    int videoEncodeUtilPercent { -1 };     // Video encoding engine utilization  
    int videoDecodeUtilPercent { -1 };     // Video decoding engine utilization
    int computeUtilPercent { -1 };         // Compute engine utilization
    
    // Additional info
    QString driverVersion;                 // GPU driver version
    QString vbiosVersion;                  // Video BIOS version
    int pcieGeneration { -1 };             // PCIe generation (e.g., 3, 4, 5)
    int pcieLanes { -1 };                  // Number of PCIe lanes
};

class GpuReader
{
public:
    static QVector<GpuDevice> enumerate();
    static bool readStats(const GpuDevice &device, GpuStats &outStats);

    // Expose vendor-specific readers for backend adapters
    static bool readStatsNvidia(const GpuDevice &device, GpuStats &outStats);
    static bool readStatsAmd(const QString &devicePath, GpuStats &outStats);
    static bool readStatsIntel(const QString &devicePath, GpuStats &outStats);

private:
    static GpuVendor detectVendor(const QString &devicePath);
    static QString detectName(const QString &devicePath, GpuVendor vendor);
    static QString detectPciBusId(const QString &devicePath);

    // Helpers
    static bool readFirstLine(const QString &filePath, QString &out);
    static bool readIntegerFile(const QString &filePath, qint64 &outValue);
    static bool readAmdClockInfo(const QString &devicePath, int &memoryClock, int &graphicsClock);
    static void readNvmlClockInfo(QLibrary &lib, void* nvmlDevice, GpuStats &outStats);
};

} // namespace system
} // namespace core

#endif // CORE_SYSTEM_GPU_H
