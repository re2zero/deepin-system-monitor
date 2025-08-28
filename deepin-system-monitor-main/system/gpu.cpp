// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QLibrary>
#include <QStringList>
#include <QProcess>

namespace core {
namespace system {

// NVML typedefs (partial, minimal surface)
typedef int nvmlReturn_t;
typedef void* nvmlDevice_t;
static constexpr int NVML_TEMPERATURE_GPU = 0;

struct NvmlUtilizationRates { unsigned int gpu; unsigned int memory; };
struct NvmlMemory { unsigned long long total; unsigned long long free; unsigned long long used; };

typedef nvmlReturn_t (*nvmlInit_v2_t)();
typedef nvmlReturn_t (*nvmlShutdown_t)();
typedef nvmlReturn_t (*nvmlDeviceGetHandleByPciBusId_v2_t)(const char* pciBusId, nvmlDevice_t* device);
typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_t)(nvmlDevice_t device, NvmlUtilizationRates* utilization);
typedef nvmlReturn_t (*nvmlDeviceGetMemoryInfo_t)(nvmlDevice_t device, NvmlMemory* memory);
typedef nvmlReturn_t (*nvmlDeviceGetTemperature_t)(nvmlDevice_t device, unsigned int sensorType, unsigned int* temp);

typedef nvmlReturn_t (*nvmlDeviceGetName_t)(nvmlDevice_t device, char* name, unsigned int length);

enum { NVML_SUCCESS = 0 };

static bool nvmlResolve(QLibrary &lib,
                        nvmlInit_v2_t &pInit,
                        nvmlShutdown_t &pShutdown,
                        nvmlDeviceGetHandleByPciBusId_v2_t &pGetByBusId,
                        nvmlDeviceGetUtilizationRates_t &pGetUtil,
                        nvmlDeviceGetMemoryInfo_t &pGetMem,
                        nvmlDeviceGetTemperature_t &pGetTemp)
{
    pInit = reinterpret_cast<nvmlInit_v2_t>(lib.resolve("nvmlInit_v2"));
    pShutdown = reinterpret_cast<nvmlShutdown_t>(lib.resolve("nvmlShutdown"));
    pGetByBusId = reinterpret_cast<nvmlDeviceGetHandleByPciBusId_v2_t>(lib.resolve("nvmlDeviceGetHandleByPciBusId_v2"));
    pGetUtil = reinterpret_cast<nvmlDeviceGetUtilizationRates_t>(lib.resolve("nvmlDeviceGetUtilizationRates"));
    pGetMem = reinterpret_cast<nvmlDeviceGetMemoryInfo_t>(lib.resolve("nvmlDeviceGetMemoryInfo"));
    pGetTemp = reinterpret_cast<nvmlDeviceGetTemperature_t>(lib.resolve("nvmlDeviceGetTemperature"));
    return pInit && pShutdown && pGetByBusId && pGetUtil && pGetMem && pGetTemp;
}

QVector<GpuDevice> GpuReader::enumerate()
{
    QVector<GpuDevice> devices;
    QDir drmDir("/sys/class/drm");
    QStringList entries = drmDir.entryList(QStringList() << "card*", QDir::Dirs | QDir::NoDotAndDotDot);
    QRegularExpression onlyCardDigits("^card\\d+$");
    for (const QString &card : entries) {
        if (!onlyCardDigits.match(card).hasMatch()) continue; // skip connectors like card0-HDMI-A-1
        QString cardPath = drmDir.absoluteFilePath(card);
        QString devicePath = cardPath + "/device";
        if (!QFile::exists(devicePath)) continue;
        GpuVendor vendor = detectVendor(devicePath);
        if (vendor == GpuVendor::Unknown) continue; // ignore non-GPU cards
        GpuDevice dev;
        dev.cardPath = cardPath;
        dev.devicePath = devicePath;
        dev.vendor = vendor;
        dev.pciBusId = detectPciBusId(devicePath);
        dev.name = detectName(devicePath, vendor);
        devices.push_back(dev);
    }
    return devices;
}

bool GpuReader::readStats(const GpuDevice &device, GpuStats &outStats)
{
    switch (device.vendor) {
    case GpuVendor::Nvidia:
        return readStatsNvidia(device, outStats);
    case GpuVendor::AMD:
        return readStatsAmd(device.devicePath, outStats);
    case GpuVendor::Intel:
        return readStatsIntel(device.devicePath, outStats);
    default:
        return false;
    }
}

GpuVendor GpuReader::detectVendor(const QString &devicePath)
{
    QString ven;
    if (!readFirstLine(devicePath + "/vendor", ven)) return GpuVendor::Unknown;
    ven = ven.trimmed().toLower();
    if (ven == "0x10de") return GpuVendor::Nvidia; // NVIDIA
    if (ven == "0x1002" || ven == "0x1022") return GpuVendor::AMD; // AMD/ATI
    if (ven == "0x8086") return GpuVendor::Intel; // Intel
    return GpuVendor::Unknown;
}

QString GpuReader::detectName(const QString &devicePath, GpuVendor vendor)
{
    QString name;
    
    // First try to read product_name file
    if (readFirstLine(devicePath + "/product_name", name)) return name.trimmed();
    
    // Try to get full product name from lspci using PCI bus ID
    QString pciBusId = detectPciBusId(devicePath);
    if (!pciBusId.isEmpty()) {
        QProcess lspci;
        lspci.start("lspci", QStringList() << "-s" << pciBusId);
        if (lspci.waitForFinished(3000)) {
            QString output = QString::fromLocal8Bit(lspci.readAllStandardOutput()).trimmed();
            if (!output.isEmpty()) {
                // Parse lspci output: "01:00.0 VGA compatible controller: NVIDIA Corporation TU116 [GeForce GTX 1660 SUPER] (rev a1)"
                // Extract the part after the second colon
                int firstColon = output.indexOf(':');
                if (firstColon > 0) {
                    int secondColon = output.indexOf(':', firstColon + 1);
                    if (secondColon > 0) {
                        QString productInfo = output.mid(secondColon + 1).trimmed();
                        // Remove revision info like "(rev a1)"
                        int revIndex = productInfo.indexOf(" (rev ");
                        if (revIndex > 0) {
                            productInfo = productInfo.left(revIndex);
                        }
                        // Clean up vendor prefix and extract meaningful product name
                        if (productInfo.contains("NVIDIA Corporation")) {
                            productInfo = productInfo.replace("NVIDIA Corporation ", "");
                        } else if (productInfo.contains("Advanced Micro Devices, Inc. [AMD/ATI]")) {
                            productInfo = productInfo.replace("Advanced Micro Devices, Inc. [AMD/ATI] ", "AMD ");
                        } else if (productInfo.contains("Intel Corporation")) {
                            productInfo = productInfo.replace("Intel Corporation ", "Intel ");
                        }
                        if (!productInfo.isEmpty()) {
                            return productInfo.trimmed();
                        }
                    }
                }
            }
        }
    }
    
    // Fallback to uevent file parsing
    QFile f(devicePath + "/uevent");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (true) {
            QByteArray line = f.readLine();
            if (line.isEmpty()) break;
            if (line.startsWith("DRIVER=")) {
                int eq = line.indexOf('=');
                if (eq >= 0) name = QString::fromLocal8Bit(line.mid(eq + 1)).trimmed();
            } else if (line.startsWith("PCI_ID=")) {
                int eq = line.indexOf('=');
                if (eq >= 0) {
                    QString id = QString::fromLocal8Bit(line.mid(eq + 1)).trimmed();
                    if (!name.isEmpty()) name = name + " (" + id + ")";
                }
            }
        }
        f.close();
    }
    if (!name.isEmpty()) return name;
    
