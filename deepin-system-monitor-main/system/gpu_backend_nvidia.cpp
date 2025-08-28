// Copyright (C) 2025 Deepin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu_backend_nvidia.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QLibrary>
#include <QLoggingCategory>
#include <QDebug>

namespace core {
namespace system {

Q_LOGGING_CATEGORY(nvmlLog, "gpu.nvidia")

// NVML API definitions (extended from gpu.cpp)
typedef int nvmlReturn_t;
typedef void* nvmlDevice_t;

enum nvmlConstants {
    NVML_SUCCESS = 0,
    NVML_TEMPERATURE_GPU = 0,
    NVML_CLOCK_GRAPHICS = 0,
    NVML_CLOCK_MEM = 1,
    NVML_PSTATE_0 = 0,
    NVML_PSTATE_15 = 15
};

struct NvmlUtilizationRates { unsigned int gpu; unsigned int memory; };
struct NvmlMemory { unsigned long long total; unsigned long long free; unsigned long long used; };
struct NvmlProcessInfo {
    unsigned int pid;
    unsigned long long usedGpuMemory;
    unsigned int type; // 0=Graphics, 1=Compute
};

// NVML Function pointers
typedef nvmlReturn_t (*nvmlInit_v2_t)();
typedef nvmlReturn_t (*nvmlShutdown_t)();
typedef nvmlReturn_t (*nvmlDeviceGetCount_v2_t)(unsigned int* deviceCount);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_v2_t)(unsigned int index, nvmlDevice_t* device);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByPciBusId_v2_t)(const char* pciBusId, nvmlDevice_t* device);
typedef nvmlReturn_t (*nvmlDeviceGetName_t)(nvmlDevice_t device, char* name, unsigned int length);
typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_t)(nvmlDevice_t device, NvmlUtilizationRates* utilization);
typedef nvmlReturn_t (*nvmlDeviceGetMemoryInfo_t)(nvmlDevice_t device, NvmlMemory* memory);
typedef nvmlReturn_t (*nvmlDeviceGetTemperature_t)(nvmlDevice_t device, unsigned int sensorType, unsigned int* temp);
typedef nvmlReturn_t (*nvmlDeviceGetPowerUsage_t)(nvmlDevice_t device, unsigned int* power);
typedef nvmlReturn_t (*nvmlDeviceGetClockInfo_t)(nvmlDevice_t device, unsigned int type, unsigned int* clock);
typedef nvmlReturn_t (*nvmlDeviceGetFanSpeed_t)(nvmlDevice_t device, unsigned int* speed);
typedef nvmlReturn_t (*nvmlDeviceGetPerformanceState_t)(nvmlDevice_t device, int* pState);
typedef nvmlReturn_t (*nvmlSystemGetDriverVersion_t)(char* version, unsigned int length);
typedef nvmlReturn_t (*nvmlSystemGetNVMLVersion_t)(char* version, unsigned int length);
typedef nvmlReturn_t (*nvmlDeviceGetVbiosVersion_t)(nvmlDevice_t device, char* version, unsigned int length);
typedef nvmlReturn_t (*nvmlDeviceGetComputeRunningProcesses_t)(nvmlDevice_t device, unsigned int* infoCount, NvmlProcessInfo* infos);
typedef nvmlReturn_t (*nvmlDeviceGetGraphicsRunningProcesses_t)(nvmlDevice_t device, unsigned int* infoCount, NvmlProcessInfo* infos);

class GpuBackendNvidia::NvidiaBackendPrivate
{
public:
    QLibrary nvmlLib;
    bool initialized = false;
    
