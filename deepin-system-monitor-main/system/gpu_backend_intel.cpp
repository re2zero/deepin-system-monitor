// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_backend_intel.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QLoggingCategory>
#include <QDebug>
#include <QDirIterator>

namespace core {
namespace system {

Q_LOGGING_CATEGORY(intelLog, "gpu.intel")

GpuBackendIntel::GpuBackendIntel()
{
    qCDebug(intelLog) << "Intel GPU backend initialized";
}

GpuBackendIntel::~GpuBackendIntel()
{
    qCDebug(intelLog) << "Intel GPU backend destroyed";
}

bool GpuBackendIntel::supports(const GpuDevice &device) const
{
    return device.vendor == GpuVendor::Intel;
}

bool GpuBackendIntel::readStats(const GpuDevice &device, GpuStats &out)
{
    bool hasData = false;
    
    // Read engine utilization and calculate average
    QVector<IntelGpuStats::EngineStats> engines;
    if (readEngineUtilization(device, engines)) {
        int avgUtilization = calculateAverageUtilization(engines);
        if (avgUtilization >= 0) {
            out.utilizationPercent = avgUtilization;
            hasData = true;
        }
    }
    
    // Read temperature
    int temperature = -1;
    if (readTemperature(device.devicePath, temperature)) {
        out.temperatureC = temperature;
        hasData = true;
    }
    
    // Read frequency information
    int currentFreq = -1, minFreq = -1, maxFreq = -1;
    if (readFrequencyInfo(device, currentFreq, minFreq, maxFreq)) {
        if (currentFreq > 0) {
            // Convert MHz to kHz for consistency with GPU stats structure
            out.coreClockkHz = static_cast<qint64>(currentFreq) * 1000;
            hasData = true;
        }
        // Intel integrated GPUs don't have separate memory clocks
        out.memoryClockkHz = -1;
    }
    
    // Intel integrated GPUs typically don't have dedicated VRAM,
    // so memory stats are usually not available or meaningful
    out.memoryUsedBytes = 0;
    out.memoryTotalBytes = 0;
    
    return hasData;
}

bool GpuBackendIntel::readExtendedStats(const GpuDevice &device, IntelGpuStats &out)
{
    // First read basic stats
    if (!readStats(device, out)) {
        return false;
    }
    
    // Read extended Intel-specific information
    readEngineUtilization(device, out.engines);
    readPowerUsage(device.devicePath, out.powerUsageWatts);
    readFrequencyInfo(device, out.currentFreqMHz, out.minFreqMHz, out.maxFreqMHz);
    readMemoryInfo(device, out.memoryShared, out.memoryResident);
    
    // Driver and platform info
    out.driverVersion = readDriverVersion(device.devicePath);
    out.platformName = readPlatformInfo(device.devicePath);
    
    return true;
}

bool GpuBackendIntel::readEngineUtilization(const GpuDevice &device, QVector<IntelGpuStats::EngineStats> &engines) const
{
    // Check cache first
    const QString devicePath = device.devicePath;
    if (m_engineCache.contains(devicePath)) {
        engines = m_engineCache[devicePath];
        
        // Update utilization data for cached engines
        for (IntelGpuStats::EngineStats &engine : engines) {
            QString enginePath = devicePath + "/engine/" + engine.name;
            readSingleEngineStats(enginePath, engine);
        }
        
        return !engines.isEmpty();
    }
    
    // Discover engines and cache the structure
    QVector<IntelGpuStats::EngineStats> discoveredEngines;
    if (readEngineDirectory(devicePath, discoveredEngines)) {
        const_cast<GpuBackendIntel*>(this)->m_engineCache[devicePath] = discoveredEngines;
        engines = discoveredEngines;
        return true;
    }
    
    return false;
}

bool GpuBackendIntel::readEngineDirectory(const QString &devicePath, QVector<IntelGpuStats::EngineStats> &engines) const
{
    QDir engineDir(devicePath + "/engine");
    if (!engineDir.exists()) {
        qCDebug(intelLog) << "Engine directory not found:" << engineDir.path();
        return false;
    }
    
    QStringList engineEntries = engineDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString &engineName : engineEntries) {
        QString enginePath = engineDir.absoluteFilePath(engineName);
        
        IntelGpuStats::EngineStats engine;
        engine.name = engineName;
        engine.className = parseEngineClass(engineName);
        
        if (readSingleEngineStats(enginePath, engine)) {
            engines.append(engine);
            qCDebug(intelLog) << "Found engine:" << engineName << "class:" << engine.className;
        }
    }
    
