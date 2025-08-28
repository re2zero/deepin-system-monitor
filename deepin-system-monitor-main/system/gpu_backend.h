#ifndef CORE_SYSTEM_GPU_BACKEND_H
#define CORE_SYSTEM_GPU_BACKEND_H

#include "system/gpu.h"
#include <memory>
#include <QVector>
#include <vector>

namespace core { namespace system {

// Forward declarations for specialized backends
class GpuBackendNvidia;
class GpuBackendAmd; 
class GpuBackendIntel;

/**
 * @brief Base GPU backend interface
 */
class GpuBackend {
public:
    virtual ~GpuBackend() = default;
    virtual bool supports(const GpuDevice &device) const = 0;
    virtual bool readStats(const GpuDevice &device, GpuStats &out) = 0;
};

/**
 * @brief NVIDIA GPU backend wrapper
 * 
 * Provides compatibility with the new specialized GpuBackendNvidia
 */
class NvidiaBackend : public GpuBackend {
public:
    NvidiaBackend();
    ~NvidiaBackend() override;
    
    bool supports(const GpuDevice &device) const override;
    bool readStats(const GpuDevice &device, GpuStats &out) override;

private:
    std::unique_ptr<GpuBackendNvidia> m_backend;
};

/**
 * @brief AMD GPU backend wrapper
 * 
 * Provides compatibility with the new specialized GpuBackendAmd
 */
class AmdBackend : public GpuBackend {
public:
    AmdBackend();
    ~AmdBackend() override;
    
    bool supports(const GpuDevice &device) const override;
    bool readStats(const GpuDevice &device, GpuStats &out) override;

private:
    std::unique_ptr<GpuBackendAmd> m_backend;
};

/**
 * @brief Intel GPU backend wrapper
 * 
 * Provides compatibility with the new specialized GpuBackendIntel
 */
class IntelBackend : public GpuBackend {
public:
    IntelBackend();
    ~IntelBackend() override;
    
    bool supports(const GpuDevice &device) const override;
    bool readStats(const GpuDevice &device, GpuStats &out) override;

private:
    std::unique_ptr<GpuBackendIntel> m_backend;
};

class GpuService {
public:
    GpuService();
    const QVector<GpuDevice>& devices();
    bool readStatsFor(const GpuDevice &device, GpuStats &out);

private:
    QVector<GpuDevice> m_devices;
    std::vector<std::unique_ptr<GpuBackend>> m_backends;
};

}} // namespace core::system

#endif // CORE_SYSTEM_GPU_BACKEND_H
