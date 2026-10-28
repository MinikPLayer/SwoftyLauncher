#pragma once
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <string>

// The core's one set of D3D12 / DXGI hooks. Features subscribe instead of hooking the same entry points
// themselves. Subscribe before arming; callbacks run on the game's thread inside the hooked call.
namespace d3d12hooks {

struct Swapchain {
    void *sc = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    std::string how, by, fg;
    bool streamline = false;
    UINT color_space = 0;
    bool color_space_set = false;
};

using PresentFn = void (*)(IDXGISwapChain3 *sc, const Swapchain *known, void *caller);
using PresentedFn = void (*)(IDXGISwapChain3 *sc, HRESULT hr);
using ResizeFn = void (*)(IDXGISwapChain3 *sc);
using ExecFn = bool (*)(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists);
using BarrierFn = bool (*)(ID3D12GraphicsCommandList *list, UINT n, const D3D12_RESOURCE_BARRIER *barriers);
using SetNameFn = void (*)(ID3D12Object *object, LPCWSTR name);

void on_present(PresentFn fn);
void on_presented(PresentedFn fn);
void on_resize(ResizeFn fn);
void on_exec(ExecFn fn);
void on_barrier(BarrierFn fn);
void on_setname(SetNameFn fn);

bool arm_swapchain();
bool arm_device();

// While one is alive, arm_device and arm_swapchain share one throwaway probe
// device: creating a D3D12 device takes about half a second on the main thread.
struct ProbeScope {
    ProbeScope();
    ~ProbeScope();
    ProbeScope(const ProbeScope &) = delete;
    ProbeScope &operator=(const ProbeScope &) = delete;
};

void exec_original(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists);
void barrier_original(ID3D12GraphicsCommandList *list, UINT n, const D3D12_RESOURCE_BARRIER *barriers);

std::string module_of(const void *addr, HMODULE *mod = nullptr);

}
