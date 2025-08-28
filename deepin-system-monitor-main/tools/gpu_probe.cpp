#include "system/gpu.h"
//#include <QCoreApplication>
#include <QTextStream>

using namespace core::system;

int main(int argc, char **argv)
{
    // Avoid QCoreApplication to prevent any platform/plugin initialization delays.
    QTextStream out(stdout);
    const bool skipNvml = (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--skip-nvml");
    if (skipNvml) qputenv("DSM_DISABLE_NVML", "1");

    // Note: This demo still uses GpuReader which has been updated to use the new backends
    const auto devices = GpuReader::enumerate();
    out << "Found " << devices.size() << " GPU device(s)\n";
    for (const auto &dev : devices) {
        out << "- Device: " << dev.name << "\n";
        out << "  PCI: " << dev.pciBusId << "\n";
        out << "  Vendor: " << (dev.vendor == GpuVendor::Nvidia ? "NVIDIA" : dev.vendor == GpuVendor::AMD ? "AMD" : dev.vendor == GpuVendor::Intel ? "Intel" : "Unknown") << "\n";
        GpuStats stats;
        bool ok = true;
        if (skipNvml && dev.vendor == GpuVendor::Nvidia) {
            ok = false; // explicitly skip NVML in this run
        } else {
            ok = GpuReader::readStats(dev, stats);  // 使用更新后的readStats，现在包含频率数据
        }
        out << "  Read OK: " << (ok ? "yes" : "no") << "\n";
        out << "  Util%: " << stats.utilizationPercent << ", TempC: " << stats.temperatureC
            << ", Mem: " << stats.memoryUsedBytes << "/" << stats.memoryTotalBytes << " bytes\n";
        out << "  CoreClock: " << stats.coreClockkHz << " kHz, MemClock: " << stats.memoryClockkHz << " kHz\n";
        out.flush();
    }
    return 0;
}
