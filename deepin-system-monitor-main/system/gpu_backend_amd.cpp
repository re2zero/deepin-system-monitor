// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_backend_amd.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QLoggingCategory>
#include <QDebug>
#include <QDirIterator>

namespace core {
namespace system {

Q_LOGGING_CATEGORY(amdLog, "gpu.amd")

GpuBackendAmd::GpuBackendAmd()
{
    qCDebug(amdLog) << "AMD GPU backend initialized";
}

GpuBackendAmd::~GpuBackendAmd()
{
    qCDebug(amdLog) << "AMD GPU backend destroyed";
}

bool GpuBackendAmd::supports(const GpuDevice &device) const
{
    return device.vendor == GpuVendor::AMD;
}

bool GpuBackendAmd::readStats(const GpuDevice &device, GpuStats &out)
{
    bool hasData = false;
    
    // Read GPU utilization
    int utilization = -1;
    if (readGpuUtilization(device.devicePath, utilization)) {
        out.utilizationPercent = utilization;
        hasData = true;
    }
    
    // Read VRAM usage
    quint64 vramUsed = 0, vramTotal = 0;
    if (readVramInfo(device.devicePath, vramUsed, vramTotal)) {
        out.memoryUsedBytes = vramUsed;
        out.memoryTotalBytes = vramTotal;
        hasData = true;
    }
    
    // Read temperature
    int temperature = -1;
    if (readTemperature(device.devicePath, temperature)) {
        out.temperatureC = temperature;
        hasData = true;
    }
    
    // Read frequency information
    int memoryClock = -1, graphicsClock = -1;
    if (readClockInfo(device.devicePath, memoryClock, graphicsClock)) {
        // Convert MHz to kHz for consistency with GPU stats structure
        if (memoryClock > 0) {
            out.memoryClockkHz = static_cast<qint64>(memoryClock) * 1000;
            hasData = true;
        }
        if (graphicsClock > 0) {
            out.coreClockkHz = static_cast<qint64>(graphicsClock) * 1000;
            hasData = true;
        }
    }
    
    return hasData;
}

bool GpuBackendAmd::readExtendedStats(const GpuDevice &device, AmdGpuStats &out)
{
    // First read basic stats
    if (!readStats(device, out)) {
        return false;
    }
    
    // Read extended AMD-specific information
    readPowerUsage(device.devicePath, out.powerUsageWatts);
    readClockInfo(device.devicePath, out.memoryClock, out.graphicsClock);
    readFanSpeed(device.devicePath, out.fanSpeedRpm, out.fanSpeedPercent);
    
    // Read GTT memory info
    readGttInfo(device.devicePath, out.gttUsed, out.gttTotal);
    
    // Driver version
    out.driverVersion = readDriverVersion(device.devicePath);
    
    return true;
}

bool GpuBackendAmd::readGpuUtilization(const QString &devicePath, int &utilization) const
{
    qint64 util = -1;
    if (readIntegerFile(devicePath + "/gpu_busy_percent", util)) {
        utilization = static_cast<int>(util);
        return util >= 0;
    }
    return false;
}

bool GpuBackendAmd::readVramInfo(const QString &devicePath, quint64 &used, quint64 &total) const
{
    bool hasUsed = false, hasTotal = false;
    
    qint64 vramUsed = 0;
    if (readIntegerFile(devicePath + "/mem_info_vram_used", vramUsed)) {
        used = static_cast<quint64>(vramUsed);
        hasUsed = true;
    }
    
    qint64 vramTotal = 0;
    if (readIntegerFile(devicePath + "/mem_info_vram_total", vramTotal)) {
        total = static_cast<quint64>(vramTotal);
        hasTotal = true;
    }
    
    return hasUsed && hasTotal;
}

bool GpuBackendAmd::readGttInfo(const QString &devicePath, quint64 &used, quint64 &total) const
{
    bool hasUsed = false, hasTotal = false;
    
    qint64 gttUsed = 0;
    if (readIntegerFile(devicePath + "/mem_info_gtt_used", gttUsed)) {
        used = static_cast<quint64>(gttUsed);
        hasUsed = true;
    }
    
    qint64 gttTotal = 0;
    if (readIntegerFile(devicePath + "/mem_info_gtt_total", gttTotal)) {
        total = static_cast<quint64>(gttTotal);
        hasTotal = true;
    }
    
    return hasUsed && hasTotal;
}

bool GpuBackendAmd::readTemperature(const QString &devicePath, int &temperatureC) const
{
    QDir hwmonDir = findHwmonDir(devicePath);
    if (!hwmonDir.exists()) {
        return false;
    }
    
    // Look for temperature input files
    QStringList tempFiles = findHwmonFiles(hwmonDir, "temp*_input");
    
    for (const QString &tempFile : tempFiles) {
        qint64 milliC = 0;
        if (readIntegerFile(hwmonDir.absoluteFilePath(tempFile), milliC)) {
            temperatureC = static_cast<int>(milliC / 1000); // Convert mC to C
            return true;
        }
    }
    
    return false;
}

bool GpuBackendAmd::readPowerUsage(const QString &devicePath, int &powerWatts) const
{
    QDir hwmonDir = findHwmonDir(devicePath);
    if (!hwmonDir.exists()) {
        return false;
    }
    
    // Look for power input files
    QStringList powerFiles = findHwmonFiles(hwmonDir, "power*_average");
    if (powerFiles.isEmpty()) {
        powerFiles = findHwmonFiles(hwmonDir, "power*_input");
    }
    
    for (const QString &powerFile : powerFiles) {
        qint64 microW = 0;
        if (readIntegerFile(hwmonDir.absoluteFilePath(powerFile), microW)) {
            powerWatts = static_cast<int>(microW / 1000000); // Convert µW to W
            return true;
        }
    }
    
    return false;
}

bool GpuBackendAmd::readClockInfo(const QString &devicePath, int &memoryClock, int &graphicsClock) const
{
    bool hasMemClock = false, hasGfxClock = false;
    
    // Read memory clock from pp_dpm_mclk
    QString mclkContent;
    if (readFirstLine(devicePath + "/pp_dpm_mclk", mclkContent)) {
        memoryClock = parseCurrentClockFromDpm(mclkContent);
        hasMemClock = (memoryClock > 0);
    }
    
    // Read graphics clock from pp_dpm_sclk
    QString sclkContent;
    if (readFirstLine(devicePath + "/pp_dpm_sclk", sclkContent)) {
        graphicsClock = parseCurrentClockFromDpm(sclkContent);
        hasGfxClock = (graphicsClock > 0);
    }
    
    return hasMemClock || hasGfxClock;
}

bool GpuBackendAmd::readFanSpeed(const QString &devicePath, int &speedRpm, int &speedPercent) const
{
    QDir hwmonDir = findHwmonDir(devicePath);
    if (!hwmonDir.exists()) {
        return false;
    }
    
    bool hasRpm = false, hasPercent = false;
    
    // Read fan speed RPM
    QStringList fanFiles = findHwmonFiles(hwmonDir, "fan*_input");
    for (const QString &fanFile : fanFiles) {
        qint64 rpm = 0;
        if (readIntegerFile(hwmonDir.absoluteFilePath(fanFile), rpm)) {
            speedRpm = static_cast<int>(rpm);
            hasRpm = true;
            break;
        }
    }
    
    // Calculate percentage if we have RPM and max RPM
    QStringList maxFanFiles = findHwmonFiles(hwmonDir, "fan*_max");
    if (hasRpm && !maxFanFiles.isEmpty()) {
        qint64 maxRpm = 0;
        if (readIntegerFile(hwmonDir.absoluteFilePath(maxFanFiles.first()), maxRpm) && maxRpm > 0) {
            speedPercent = static_cast<int>((speedRpm * 100) / maxRpm);
            hasPercent = true;
        }
    }
    
    return hasRpm || hasPercent;
}

QString GpuBackendAmd::readDriverVersion(const QString &devicePath) const
{
    // Try to get driver version from various sources
    QString version;
    
    // Method 1: Read from driver/version
    if (readFirstLine(devicePath + "/driver/version", version)) {
        return version.trimmed();
    }
    
    // Method 2: Read from modalias and parse
    QString modalias;
    if (readFirstLine(devicePath + "/modalias", modalias)) {
        // modalias format: pci:v00001002d0000... for AMD
        if (modalias.contains("v00001002") || modalias.contains("v00001022")) {
            return "amdgpu"; // Generic AMD driver name
        }
    }
    
    return QString();
}

QDir GpuBackendAmd::findHwmonDir(const QString &devicePath) const
{
    QDir hwmonBaseDir(devicePath + "/hwmon");
    if (!hwmonBaseDir.exists()) {
        return QDir();
    }
    
    // Look for hwmon subdirectories (typically hwmon0, hwmon1, etc.)
    QStringList hwmonEntries = hwmonBaseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : hwmonEntries) {
        QDir hwmonDir(hwmonBaseDir.absoluteFilePath(entry));
        if (hwmonDir.exists()) {
            return hwmonDir;
        }
    }
    
