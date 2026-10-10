#include "NativeRenderer.h"

#include <array>
#include <cstdio>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace anima::native {
namespace {
constexpr UINT kWindowWidth = 1280;
constexpr UINT kWindowHeight = 720;
constexpr UINT kRenderWidth = 800;
constexpr UINT kRenderHeight = 450;
constexpr UINT kFrameCount = 2;

void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " (HRESULT 0x" + [] (HRESULT v) {
        char b[16]{}; std::snprintf(b, sizeof(b), "%08X", static_cast<unsigned>(v)); return std::string(b);
    }(hr) + ")");
}
std::filesystem::path ExecutableFolder() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}
ComPtr<ID3DBlob> Compile(const std::filesystem::path& path, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    const UINT debugFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    const UINT debugFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, flags | debugFlags, 0, &blob, &errors);
    if (FAILED(hr)) {
        std::string detail;
        if (errors && errors->GetBufferPointer()) detail.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw std::runtime_error("Shader compile failed: " + path.string() + " / " + entry + ":\n" + detail);
    }
    return blob;
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}
ComPtr<ID3D12RootSignature> MakeComputeRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0;
    params[1].Constants.RegisterSpace = 0;
    params[1].Constants.Num32BitValues = 4;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> serialized, errors;
    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Serialize compute root signature");
    ComPtr<ID3D12RootSignature> root;
    ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root)), "Create compute root signature");
    return root;
}
ComPtr<ID3D12RootSignature> MakeGraphicsRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &param;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> serialized, errors;
    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Serialize graphics root signature");
    ComPtr<ID3D12RootSignature> root;
    ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root)), "Create graphics root signature");
    return root;
}
}