    // NVML Function pointers
    nvmlInit_v2_t nvmlInit = nullptr;
    nvmlShutdown_t nvmlShutdown = nullptr;
    nvmlDeviceGetCount_v2_t nvmlDeviceGetCount = nullptr;
    nvmlDeviceGetHandleByIndex_v2_t nvmlDeviceGetHandleByIndex = nullptr;
    nvmlDeviceGetHandleByPciBusId_v2_t nvmlDeviceGetHandleByPciBusId = nullptr;
    nvmlDeviceGetName_t nvmlDeviceGetName = nullptr;
    nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates = nullptr;
    nvmlDeviceGetMemoryInfo_t nvmlDeviceGetMemoryInfo = nullptr;
    nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature = nullptr;
    nvmlDeviceGetPowerUsage_t nvmlDeviceGetPowerUsage = nullptr;
    nvmlDeviceGetClockInfo_t nvmlDeviceGetClockInfo = nullptr;
    nvmlDeviceGetFanSpeed_t nvmlDeviceGetFanSpeed = nullptr;
    nvmlDeviceGetPerformanceState_t nvmlDeviceGetPerformanceState = nullptr;
    nvmlSystemGetDriverVersion_t nvmlSystemGetDriverVersion = nullptr;
    nvmlSystemGetNVMLVersion_t nvmlSystemGetNVMLVersion = nullptr;
    nvmlDeviceGetVbiosVersion_t nvmlDeviceGetVbiosVersion = nullptr;
    nvmlDeviceGetComputeRunningProcesses_t nvmlDeviceGetComputeRunningProcesses = nullptr;
    nvmlDeviceGetGraphicsRunningProcesses_t nvmlDeviceGetGraphicsRunningProcesses = nullptr;

    bool resolveFunctions();
    void cleanup();
};

GpuBackendNvidia::GpuBackendNvidia()
    : d_ptr(std::make_unique<NvidiaBackendPrivate>())
{
    initializeNvml();
}

GpuBackendNvidia::~GpuBackendNvidia()
{
    shutdownNvml();
}

bool GpuBackendNvidia::supports(const GpuDevice &device) const
{
    return device.vendor == GpuVendor::Nvidia && isNvmlAvailable();
}

bool GpuBackendNvidia::readStats(const GpuDevice &device, GpuStats &out)
{
    if (!isNvmlAvailable()) {
        qCWarning(nvmlLog) << "NVML not available for device" << device.name;
        return false;
    }

    void* nvmlDevice = nullptr;
    if (!getNvmlDevice(device, &nvmlDevice)) {
        return false;
    }

    auto d = d_ptr.get();
    
    // Read basic stats
    NvmlUtilizationRates utilization {0, 0};
    if (d->nvmlDeviceGetUtilizationRates(nvmlDevice, &utilization) == NVML_SUCCESS) {
        out.utilizationPercent = static_cast<int>(utilization.gpu);
    }

    NvmlMemory memory {0, 0, 0};
    if (d->nvmlDeviceGetMemoryInfo(nvmlDevice, &memory) == NVML_SUCCESS) {
        out.memoryTotalBytes = static_cast<quint64>(memory.total);
        out.memoryUsedBytes = static_cast<quint64>(memory.used);
    }

    unsigned int temperature = 0;
    if (d->nvmlDeviceGetTemperature(nvmlDevice, NVML_TEMPERATURE_GPU, &temperature) == NVML_SUCCESS) {
        out.temperatureC = static_cast<int>(temperature);
    }

    // Read frequency information
    unsigned int graphicsClock = 0;
    if (d->nvmlDeviceGetClockInfo(nvmlDevice, NVML_CLOCK_GRAPHICS, &graphicsClock) == NVML_SUCCESS) {
        // Convert MHz to kHz for consistency with GPU stats structure
        out.coreClockkHz = static_cast<qint64>(graphicsClock) * 1000;
    }

    unsigned int memoryClock = 0;
    if (d->nvmlDeviceGetClockInfo(nvmlDevice, NVML_CLOCK_MEM, &memoryClock) == NVML_SUCCESS) {
        // Convert MHz to kHz for consistency with GPU stats structure
        out.memoryClockkHz = static_cast<qint64>(memoryClock) * 1000;
    }

    return out.utilizationPercent >= 0 || out.memoryTotalBytes > 0 || out.temperatureC >= 0 ||
           out.coreClockkHz > 0 || out.memoryClockkHz > 0;
}