    // Final fallback
    switch (vendor) {
    case GpuVendor::Nvidia: return QStringLiteral("NVIDIA GPU");
    case GpuVendor::AMD: return QStringLiteral("AMD GPU");
    case GpuVendor::Intel: return QStringLiteral("Intel GPU");
    default: return QStringLiteral("GPU");
    }
}

QString GpuReader::detectPciBusId(const QString &devicePath)
{
    QString path = devicePath + "/uevent";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString pciAddr;
    while (true) {
        QByteArray line = f.readLine();
        if (line.isEmpty()) break;
        if (line.startsWith("PCI_SLOT_NAME=")) {
            int eq = line.indexOf('=');
            if (eq >= 0) {
                pciAddr = QString::fromLocal8Bit(line.mid(eq + 1)).trimmed();
                break;
            }
        }
    }
    f.close();
    return pciAddr;
}

bool GpuReader::readStatsNvidia(const GpuDevice &device, GpuStats &outStats)
{
    if (qEnvironmentVariableIsSet("DSM_DISABLE_NVML")) {
        return false;
    }

    // Quick existence check to avoid dlopen hangs on misconfigured systems
    QStringList candidates = {
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
        "/usr/lib64/libnvidia-ml.so.1",
        "/usr/lib/libnvidia-ml.so.1"
    };
    QString libPath;
    for (const QString &c : candidates) {
        if (QFile::exists(c)) { libPath = c; break; }
    }
    QLibrary lib(libPath.isEmpty() ? QStringLiteral("nvidia-ml") : libPath);
    if (!lib.load()) return false;

    nvmlInit_v2_t pInit = nullptr; nvmlShutdown_t pShutdown = nullptr;
    nvmlDeviceGetHandleByPciBusId_v2_t pGetByBusId = nullptr;
    nvmlDeviceGetUtilizationRates_t pGetUtil = nullptr;
    nvmlDeviceGetMemoryInfo_t pGetMem = nullptr;
    nvmlDeviceGetTemperature_t pGetTemp = nullptr;

    if (!nvmlResolve(lib, pInit, pShutdown, pGetByBusId, pGetUtil, pGetMem, pGetTemp)) {
        return false;
    }

    if (pInit() != NVML_SUCCESS) return false;

    nvmlDevice_t h = nullptr;
    QByteArray pci = device.pciBusId.toLatin1();
    nvmlReturn_t rc = pGetByBusId(pci.constData(), &h);
    if (rc != NVML_SUCCESS) { pShutdown(); return false; }

    NvmlUtilizationRates ur {0,0};
    NvmlMemory mem {0,0,0};
    unsigned int temp = 0;

    if (pGetUtil(h, &ur) == NVML_SUCCESS)
        outStats.utilizationPercent = static_cast<int>(ur.gpu);
    if (pGetMem(h, &mem) == NVML_SUCCESS) {
        outStats.memoryTotalBytes = static_cast<quint64>(mem.total);
        outStats.memoryUsedBytes = static_cast<quint64>(mem.used);
    }
    if (pGetTemp(h, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS)
        outStats.temperatureC = static_cast<int>(temp);

    // Read clock frequencies
    readNvmlClockInfo(lib, h, outStats);

    pShutdown();
    return true;
}

static bool readHwmonTempC(const QString &devicePath, int &outTempC)
{
    auto readInteger = [](const QString &filePath, qint64 &outValue) -> bool {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        QTextStream ts(&f);
        QString s = ts.readLine();
        f.close();
        if (s.isEmpty()) return false;
        bool ok = false;
        qint64 v = s.trimmed().toLongLong(&ok, 0);
        if (!ok) return false;
        outValue = v;
        return true;
    };

    QDir hwmonDir(devicePath + "/hwmon");
    if (!hwmonDir.exists()) return false;
    for (const QString &entry : hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir d(hwmonDir.absoluteFilePath(entry));
        QStringList temps = d.entryList(QStringList() << "temp*_input", QDir::Files);
        for (const QString &t : temps) {
            qint64 milliC = 0;
            QString p = d.absoluteFilePath(t);
            if (readInteger(p, milliC)) {
                // value in millidegree Celsius
                outTempC = static_cast<int>(milliC / 1000);
                return true;
            }
        }
    }
    return false;
}

bool GpuReader::readStatsAmd(const QString &devicePath, GpuStats &outStats)
{
    // utilization
    qint64 util = -1;
    (void)readIntegerFile(devicePath + "/gpu_busy_percent", util);
    if (util >= 0) outStats.utilizationPercent = static_cast<int>(util);

    // memory
    qint64 vramUsed = 0, vramTotal = 0;
    if (readIntegerFile(devicePath + "/mem_info_vram_used", vramUsed))
        outStats.memoryUsedBytes = static_cast<quint64>(vramUsed);
    if (readIntegerFile(devicePath + "/mem_info_vram_total", vramTotal))
        outStats.memoryTotalBytes = static_cast<quint64>(vramTotal);

    // temperature
    int tempC = -1;
    if (readHwmonTempC(devicePath, tempC))
        outStats.temperatureC = tempC;
    
    // clock frequencies
    int memoryClock = -1, graphicsClock = -1;
    if (readAmdClockInfo(devicePath, memoryClock, graphicsClock)) {
        if (graphicsClock > 0)
            outStats.coreClockkHz = graphicsClock * 1000; // Convert MHz to kHz
        if (memoryClock > 0)
            outStats.memoryClockkHz = memoryClock * 1000; // Convert MHz to kHz
    }

    return outStats.utilizationPercent >= 0
        || outStats.memoryTotalBytes > 0
        || outStats.temperatureC >= 0
        || outStats.coreClockkHz > 0
        || outStats.memoryClockkHz > 0;
}

bool GpuReader::readStatsIntel(const QString &devicePath, GpuStats &outStats)
{
    // utilization: average engine busy_percent
    QDir engineDir(devicePath + "/engine");
    if (engineDir.exists()) {
        QStringList engines = engineDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        int count = 0; qint64 sum = 0;
        for (const QString &e : engines) {
            qint64 busy = -1;
            readIntegerFile(engineDir.absoluteFilePath(e + "/busy_percent"), busy);
            if (busy >= 0) { sum += busy; count++; }
        }
        if (count > 0) outStats.utilizationPercent = static_cast<int>(sum / count);
    }

    // temperature
    int tempC = -1;
    if (readHwmonTempC(devicePath, tempC))
        outStats.temperatureC = tempC;

    // current frequency
    qint64 currentFreq = -1;
    if (readIntegerFile(devicePath + "/gt_cur_freq_mhz", currentFreq) && currentFreq > 0) {
        outStats.coreClockkHz = static_cast<int>(currentFreq * 1000); // Convert MHz to kHz
    }

    // memory: not universally available for integrated graphics; leave 0
    // Intel integrated GPUs share system memory, so no separate memory frequency
    outStats.memoryClockkHz = -1;

    return outStats.utilizationPercent >= 0 
        || outStats.temperatureC >= 0
        || outStats.coreClockkHz > 0;
}

bool GpuReader::readFirstLine(const QString &filePath, QString &out)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QByteArray line = f.readLine();
    f.close();
    if (line.isEmpty()) return false;
    out = QString::fromLocal8Bit(line).trimmed();
    return !out.isEmpty();
}