struct NativeRenderer::Impl {
    HWND hwnd = nullptr;
    bool rayTracing = true;
    bool initialized = false;
    bool dxrSupported = false;
    std::wstring adapterName = L"No adapter";
    std::wstring status = L"Not initialized";
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> allocators;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> shaderHeap;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> backBuffers;
    ComPtr<ID3D12Resource> output;
    ComPtr<ID3D12RootSignature> computeRoot;
    ComPtr<ID3D12RootSignature> graphicsRoot;
    ComPtr<ID3D12PipelineState> computePSO;
    ComPtr<ID3D12PipelineState> graphicsPSO;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 nextFence = 0;
    std::array<UINT64, kFrameCount> frameFence{};
    UINT rtvIncrement = 0;
    UINT srvIncrement = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT index) const {
        auto h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(index) * rtvIncrement; return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptor(UINT index) const {
        auto h = shaderHeap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<UINT64>(index) * srvIncrement; return h;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptor(UINT index) const {
        auto h = shaderHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(index) * srvIncrement; return h;
    }
    void WaitForFrame(UINT index) {
        if (frameFence[index] == 0 || fence->GetCompletedValue() >= frameFence[index]) return;
        ThrowIfFailed(fence->SetEventOnCompletion(frameFence[index], fenceEvent), "Set fence event");
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    void WaitGpu() {
        if (!queue || !fence || !fenceEvent) return;
        const UINT64 value = ++nextFence;
        if (SUCCEEDED(queue->Signal(fence.Get(), value))) {
            if (fence->GetCompletedValue() < value) {
                fence->SetEventOnCompletion(value, fenceEvent);
                WaitForSingleObject(fenceEvent, INFINITE);
            }
        }
    }
    void CreateDevice() {
#ifdef _DEBUG
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) debugController->EnableDebugLayer();
#endif
        ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
        ComPtr<IDXGIAdapter1> firstHardwareAdapter;
        ComPtr<ID3D12Device> firstHardwareDevice;
        DXGI_ADAPTER_DESC1 firstHardwareDesc{};
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> candidate;
            HRESULT hr = factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            ThrowIfFailed(hr, "Enumerate GPU adapter");
            DXGI_ADAPTER_DESC1 desc{}; candidate->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            ComPtr<ID3D12Device> testDevice;
            if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&testDevice)))) continue;
            // Prefer NVIDIA (PCI vendor 0x10DE), then use the first supported
            // high-performance physical adapter if NVIDIA is absent.
            if (desc.VendorId == 0x10DE) {
                adapter = candidate; device = testDevice; adapterName = desc.Description; break;
            }
            if (!firstHardwareAdapter) {
                firstHardwareAdapter = candidate; firstHardwareDevice = testDevice; firstHardwareDesc = desc;
            }
        }
        if (!device && firstHardwareAdapter) {
            adapter = firstHardwareAdapter; device = firstHardwareDevice; adapterName = firstHardwareDesc.Description;
        }
        if (!device) {
            // Explicit WARP fallback only after all physical adapters have been tried.
            ComPtr<IDXGIAdapter> warp;
            ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "Enumerate WARP fallback");
            ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "Create D3D12 WARP fallback");
            adapterName = L"Microsoft WARP (software fallback; not the RTX GPU)";
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        dxrSupported = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) && options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; q.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL; q.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; q.NodeMask = 0;
        ThrowIfFailed(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "Create command queue");
    }
    void CreateSwapChain() {
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = kWindowWidth; d.Height = kWindowHeight; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Stereo = FALSE; d.SampleDesc.Count = 1; d.SampleDesc.Quality = 0;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = kFrameCount;
        d.Scaling = DXGI_SCALING_STRETCH; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; d.Flags = 0;
        ComPtr<IDXGISwapChain1> sc1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &d, nullptr, nullptr, &sc1), "Create swap chain");
        ThrowIfFailed(sc1.As(&swapChain), "Query IDXGISwapChain3");
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        D3D12_DESCRIPTOR_HEAP_DESC rh{}; rh.NumDescriptors = kFrameCount; rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtvHeap)), "Create RTV heap");
        rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < kFrameCount; ++i) {
            ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i])), "Get swap-chain buffer");
            device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, Rtv(i));
            ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])), "Create command allocator");
        }
    }
    void CreateOutput() {
        D3D12_DESCRIPTOR_HEAP_DESC h{}; h.NumDescriptors = 2; h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&shaderHeap)), "Create shader descriptor heap");
        srvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Alignment = 0; d.Width = kRenderWidth; d.Height = kRenderHeight;
        d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.SampleDesc.Quality = 0;
        d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask = 1; heap.VisibleNodeMask = 1;
        ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)), "Create GPU output texture");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = d.Format; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, CpuDescriptor(0));
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = d.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(output.Get(), &srv, CpuDescriptor(1));
    }
    void CreatePipelines() {
        const auto shaders = ExecutableFolder() / L"Shaders";
        const auto cs = Compile(shaders / L"room_compute.hlsl", "CSMain", "cs_5_1");
        const auto vs = Compile(shaders / L"blit.hlsl", "VSMain", "vs_5_1");
        const auto ps = Compile(shaders / L"blit.hlsl", "PSMain", "ps_5_1");
        computeRoot = MakeComputeRootSignature(device.Get());
        graphicsRoot = MakeGraphicsRootSignature(device.Get());
        D3D12_COMPUTE_PIPELINE_STATE_DESC c{}; c.pRootSignature = computeRoot.Get(); c.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        ThrowIfFailed(device->CreateComputePipelineState(&c, IID_PPV_ARGS(&computePSO)), "Create compute pipeline");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC g{}; g.pRootSignature = graphicsRoot.Get();
        g.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; g.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        g.BlendState.AlphaToCoverageEnable = FALSE; g.BlendState.IndependentBlendEnable = FALSE;
        auto& b = g.BlendState.RenderTarget[0]; b.BlendEnable = FALSE; b.LogicOpEnable = FALSE;
        b.SrcBlend = D3D12_BLEND_ONE; b.DestBlend = D3D12_BLEND_ZERO; b.BlendOp = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D12_BLEND_ONE; b.DestBlendAlpha = D3D12_BLEND_ZERO; b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        b.LogicOp = D3D12_LOGIC_OP_NOOP; b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        g.SampleMask = UINT_MAX;
        g.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; g.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        g.RasterizerState.FrontCounterClockwise = FALSE; g.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        g.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP; g.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        g.RasterizerState.DepthClipEnable = TRUE; g.RasterizerState.MultisampleEnable = FALSE; g.RasterizerState.AntialiasedLineEnable = FALSE;
        g.RasterizerState.ForcedSampleCount = 0; g.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        g.DepthStencilState.DepthEnable = FALSE; g.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; g.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        g.DepthStencilState.StencilEnable = FALSE;
        g.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        g.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        D3D12_DEPTH_STENCILOP_DESC stencilFace{};
        stencilFace.StencilFailOp = D3D12_STENCIL_OP_KEEP; stencilFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        stencilFace.StencilPassOp = D3D12_STENCIL_OP_KEEP; stencilFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        g.DepthStencilState.FrontFace = stencilFace; g.DepthStencilState.BackFace = stencilFace;
        g.InputLayout = { nullptr, 0 };
        g.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; g.NumRenderTargets = 1; g.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        g.DSVFormat = DXGI_FORMAT_UNKNOWN; g.SampleDesc.Count = 1; g.SampleDesc.Quality = 0;
        ThrowIfFailed(device->CreateGraphicsPipelineState(&g, IID_PPV_ARGS(&graphicsPSO)), "Create graphics pipeline");
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].Get(), computePSO.Get(), IID_PPV_ARGS(&commandList)), "Create command list");
        ThrowIfFailed(commandList->Close(), "Close initial command list");
    }
};