bool GpuBackendNvidia::readExtendedStats(const GpuDevice &device, NvidiaGpuStats &out)
{
    // First read basic stats
    if (!readStats(device, out)) {
        return false;
    }

    void* nvmlDevice = nullptr;
    if (!getNvmlDevice(device, &nvmlDevice)) {
        return false;
    }

    auto d = d_ptr.get();

    // Read extended stats
    unsigned int power = 0;
    if (d->nvmlDeviceGetPowerUsage && d->nvmlDeviceGetPowerUsage(nvmlDevice, &power) == NVML_SUCCESS) {
        out.powerUsageWatts = static_cast<int>(power / 1000); // Convert mW to W
    }

    unsigned int memoryClock = 0;
    if (d->nvmlDeviceGetClockInfo && d->nvmlDeviceGetClockInfo(nvmlDevice, NVML_CLOCK_MEM, &memoryClock) == NVML_SUCCESS) {
        out.memoryClock = static_cast<int>(memoryClock);
    }

    unsigned int graphicsClock = 0;
    if (d->nvmlDeviceGetClockInfo && d->nvmlDeviceGetClockInfo(nvmlDevice, NVML_CLOCK_GRAPHICS, &graphicsClock) == NVML_SUCCESS) {
        out.graphicsClock = static_cast<int>(graphicsClock);
    }

    unsigned int fanSpeed = 0;
    if (d->nvmlDeviceGetFanSpeed && d->nvmlDeviceGetFanSpeed(nvmlDevice, &fanSpeed) == NVML_SUCCESS) {
        out.fanSpeedPercent = static_cast<int>(fanSpeed);
    }

    int pState = -1;
    if (d->nvmlDeviceGetPerformanceState && d->nvmlDeviceGetPerformanceState(nvmlDevice, &pState) == NVML_SUCCESS) {
        out.performanceState = pState;
    }

    // Get versions
    char version[256] = {0};
    if (d->nvmlSystemGetDriverVersion && d->nvmlSystemGetDriverVersion(version, sizeof(version)) == NVML_SUCCESS) {
        out.driverVersion = QString::fromLatin1(version);
    }

    if (d->nvmlDeviceGetVbiosVersion && d->nvmlDeviceGetVbiosVersion(nvmlDevice, version, sizeof(version)) == NVML_SUCCESS) {
        out.vbiosVersion = QString::fromLatin1(version);
    }

    // Read process usages
    readProcessUsages(device, out.processUsages);

    return true;
}

bool GpuBackendNvidia::readProcessUsages(const GpuDevice &device, QVector<NvidiaGpuStats::ProcessUsage> &out)
{
    if (!isNvmlAvailable()) {
        return false;
    }

    void* nvmlDevice = nullptr;
    if (!getNvmlDevice(device, &nvmlDevice)) {
        return false;
    }

    auto d = d_ptr.get();
    out.clear();

    // Read compute processes
    if (d->nvmlDeviceGetComputeRunningProcesses) {
        unsigned int count = 32; // Max processes to query
        NvmlProcessInfo processes[32];
        if (d->nvmlDeviceGetComputeRunningProcesses(nvmlDevice, &count, processes) == NVML_SUCCESS) {
            for (unsigned int i = 0; i < count; ++i) {
                NvidiaGpuStats::ProcessUsage usage;
                usage.pid = processes[i].pid;
                usage.memoryUsed = processes[i].usedGpuMemory;
                usage.type = 1; // Compute
                usage.processName = QString("PID %1").arg(usage.pid); // TODO: Resolve process name
                out.append(usage);
            }
        }
    }

    // Read graphics processes
    if (d->nvmlDeviceGetGraphicsRunningProcesses) {
        unsigned int count = 32;
        NvmlProcessInfo processes[32];
        if (d->nvmlDeviceGetGraphicsRunningProcesses(nvmlDevice, &count, processes) == NVML_SUCCESS) {
            for (unsigned int i = 0; i < count; ++i) {
                NvidiaGpuStats::ProcessUsage usage;
                usage.pid = processes[i].pid;
                usage.memoryUsed = processes[i].usedGpuMemory;
                usage.type = 0; // Graphics
                usage.processName = QString("PID %1").arg(usage.pid); // TODO: Resolve process name
                out.append(usage);
            }
        }
    }

    return true;
}

QString GpuBackendNvidia::getDriverVersion() const
{
    if (!isNvmlAvailable() || !d_ptr->nvmlSystemGetDriverVersion) {
        return QString();
    }

    char version[256] = {0};
    if (d_ptr->nvmlSystemGetDriverVersion(version, sizeof(version)) == NVML_SUCCESS) {
        return QString::fromLatin1(version);
    }
    return QString();
}