    return !engines.isEmpty();
}

bool GpuBackendIntel::readSingleEngineStats(const QString &enginePath, IntelGpuStats::EngineStats &engine) const
{
    bool hasData = false;
    
    // Try to read busy_percent first (newer kernels)
    qint64 busyPercent = -1;
    if (readIntegerFile(enginePath + "/busy_percent", busyPercent) && busyPercent >= 0) {
        engine.utilizationPercent = static_cast<int>(busyPercent);
        hasData = true;
    }
    
    // Read busy_ns for additional information
    qint64 busyNs = 0;
    if (readIntegerFile(enginePath + "/busy_ns", busyNs)) {
        engine.busyNs = static_cast<quint64>(busyNs);
        hasData = true;
    }
    
    // Read number of instances
    qint64 instances = 1;
    if (readIntegerFile(enginePath + "/instances", instances)) {
        engine.instances = static_cast<int>(instances);
    }
    
    // Read engine class if available
    QString className;
    if (readFirstLine(enginePath + "/class", className)) {
        engine.className = className.trimmed();
    }
    
    return hasData;
}

bool GpuBackendIntel::readTemperature(const QString &devicePath, int &temperatureC) const
{
    QDir hwmonDir = findHwmonDir(devicePath);
    if (!hwmonDir.exists()) {
        return false;
    }
    
    // Look for temperature input files
    QStringList tempFiles;
    tempFiles << "temp1_input" << "temp2_input" << "temp_input";
    
    for (const QString &tempFile : tempFiles) {
        QString tempPath = hwmonDir.absoluteFilePath(tempFile);
        if (QFile::exists(tempPath)) {
            qint64 milliC = 0;
            if (readIntegerFile(tempPath, milliC)) {
                temperatureC = static_cast<int>(milliC / 1000); // Convert mC to C
                return true;
            }
        }
    }
    
    return false;
}

bool GpuBackendIntel::readPowerUsage(const QString &devicePath, int &powerWatts) const
{
    QDir hwmonDir = findHwmonDir(devicePath);
    if (!hwmonDir.exists()) {
        return false;
    }
    
    // Look for power input files
    QStringList powerFiles;
    powerFiles << "power1_average" << "power1_input" << "power_average" << "power_input";
    
    for (const QString &powerFile : powerFiles) {
        QString powerPath = hwmonDir.absoluteFilePath(powerFile);
        if (QFile::exists(powerPath)) {
            qint64 microW = 0;
            if (readIntegerFile(powerPath, microW)) {
                powerWatts = static_cast<int>(microW / 1000000); // Convert µW to W
                return true;
            }
        }
    }
    
    return false;
}

bool GpuBackendIntel::readGpuFrequency(const QString &devicePath, int &current, int &min, int &max) const
{
    bool hasCurrent = false, hasMin = false, hasMax = false;
    
    // Read current frequency
    qint64 currentFreq = -1;
    if (readIntegerFile(devicePath + "/gt_cur_freq_mhz", currentFreq)) {
        current = static_cast<int>(currentFreq);
        hasCurrent = true;
    }
    
    // Read minimum frequency
    qint64 minFreq = -1;
    if (readIntegerFile(devicePath + "/gt_min_freq_mhz", minFreq)) {
        min = static_cast<int>(minFreq);
        hasMin = true;
    }
    
    // Read maximum frequency
    qint64 maxFreq = -1;
    if (readIntegerFile(devicePath + "/gt_max_freq_mhz", maxFreq)) {
        max = static_cast<int>(maxFreq);
        hasMax = true;
    }
    
    return hasCurrent || hasMin || hasMax;
}

QString GpuBackendIntel::readDriverName(const QString &devicePath) const
{
    QString driverName;
    if (readFirstLine(devicePath + "/driver/module/version", driverName) ||
        readFirstLine(devicePath + "/driver/version", driverName)) {
        return driverName.trimmed();
    }
    
    // Try to read from uevent
    QFile ueventFile(devicePath + "/uevent");
    if (ueventFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&ueventFile);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.startsWith("DRIVER=")) {
                return line.mid(7).trimmed(); // Remove "DRIVER=" prefix
            }
        }
    }
    
    return QString();
}

