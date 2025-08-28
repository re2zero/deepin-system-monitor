#include "gpu_backend.h"
#include "gpu_backend_nvidia.h"
#include "gpu_backend_amd.h" 
#include "gpu_backend_intel.h"

namespace core { namespace system {

// NvidiaBackend wrapper implementation
NvidiaBackend::NvidiaBackend()
    : m_backend(std::make_unique<GpuBackendNvidia>())
{
}

NvidiaBackend::~NvidiaBackend() = default;

bool NvidiaBackend::supports(const GpuDevice &device) const
{
    return m_backend->supports(device);
}

bool NvidiaBackend::readStats(const GpuDevice &device, GpuStats &out)
{
    return m_backend->readStats(device, out);
}

// AmdBackend wrapper implementation
AmdBackend::AmdBackend()
    : m_backend(std::make_unique<GpuBackendAmd>())
{
}

AmdBackend::~AmdBackend() = default;

bool AmdBackend::supports(const GpuDevice &device) const
{
    return m_backend->supports(device);
}

bool AmdBackend::readStats(const GpuDevice &device, GpuStats &out)
{
    return m_backend->readStats(device, out);
}

// IntelBackend wrapper implementation
IntelBackend::IntelBackend()
    : m_backend(std::make_unique<GpuBackendIntel>())
{
}

IntelBackend::~IntelBackend() = default;

bool IntelBackend::supports(const GpuDevice &device) const
{
    return m_backend->supports(device);
}

bool IntelBackend::readStats(const GpuDevice &device, GpuStats &out)
{
    return m_backend->readStats(device, out);
}

// GpuService implementation
GpuService::GpuService()
{
    m_backends.emplace_back(std::make_unique<NvidiaBackend>());
    m_backends.emplace_back(std::make_unique<AmdBackend>());
    m_backends.emplace_back(std::make_unique<IntelBackend>());
}

const QVector<GpuDevice>& GpuService::devices()
{
    if (m_devices.isEmpty()) {
        m_devices = GpuReader::enumerate();
    }
    return m_devices;
}

bool GpuService::readStatsFor(const GpuDevice &device, GpuStats &out)
{
    for (const auto &b : m_backends) {
        if (b->supports(device)) {
            return b->readStats(device, out);
        }
    }
    return false;
}

}} // namespace core::system