NativeRenderer::NativeRenderer() : m_impl(std::make_unique<Impl>()) {}
NativeRenderer::~NativeRenderer() { Shutdown(); }
bool NativeRenderer::Initialize(HWND window) {
    auto& r = *m_impl; r.hwnd = window;
    try {
        r.CreateDevice(); r.CreateSwapChain(); r.CreateOutput(); r.CreatePipelines();
        ThrowIfFailed(r.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.fence)), "Create GPU fence");
        r.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!r.fenceEvent) throw std::runtime_error("Could not create GPU fence event.");
        r.initialized = true;
        r.status = r.dxrSupported ? L"D3D12 compute active; DXR hardware support detected" : L"D3D12 compute active; DXR hardware support not detected";
        return true;
    } catch (const std::exception& e) {
        const std::string narrow = e.what();
        r.status.assign(narrow.begin(), narrow.end());
        return false;
    }
}
void NativeRenderer::Render(float timeSeconds) {
    auto& r = *m_impl;
    if (!r.initialized) return;
    const UINT frame = r.swapChain->GetCurrentBackBufferIndex();
    r.WaitForFrame(frame);
    ThrowIfFailed(r.allocators[frame]->Reset(), "Reset frame allocator");
    ThrowIfFailed(r.commandList->Reset(r.allocators[frame].Get(), r.computePSO.Get()), "Reset command list");
    ID3D12DescriptorHeap* heaps[] = { r.shaderHeap.Get() };
    r.commandList->SetDescriptorHeaps(1, heaps);
    r.commandList->SetComputeRootSignature(r.computeRoot.Get());
    r.commandList->SetPipelineState(r.computePSO.Get());
    r.commandList->SetComputeRootDescriptorTable(0, r.GpuDescriptor(0));
    const UINT constants[4] = { kRenderWidth, kRenderHeight, std::bit_cast<UINT>(timeSeconds), r.rayTracing ? 1u : 0u };
    r.commandList->SetComputeRoot32BitConstants(1, 4, constants, 0);
    r.commandList->Dispatch((kRenderWidth + 7) / 8, (kRenderHeight + 7) / 8, 1);
    auto toSrv = Transition(r.output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    r.commandList->ResourceBarrier(1, &toSrv);
    auto toRt = Transition(r.backBuffers[frame].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    r.commandList->ResourceBarrier(1, &toRt);
    const auto rtv = r.Rtv(frame);
    r.commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight), 0.0f, 1.0f };
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(kWindowWidth), static_cast<LONG>(kWindowHeight) };
    r.commandList->RSSetViewports(1, &vp); r.commandList->RSSetScissorRects(1, &sc);
    const float clear[4] = { 0.02f,0.025f,0.02f,1.0f }; r.commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
    r.commandList->SetGraphicsRootSignature(r.graphicsRoot.Get());
    r.commandList->SetPipelineState(r.graphicsPSO.Get());
    r.commandList->SetGraphicsRootDescriptorTable(0, r.GpuDescriptor(1));
    r.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.commandList->DrawInstanced(3,1,0,0);
    auto toPresent = Transition(r.backBuffers[frame].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    auto toUav = Transition(r.output.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_RESOURCE_BARRIER barriers[2] = { toPresent, toUav };
    r.commandList->ResourceBarrier(2, barriers);
    ThrowIfFailed(r.commandList->Close(), "Close command list");
    ID3D12CommandList* lists[] = { r.commandList.Get() };
    r.queue->ExecuteCommandLists(1, lists);
    HRESULT presentHr = r.swapChain->Present(1, 0);
    if (FAILED(presentHr)) ThrowIfFailed(presentHr, "Present frame");
    r.frameFence[frame] = ++r.nextFence;
    ThrowIfFailed(r.queue->Signal(r.fence.Get(), r.frameFence[frame]), "Signal frame fence");
}
void NativeRenderer::Shutdown() {
    if (!m_impl) return;
    auto& r = *m_impl;
    if (r.initialized) r.WaitGpu();
    if (r.fenceEvent) { CloseHandle(r.fenceEvent); r.fenceEvent = nullptr; }
    r.initialized = false;
}
void NativeRenderer::ToggleRayTracing() { SetRayTracing(!m_impl->rayTracing); }
void NativeRenderer::SetRayTracing(bool enabled) { m_impl->rayTracing = enabled; }
bool NativeRenderer::RayTracingEnabled() const { return m_impl->rayTracing; }
bool NativeRenderer::DxrSupported() const { return m_impl->dxrSupported; }
const std::wstring& NativeRenderer::AdapterName() const { return m_impl->adapterName; }
std::wstring NativeRenderer::StatusText() const { return m_impl->status; }
}