QString GpuBackendIntel::readDriverVersion(const QString &devicePath) const
{
    QString version = readDriverName(devicePath);
    if (!version.isEmpty()) {
        return version;
    }
    
    // Fallback: determine driver type based on device
    QString deviceId = readDeviceId(devicePath);
    if (!deviceId.isEmpty()) {
        // Modern Intel GPUs typically use i915 driver
        return "i915";
    }
    
    return QString();
}

QString GpuBackendIntel::readPlatformInfo(const QString &devicePath) const
{
    // Try to determine Intel GPU generation from device ID
    QString deviceId = readDeviceId(devicePath);
    if (deviceId.isEmpty()) {
        return QString();
    }
    
    // Parse device ID to determine generation
    // This is a simplified mapping - real implementation would be more comprehensive
    QRegularExpression deviceIdRe(R"(([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4}))");
    QRegularExpressionMatch match = deviceIdRe.match(deviceId);
    
    if (match.hasMatch()) {
        QString vendorId = match.captured(1).toLower();
        QString deviceIdHex = match.captured(2).toLower();
        
        if (vendorId == "8086") { // Intel vendor ID
            // Very simplified generation detection
            if (deviceIdHex.startsWith("46") || deviceIdHex.startsWith("4c")) {
                return "Gen12 (Tiger Lake)";
            } else if (deviceIdHex.startsWith("9b") || deviceIdHex.startsWith("8a")) {
                return "Gen11 (Ice Lake)";
            } else if (deviceIdHex.startsWith("3e") || deviceIdHex.startsWith("87")) {
                return "Gen9.5 (Coffee Lake)";
            } else if (deviceIdHex.startsWith("59") || deviceIdHex.startsWith("5a")) {
                return "Gen9 (Skylake)";
            }
        }
    }
    
    return "Intel GPU";
}

QString GpuBackendIntel::readDeviceId(const QString &devicePath) const
{
    QString deviceId;
    if (readFirstLine(devicePath + "/device", deviceId)) {
        return deviceId.trimmed();
    }
    
    // Try reading from uevent
    QFile ueventFile(devicePath + "/uevent");
    if (ueventFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&ueventFile);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.startsWith("PCI_ID=")) {
                return line.mid(7).trimmed(); // Remove "PCI_ID=" prefix
            }
        }
    }
    
    return QString();
}

QString GpuBackendIntel::parseEngineClass(const QString &engineName) const
{
    if (engineName.startsWith("rcs")) {
        return "Render";
    } else if (engineName.startsWith("bcs")) {
        return "Copy";
    } else if (engineName.startsWith("vcs")) {
        return "Video";
    } else if (engineName.startsWith("vecs")) {
        return "VideoEnhance";
    } else if (engineName.startsWith("ccs")) {
        return "Compute";
    } else {
        return "Unknown";
    }
}

