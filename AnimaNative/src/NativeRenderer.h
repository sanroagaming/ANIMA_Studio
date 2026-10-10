#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
#include <memory>

namespace anima::native {
class NativeRenderer {
public:
    NativeRenderer();
    ~NativeRenderer();
    bool Initialize(HWND window);
    void Render(float timeSeconds);
    void Shutdown();
    void ToggleRayTracing();
    void SetRayTracing(bool enabled);
    bool RayTracingEnabled() const;
    bool DxrSupported() const;
    const std::wstring& AdapterName() const;
    std::wstring StatusText() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