QString GpuBackendNvidia::getNvmlVersion() const
{
    if (!isNvmlAvailable() || !d_ptr->nvmlSystemGetNVMLVersion) {
        return QString();
    }

    char version[256] = {0};
    if (d_ptr->nvmlSystemGetNVMLVersion(version, sizeof(version)) == NVML_SUCCESS) {
        return QString::fromLatin1(version);
    }
    return QString();
}

int GpuBackendNvidia::getDeviceCount() const
{
    if (!isNvmlAvailable() || !d_ptr->nvmlDeviceGetCount) {
        return 0;
    }

    unsigned int count = 0;
    if (d_ptr->nvmlDeviceGetCount(&count) == NVML_SUCCESS) {
        return static_cast<int>(count);
    }
    return 0;
}

bool GpuBackendNvidia::initializeNvml()
{
    auto d = d_ptr.get();
    
    // Check if already initialized
    if (d->initialized) {
        return true;
    }
    
    if (qEnvironmentVariableIsSet("DSM_DISABLE_NVML")) {
        qCInfo(nvmlLog) << "NVML disabled by environment variable";
        return false;
    }

    // Find NVML library
    QStringList candidates = getNvmlLibraryCandidates();
    QString libPath;
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            libPath = candidate;
            break;
        }
    }

    if (libPath.isEmpty()) {
        d->nvmlLib.setFileName("nvidia-ml"); // Try system search
    } else {
        d->nvmlLib.setFileName(libPath);
    }

    if (!d->nvmlLib.load()) {
        qCWarning(nvmlLog) << "Failed to load NVML library:" << d->nvmlLib.errorString();
        return false;
    }

    if (!d->resolveFunctions()) {
        qCWarning(nvmlLog) << "Failed to resolve NVML functions";
        d->nvmlLib.unload();
        return false;
    }

    nvmlReturn_t result = d->nvmlInit();
    if (result != NVML_SUCCESS) {
        qCWarning(nvmlLog) << "Failed to initialize NVML, result:" << result;
        d->nvmlLib.unload();
        return false;
    }

    d->initialized = true;
    qCInfo(nvmlLog) << "NVML initialized successfully";
    return true;
}

void GpuBackendNvidia::shutdownNvml()
{
    auto d = d_ptr.get();
    if (d->initialized && d->nvmlShutdown) {
        d->nvmlShutdown();
        d->initialized = false;
    }
    d->cleanup();
}

bool GpuBackendNvidia::isNvmlAvailable() const
{
    return d_ptr->initialized;
}

bool GpuBackendNvidia::getNvmlDevice(const GpuDevice &device, void** nvmlDevice) const
{
    if (!isNvmlAvailable()) {
        return false;
    }

    QByteArray busId = device.pciBusId.toLatin1();
    nvmlReturn_t result = d_ptr->nvmlDeviceGetHandleByPciBusId(busId.constData(), nvmlDevice);
    
    if (result != NVML_SUCCESS) {
        qCWarning(nvmlLog) << "Failed to get NVML device for" << device.pciBusId << "result:" << result;
        return false;
    }
    
    return true;
}

QStringList GpuBackendNvidia::getNvmlLibraryCandidates() const
{
    return {
        "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1",
        "/usr/lib64/libnvidia-ml.so.1",
        "/usr/lib/libnvidia-ml.so.1",
        "/usr/local/cuda/lib64/libnvidia-ml.so.1",
        "libnvidia-ml.so.1",
        "libnvidia-ml.so"
    };
}

