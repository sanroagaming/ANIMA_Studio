#include "AnimaGpu.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;

namespace anima02 {
namespace {
struct SelectedDevice {
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    std::string name;
    bool dxr = false;
    std::size_t dedicatedMemoryMB = 0;
};

std::string Narrow(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), n, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

SelectedDevice OpenBestDevice() {
    SelectedDevice out;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&out.factory));
    if (FAILED(hr)) throw std::runtime_error("DXGI factory creation failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ").");

    // Prefer a discrete NVIDIA adapter (including the RTX 3050) rather than
    // accidentally choosing the integrated GPU or the software WARP adapter.
    ComPtr<IDXGIAdapter1> fallback;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        if (out.factory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(candidate->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        ComPtr<ID3D12Device> probe;
        if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&probe)))) continue;
        if (!fallback) fallback = candidate;
        const std::wstring adapterName(desc.Description);
        if (adapterName.find(L"NVIDIA") != std::wstring::npos || adapterName.find(L"RTX") != std::wstring::npos) {
            out.adapter = candidate;
            out.name = Narrow(desc.Description);
            out.dedicatedMemoryMB = static_cast<std::size_t>(desc.DedicatedVideoMemory / (1024ull * 1024ull));
            break;
        }
    }
    if (!out.adapter && fallback) {
        out.adapter = fallback;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(out.adapter->GetDesc1(&desc))) {
            out.name = Narrow(desc.Description);
            out.dedicatedMemoryMB = static_cast<std::size_t>(desc.DedicatedVideoMemory / (1024ull * 1024ull));
        }
    }
    if (!out.adapter) throw std::runtime_error("No hardware Direct3D 12 adapter was found. Check the Windows GPU driver and DirectX support.");
    hr = D3D12CreateDevice(out.adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&out.device));
    if (FAILED(hr)) throw std::runtime_error("Could not create a Direct3D 12 device.");

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    out.dxr = SUCCEEDED(out.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))
        && options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    return out;
}

D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type; p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN; p.CreationNodeMask = 1; p.VisibleNodeMask = 1;
    return p;
}
D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Alignment = 0; d.Width = bytes;
    d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1; d.SampleDesc.Quality = 0; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    return d;
}
void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " failed (HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + ").");
}
}

GpuStatus QueryGpu() {
    try {
        auto device = OpenBestDevice();
        return { true, device.dxr, device.name, "Direct3D 12 compute", device.dedicatedMemoryMB };
    } catch (const std::exception& e) {
        return { false, false, std::string("Unavailable: ") + e.what(), "Direct3D 12 unavailable", 0 };
    }
}

std::vector<double> ComputeSquaresOnGpu(std::size_t count) {
    if (count == 0 || count > 65536) throw std::runtime_error("gpu.squares expects a count from 1 to 65536.");
    auto selected = OpenBestDevice();
    auto& device = selected.device;
    const UINT elementCount = static_cast<UINT>(count);
    const UINT64 byteCount = static_cast<UINT64>(count) * sizeof(float);

    static constexpr char shader[] = R"HLSL(
RWStructuredBuffer<float> Results : register(u0);
cbuffer Params : register(b0) { uint Count; };
[numthreads(64, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint i = dispatchId.x;
    if (i < Count) { float x = (float)(i + 1); Results[i] = x * x; }
}
)HLSL";

    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "ANIMA_0_2_GPU_SQUARES", nullptr, nullptr,
        "main", "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (FAILED(hr)) {
        std::string detail = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "no compiler details";
        throw std::runtime_error("GPU shader compilation failed: " + detail);
    }

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; range.NumDescriptors = 1;
    range.BaseShaderRegister = 0; range.RegisterSpace = 0; range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0; params[0].Constants.RegisterSpace = 0; params[0].Constants.Num32BitValues = 1;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2; rootDesc.pParameters = params; rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> signature, signatureErrors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &signatureErrors), "D3D12SerializeRootSignature");
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)), "CreateRootSignature");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = root.Get(); psoDesc.CS = { code->GetBufferPointer(), code->GetBufferSize() };
    ComPtr<ID3D12PipelineState> pso;
    Check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)), "CreateComputePipelineState");

    const auto defaultHeap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    const auto resultDesc = BufferDesc(byteCount, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> output;
    Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resultDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)), "Create GPU output buffer");

    const auto readbackHeap = HeapProps(D3D12_HEAP_TYPE_READBACK);
    const auto readbackDesc = BufferDesc(byteCount, D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> readback;
    Check(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Create GPU readback buffer");

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; heapDesc.NodeMask = 0;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptors)), "Create GPU descriptor heap");
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_UNKNOWN; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = 0; uav.Buffer.NumElements = elementCount; uav.Buffer.StructureByteStride = sizeof(float);
    device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, descriptors->GetCPUDescriptorHandleForHeapStart());

    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.Get(), pso.Get(), IID_PPV_ARGS(&list)), "CreateCommandList");
    list->SetComputeRootSignature(root.Get());
    ID3D12DescriptorHeap* heaps[] = { descriptors.Get() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRoot32BitConstant(0, elementCount, 0);
    list->SetComputeRootDescriptorTable(1, descriptors->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch((elementCount + 63) / 64, 1, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    list->CopyResource(readback.Get(), output.Get());
    Check(list->Close(), "Close compute command list");

    D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE; queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; queueDesc.NodeMask = 0;
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
    ID3D12CommandList* lists[] = { list.Get() };
    queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
    Check(queue->Signal(fence.Get(), 1), "Signal compute fence");
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) throw std::runtime_error("Could not create a GPU fence event.");
    if (fence->GetCompletedValue() < 1) {
        Check(fence->SetEventOnCompletion(1, eventHandle), "SetEventOnCompletion");
        WaitForSingleObject(eventHandle, INFINITE);
    }
    CloseHandle(eventHandle);

    float* mapped = nullptr;
    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(byteCount) };
    Check(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map GPU result buffer");
    std::vector<double> values(count);
    for (std::size_t i = 0; i < count; ++i) values[i] = static_cast<double>(mapped[i]);
    D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    return values;
}
}
#else
namespace anima02 {
GpuStatus QueryGpu() {
    return { false, false, "Unavailable: native GPU access is supported by the Windows Direct3D 12 runtime.", "No native GPU backend on this host", 0 };
}
std::vector<double> ComputeSquaresOnGpu(std::size_t) {
    throw std::runtime_error("gpu.squares requires the ANIMA Windows Direct3D 12 runtime.");
}
}
#endif