    return QDir();
}

QStringList GpuBackendAmd::findHwmonFiles(const QDir &hwmonDir, const QString &pattern) const
{
    if (!hwmonDir.exists()) {
        return QStringList();
    }
    
    QStringList nameFilters;
    nameFilters << pattern;
    
    return hwmonDir.entryList(nameFilters, QDir::Files);
}

int GpuBackendAmd::parseCurrentClockFromDpm(const QString &dpmContent) const
{
    // Parse DPM content format:
    // 0: 300Mhz
    // 1: 600Mhz *
    // 2: 900Mhz
    // The asterisk (*) indicates the current level
    
    QStringList lines = dpmContent.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.contains('*')) {
            // Extract frequency from line like "1: 600Mhz *"
            QRegularExpression re(R"((\d+)\s*[MG]hz)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
                return match.captured(1).toInt();
            }
        }
    }
    
    return -1;
}

QStringList GpuBackendAmd::parseClockLevels(const QString &dpmContent) const
{
    QStringList levels;
    QStringList lines = dpmContent.split('\n', Qt::SkipEmptyParts);
    
    for (const QString &line : lines) {
        QString trimmedLine = line.trimmed();
        if (!trimmedLine.isEmpty() && trimmedLine.contains(':')) {
            levels.append(trimmedLine);
        }
    }
    
    return levels;
}