bool GpuBackendNvidia::NvidiaBackendPrivate::resolveFunctions()
{
    // Resolve required functions
    nvmlInit = reinterpret_cast<nvmlInit_v2_t>(nvmlLib.resolve("nvmlInit_v2"));
    nvmlShutdown = reinterpret_cast<nvmlShutdown_t>(nvmlLib.resolve("nvmlShutdown"));
    nvmlDeviceGetHandleByPciBusId = reinterpret_cast<nvmlDeviceGetHandleByPciBusId_v2_t>(nvmlLib.resolve("nvmlDeviceGetHandleByPciBusId_v2"));
    nvmlDeviceGetUtilizationRates = reinterpret_cast<nvmlDeviceGetUtilizationRates_t>(nvmlLib.resolve("nvmlDeviceGetUtilizationRates"));
    nvmlDeviceGetMemoryInfo = reinterpret_cast<nvmlDeviceGetMemoryInfo_t>(nvmlLib.resolve("nvmlDeviceGetMemoryInfo"));
    nvmlDeviceGetTemperature = reinterpret_cast<nvmlDeviceGetTemperature_t>(nvmlLib.resolve("nvmlDeviceGetTemperature"));

    if (!nvmlInit || !nvmlShutdown || !nvmlDeviceGetHandleByPciBusId || 
        !nvmlDeviceGetUtilizationRates || !nvmlDeviceGetMemoryInfo || !nvmlDeviceGetTemperature) {
        qCWarning(nvmlLog) << "Failed to resolve required NVML functions";
        return false;
    }

    // Resolve optional extended functions
    nvmlDeviceGetCount = reinterpret_cast<nvmlDeviceGetCount_v2_t>(nvmlLib.resolve("nvmlDeviceGetCount_v2"));
    nvmlDeviceGetPowerUsage = reinterpret_cast<nvmlDeviceGetPowerUsage_t>(nvmlLib.resolve("nvmlDeviceGetPowerUsage"));
    nvmlDeviceGetClockInfo = reinterpret_cast<nvmlDeviceGetClockInfo_t>(nvmlLib.resolve("nvmlDeviceGetClockInfo"));
    nvmlDeviceGetFanSpeed = reinterpret_cast<nvmlDeviceGetFanSpeed_t>(nvmlLib.resolve("nvmlDeviceGetFanSpeed"));
    nvmlDeviceGetPerformanceState = reinterpret_cast<nvmlDeviceGetPerformanceState_t>(nvmlLib.resolve("nvmlDeviceGetPerformanceState"));
    nvmlSystemGetDriverVersion = reinterpret_cast<nvmlSystemGetDriverVersion_t>(nvmlLib.resolve("nvmlSystemGetDriverVersion"));
    nvmlSystemGetNVMLVersion = reinterpret_cast<nvmlSystemGetNVMLVersion_t>(nvmlLib.resolve("nvmlSystemGetNVMLVersion"));
    nvmlDeviceGetVbiosVersion = reinterpret_cast<nvmlDeviceGetVbiosVersion_t>(nvmlLib.resolve("nvmlDeviceGetVbiosVersion"));
    nvmlDeviceGetComputeRunningProcesses = reinterpret_cast<nvmlDeviceGetComputeRunningProcesses_t>(nvmlLib.resolve("nvmlDeviceGetComputeRunningProcesses"));
    nvmlDeviceGetGraphicsRunningProcesses = reinterpret_cast<nvmlDeviceGetGraphicsRunningProcesses_t>(nvmlLib.resolve("nvmlDeviceGetGraphicsRunningProcesses"));

    return true;
}

void GpuBackendNvidia::NvidiaBackendPrivate::cleanup()
{
    if (nvmlLib.isLoaded()) {
        nvmlLib.unload();
    }
    
    // Reset all function pointers
    nvmlInit = nullptr;
    nvmlShutdown = nullptr;
    nvmlDeviceGetCount = nullptr;
    nvmlDeviceGetHandleByIndex = nullptr;
    nvmlDeviceGetHandleByPciBusId = nullptr;
    nvmlDeviceGetName = nullptr;
    nvmlDeviceGetUtilizationRates = nullptr;
    nvmlDeviceGetMemoryInfo = nullptr;
    nvmlDeviceGetTemperature = nullptr;
    nvmlDeviceGetPowerUsage = nullptr;
    nvmlDeviceGetClockInfo = nullptr;
    nvmlDeviceGetFanSpeed = nullptr;
    nvmlDeviceGetPerformanceState = nullptr;
    nvmlSystemGetDriverVersion = nullptr;
    nvmlSystemGetNVMLVersion = nullptr;
    nvmlDeviceGetVbiosVersion = nullptr;
    nvmlDeviceGetComputeRunningProcesses = nullptr;
    nvmlDeviceGetGraphicsRunningProcesses = nullptr;
}

} // namespace system
} // namespace core