int GpuBackendIntel::calculateAverageUtilization(const QVector<IntelGpuStats::EngineStats> &engines) const
{
    if (engines.isEmpty()) {
        return -1;
    }
    
    int totalUtilization = 0;
    int validEngines = 0;
    
    for (const IntelGpuStats::EngineStats &engine : engines) {
        if (engine.utilizationPercent >= 0) {
            totalUtilization += engine.utilizationPercent;
            validEngines++;
        }
    }
    
    if (validEngines == 0) {
        return -1;
    }
    
    return totalUtilization / validEngines;
}

QDir GpuBackendIntel::findHwmonDir(const QString &devicePath) const
{
    QDir hwmonBaseDir(devicePath + "/hwmon");
    if (!hwmonBaseDir.exists()) {
        return QDir();
    }
    
    // Look for hwmon subdirectories
    QStringList hwmonEntries = hwmonBaseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : hwmonEntries) {
        QDir hwmonDir(hwmonBaseDir.absoluteFilePath(entry));
        if (hwmonDir.exists()) {
            return hwmonDir;
        }
    }
    
    return QDir();
}

QStringList GpuBackendIntel::findHwmonFiles(const QDir &hwmonDir, const QString &pattern) const
{
    if (!hwmonDir.exists()) {
        return QStringList();
    }
    
    QStringList nameFilters;
    nameFilters << pattern;
    
    return hwmonDir.entryList(nameFilters, QDir::Files);
}

QString GpuBackendIntel::getDriverVersion(const GpuDevice &device) const
{
    return readDriverVersion(device.devicePath);
}

QString GpuBackendIntel::getPlatformName(const GpuDevice &device) const
{
    return readPlatformInfo(device.devicePath);
}

QStringList GpuBackendIntel::getAvailableEngines(const GpuDevice &device) const
{
    QVector<IntelGpuStats::EngineStats> engines;
    if (readEngineUtilization(device, engines)) {
        QStringList engineNames;
        for (const IntelGpuStats::EngineStats &engine : engines) {
            engineNames.append(engine.name);
        }
        return engineNames;
    }
    
    return QStringList();
}

bool GpuBackendIntel::readFrequencyInfo(const GpuDevice &device, int &current, int &min, int &max) const
{
    return readGpuFrequency(device.devicePath, current, min, max);
}

bool GpuBackendIntel::readMemoryInfo(const GpuDevice &device, quint64 &shared, quint64 &resident) const
{
    // Intel integrated GPUs typically don't have dedicated VRAM
    // Memory information is usually not available through sysfs
    // This would require more advanced techniques like reading from /proc or debugfs
    
    Q_UNUSED(device)
    shared = 0;
    resident = 0;
    return false;
}

bool GpuBackendIntel::readFirstLine(const QString &filePath, QString &out) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream stream(&file);
    QString line = stream.readLine();
    file.close();
    
    if (line.isNull()) {
        return false;
    }
    
    out = line.trimmed();
    return !out.isEmpty();
}

bool GpuBackendIntel::readIntegerFile(const QString &filePath, qint64 &outValue) const
{
    QString content;
    if (!readFirstLine(filePath, content)) {
        return false;
    }
    
    bool ok = false;
    qint64 value = content.toLongLong(&ok, 0);
    
    if (ok) {
        outValue = value;
        return true;
    }
    
    return false;
}

bool GpuBackendIntel::readFloatFile(const QString &filePath, double &outValue) const
{
    QString content;
    if (!readFirstLine(filePath, content)) {
        return false;
    }
    
    bool ok = false;
    double value = content.toDouble(&ok);
    
    if (ok) {
        outValue = value;
        return true;
    }
    
    return false;
}

bool GpuBackendIntel::readHwmonAttribute(const QString &devicePath, const QString &attribute, qint64 &value) const
{
    QDir hwmonDir = findHwmonDir(devicePath);
    if (!hwmonDir.exists()) {
        return false;
    }
    
    return readIntegerFile(hwmonDir.absoluteFilePath(attribute), value);
}

} // namespace system
} // namespace core