bool GpuReader::readIntegerFile(const QString &filePath, qint64 &outValue)
{
    QString s;
    if (!readFirstLine(filePath, s)) return false;
    bool ok = false;
    qint64 v = s.trimmed().toLongLong(&ok, 0);
    if (!ok) return false;
    outValue = v;
    return true;
}

bool GpuReader::readAmdClockInfo(const QString &devicePath, int &memoryClock, int &graphicsClock)
{
    memoryClock = -1;
    graphicsClock = -1;
    bool hasData = false;

    // Read memory clock from pp_dpm_mclk
    QString mclkContent;
    if (readFirstLine(devicePath + "/pp_dpm_mclk", mclkContent)) {
        // Parse DPM content format:
        // 0: 300Mhz
        // 1: 600Mhz *
        // 2: 900Mhz
        // The asterisk (*) indicates the current level
        QStringList lines = mclkContent.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.contains('*')) {
                // Extract frequency from line like "1: 600Mhz *"
                QRegularExpression re(R"((\d+)\s*[MG]hz)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) {
                    memoryClock = match.captured(1).toInt();
                    hasData = true;
                    break;
                }
            }
        }
    }

    // Read graphics clock from pp_dpm_sclk
    QString sclkContent;
    if (readFirstLine(devicePath + "/pp_dpm_sclk", sclkContent)) {
        QStringList lines = sclkContent.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.contains('*')) {
                // Extract frequency from line like "1: 600Mhz *"
                QRegularExpression re(R"((\d+)\s*[MG]hz)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) {
                    graphicsClock = match.captured(1).toInt();
                    hasData = true;
                    break;
                }
            }
        }
    }

    return hasData;
}