QString GpuBackendAmd::getDriverVersion(const GpuDevice &device) const
{
    return readDriverVersion(device.devicePath);
}

QStringList GpuBackendAmd::getAvailableClockLevels(const GpuDevice &device, const QString &clockType) const
{
    QString filePath;
    if (clockType.toLower() == "memory" || clockType.toLower() == "mclk") {
        filePath = device.devicePath + "/pp_dpm_mclk";
    } else if (clockType.toLower() == "graphics" || clockType.toLower() == "sclk") {
        filePath = device.devicePath + "/pp_dpm_sclk";
    } else {
        return QStringList();
    }
    
    QString content;
    if (readFirstLine(filePath, content)) {
        return parseClockLevels(content);
    }
    
    return QStringList();
}

QStringList GpuBackendAmd::getSupportedPowerProfiles(const GpuDevice &device) const
{
    QString content;
    if (readFirstLine(device.devicePath + "/pp_power_profile_mode", content)) {
        QStringList profiles;
        QStringList lines = content.split('\n', Qt::SkipEmptyParts);
        
        for (const QString &line : lines) {
            QString trimmedLine = line.trimmed();
            if (!trimmedLine.isEmpty() && !trimmedLine.startsWith("NUM")) {
                profiles.append(trimmedLine);
            }
        }
        
        return profiles;
    }
    
    return QStringList();
}

bool GpuBackendAmd::readPowerCap(const GpuDevice &device, int &currentWatts, int &maxWatts) const
{
    bool hasCurrent = readPowerUsage(device.devicePath, currentWatts);
    bool hasMax = false;
    
    // Try to read power cap from hwmon
    QDir hwmonDir = findHwmonDir(device.devicePath);
    if (hwmonDir.exists()) {
        QStringList capFiles = findHwmonFiles(hwmonDir, "power*_cap");
        for (const QString &capFile : capFiles) {
            qint64 microW = 0;
            if (readIntegerFile(hwmonDir.absoluteFilePath(capFile), microW)) {
                maxWatts = static_cast<int>(microW / 1000000);
                hasMax = true;
                break;
            }
        }
    }
    
    return hasCurrent || hasMax;
}

bool GpuBackendAmd::readClockFrequencies(const GpuDevice &device, int &memoryClock, int &graphicsClock) const
{
    return readClockInfo(device.devicePath, memoryClock, graphicsClock);
}

bool GpuBackendAmd::readFanInfo(const GpuDevice &device, int &speedRpm, int &speedPercent) const
{
    return readFanSpeed(device.devicePath, speedRpm, speedPercent);
}

bool GpuBackendAmd::readFirstLine(const QString &filePath, QString &out) const
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

bool GpuBackendAmd::readIntegerFile(const QString &filePath, qint64 &outValue) const
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

bool GpuBackendAmd::readFloatFile(const QString &filePath, double &outValue) const
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

bool GpuBackendAmd::readHwmonAttribute(const QString &devicePath, const QString &attribute, qint64 &value) const
{
    QDir hwmonDir = findHwmonDir(devicePath);
    if (!hwmonDir.exists()) {
        return false;
    }
    
    return readIntegerFile(hwmonDir.absoluteFilePath(attribute), value);
}

} // namespace system
} // namespace core