void GpuReader::readNvmlClockInfo(QLibrary &lib, void* nvmlDevice, GpuStats &outStats)
{
    // Define NVML constants for clock types
    const unsigned int NVML_CLOCK_GRAPHICS = 0;
    const unsigned int NVML_CLOCK_MEM = 1;
    
    // Get nvmlDeviceGetClockInfo function pointer
    typedef nvmlReturn_t (*nvmlDeviceGetClockInfo_t)(nvmlDevice_t device, unsigned int type, unsigned int* clock);
    nvmlDeviceGetClockInfo_t pGetClockInfo = reinterpret_cast<nvmlDeviceGetClockInfo_t>(
        lib.resolve("nvmlDeviceGetClockInfo"));
    
    if (!pGetClockInfo) {
        return; // Function not available
    }
    
    // Read graphics clock
    unsigned int graphicsClock = 0;
    if (pGetClockInfo(nvmlDevice, NVML_CLOCK_GRAPHICS, &graphicsClock) == NVML_SUCCESS && graphicsClock > 0) {
        outStats.coreClockkHz = static_cast<int>(graphicsClock * 1000); // Convert MHz to kHz
    }
    
    // Read memory clock
    unsigned int memoryClock = 0;
    if (pGetClockInfo(nvmlDevice, NVML_CLOCK_MEM, &memoryClock) == NVML_SUCCESS && memoryClock > 0) {
        outStats.memoryClockkHz = static_cast<int>(memoryClock * 1000); // Convert MHz to kHz
    }
}

} // namespace system
} // namespace core
