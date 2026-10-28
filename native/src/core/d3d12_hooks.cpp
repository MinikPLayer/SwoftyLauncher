#include "d3d12_hooks.hpp"

#include "hud_guard.hpp"

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

namespace d3d12hooks {

namespace {

template <class Fn> struct Subs {
    Fn fns[16] = {};
    std::atomic<int> n{0};

    void add(Fn fn)
    {
        int at = n.load();
        if (at < 16) {
            fns[at] = fn;
            n.store(at + 1);
        }
    }
};

Subs<PresentFn> presents;
Subs<PresentedFn> presenteds;
Subs<ResizeFn> resizes;
Subs<ExecFn> execs;
Subs<BarrierFn> barriers;
Subs<SetNameFn> setnames;

template <class T> void release(T *&p)
{
    if (p)
        p->Release();
    p = nullptr;
}

std::mutex lock;
std::vector<Swapchain> chains;
std::vector<std::pair<void *, UINT>> pending_color_space;

safetyhook::InlineHook h_present, h_resize, h_resize1, h_color_space, h_create, h_create_hwnd, h_create_core,
    h_create_comp, h_exec, h_barrier, h_setname;
HMODULE impl_create, impl_hwnd, impl_core, impl_comp;
constexpr size_t SETNAME_SLOT = 6, EXEC_SLOT = 10, BARRIER_SLOT = 26;
thread_local int creating;
std::atomic<bool> in_present;

std::string self_module()
{
    static std::string name = module_of(reinterpret_cast<const void *>(&self_module));
    return name;
}

void captured(const char *how, IUnknown *device, void *sc, UINT w, UINT h, DXGI_FORMAT fmt, UINT buffers, HMODULE impl)
{
    void *frames[48];
    USHORT n = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    std::vector<std::string> mods;
    for (USHORT i = 0; i < n; i++)
        mods.push_back(module_of(frames[i]));
    char impl_path[MAX_PATH] = {};
    DWORD impl_n = impl ? GetModuleFileNameA(impl, impl_path, MAX_PATH) : 0;
    std::string impl_name(hud::guard::base_name(std::string_view(impl_path, impl_n)));
    std::string by = hud::guard::chain(mods, self_module(), impl_name);
    ID3D12CommandQueue *q = nullptr;
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&q))) || !q) {
        swofty::log("[d3d12] swapchain %p (%s, by %s) was not created on a D3D12 command queue; ignored", sc, how,
                    by.c_str());
        return;
    }
    D3D12_COMMAND_QUEUE_DESC qd = q->GetDesc();
    q->Release();
    Swapchain c;
    c.sc = sc;
    c.queue = q;
    c.how = how;
    c.by = by;
    c.fg = hud::guard::fg_in(mods);
    c.streamline = hud::guard::streamline_in(mods);
    if (qd.Type != D3D12_COMMAND_LIST_TYPE_DIRECT && c.fg.empty())
        c.fg = "a non-direct present queue";
    {
        std::lock_guard<std::mutex> g(lock);
        auto pend = std::find_if(pending_color_space.begin(), pending_color_space.end(),
                                 [&](const std::pair<void *, UINT> &x) { return x.first == sc; });
        if (pend != pending_color_space.end()) {
            c.color_space = pend->second;
            c.color_space_set = true;
            pending_color_space.erase(pend);
        }
        auto it = std::find_if(chains.begin(), chains.end(), [&](const Swapchain &x) { return x.sc == sc; });
        if (it != chains.end())
            *it = c;
        else
            chains.push_back(c);
    }
    swofty::log("[d3d12] swapchain %p created through %s on queue %p (type %d, %ux%u, format %d, %u buffer(s)); "
                "created by %s%s%s",
                sc, how, static_cast<void *>(q), int(qd.Type), w, h, int(fmt), buffers, by.c_str(),
                c.fg.empty() ? (c.streamline ? "; through Streamline's frame generation hook" : "")
                             : "; frame generation: ",
                c.fg.c_str());
}

HRESULT STDMETHODCALLTYPE create_hook(IDXGIFactory *f, IUnknown *dev, DXGI_SWAP_CHAIN_DESC *d, IDXGISwapChain **out)
{
    creating++;
    HRESULT hr = h_create.call<HRESULT>(f, dev, d, out);
    if (--creating == 0 && SUCCEEDED(hr) && out && *out && d)
        captured("IDXGIFactory::CreateSwapChain", dev, *out, d->BufferDesc.Width, d->BufferDesc.Height,
                 d->BufferDesc.Format, d->BufferCount, impl_create);
    return hr;
}

HRESULT STDMETHODCALLTYPE create_hwnd_hook(IDXGIFactory2 *f, IUnknown *dev, HWND wnd, const DXGI_SWAP_CHAIN_DESC1 *d,
                                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs, IDXGIOutput *o,
                                           IDXGISwapChain1 **out)
{
    creating++;
    HRESULT hr = h_create_hwnd.call<HRESULT>(f, dev, wnd, d, fs, o, out);
    if (--creating == 0 && SUCCEEDED(hr) && out && *out && d)
        captured("CreateSwapChainForHwnd", dev, *out, d->Width, d->Height, d->Format, d->BufferCount, impl_hwnd);
    return hr;
}

HRESULT STDMETHODCALLTYPE create_core_hook(IDXGIFactory2 *f, IUnknown *dev, IUnknown *wnd,
                                           const DXGI_SWAP_CHAIN_DESC1 *d, IDXGIOutput *o, IDXGISwapChain1 **out)
{
    creating++;
    HRESULT hr = h_create_core.call<HRESULT>(f, dev, wnd, d, o, out);
    if (--creating == 0 && SUCCEEDED(hr) && out && *out && d)
        captured("CreateSwapChainForCoreWindow", dev, *out, d->Width, d->Height, d->Format, d->BufferCount, impl_core);
    return hr;
}

HRESULT STDMETHODCALLTYPE create_comp_hook(IDXGIFactory2 *f, IUnknown *dev, const DXGI_SWAP_CHAIN_DESC1 *d,
                                           IDXGIOutput *o, IDXGISwapChain1 **out)
{
    creating++;
    HRESULT hr = h_create_comp.call<HRESULT>(f, dev, d, o, out);
    if (--creating == 0 && SUCCEEDED(hr) && out && *out && d)
        captured("CreateSwapChainForComposition", dev, *out, d->Width, d->Height, d->Format, d->BufferCount,
                 impl_comp);
    return hr;
}

std::string fg_caller(void *caller)
{
    static void *last;
    static std::string last_fg;
    if (caller == last)
        return last_fg;
    std::string name = module_of(caller);
    last = caller;
    last_fg = hud::guard::fg_module(name) && !hud::guard::streamline_hook(name) ? name : std::string();
    return last_fg;
}

struct PresentSeen {
    void *sc, *caller;
    DWORD tid;
};

std::mutex present_lock;
std::vector<PresentSeen> present_seen;
ULONGLONG present_since;
unsigned present_count;

void count_present(IDXGISwapChain3 *sc, void *caller)
{
    DWORD tid = GetCurrentThreadId();
    ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> g(present_lock);
    present_count++;
    if (!present_since)
        present_since = now;
    bool fresh = present_seen.size() < 32 &&
                 std::none_of(present_seen.begin(), present_seen.end(), [&](const PresentSeen &p) {
                     return p.sc == sc && p.caller == caller && p.tid == tid;
                 });
    if (fresh) {
        present_seen.push_back({sc, caller, tid});
        swofty::log("[d3d12] Present on swapchain %p from %s (%p) on thread %lu", static_cast<void *>(sc),
                    module_of(caller).c_str(), caller, tid);
    }
    if (now - present_since >= 60000) {
        swofty::log("[d3d12] last %llu s: %u present(s), %.1f per second",
                    static_cast<unsigned long long>((now - present_since) / 1000), present_count,
                    present_count * 1000.0 / double(now - present_since));
        present_since = now;
        present_count = 0;
    }
}

HRESULT STDMETHODCALLTYPE present_hook(IDXGISwapChain3 *sc, UINT sync, UINT flags)
{
    if (!(flags & DXGI_PRESENT_TEST))
        count_present(sc, __builtin_return_address(0));
    if (!(flags & DXGI_PRESENT_TEST) && presents.n.load() && !in_present.exchange(true)) {
        void *caller = __builtin_return_address(0);
        std::string pfg = fg_caller(caller);
        Swapchain c;
        bool known = false;
        {
            std::lock_guard<std::mutex> g(lock);
            auto it = std::find_if(chains.begin(), chains.end(), [&](const Swapchain &x) { return x.sc == sc; });
            if (it != chains.end()) {
                if (it->fg.empty() && !pfg.empty())
                    it->fg = pfg + " (calls Present)";
                c = *it;
                known = true;
            }
        }
        for (int i = 0, n = presents.n.load(); i < n; i++)
            presents.fns[i](sc, known ? &c : nullptr, caller);
        in_present.store(false);
    }
    HRESULT hr = h_present.call<HRESULT>(sc, sync, flags);
    for (int i = 0, n = presenteds.n.load(); i < n; i++)
        presenteds.fns[i](sc, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE resize_hook(IDXGISwapChain3 *sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
{
    for (int i = 0, n = resizes.n.load(); i < n; i++)
        resizes.fns[i](sc);
    return h_resize.call<HRESULT>(sc, count, w, h, fmt, flags);
}

HRESULT STDMETHODCALLTYPE resize1_hook(IDXGISwapChain3 *sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags,
                                       const UINT *mask, IUnknown *const *queues)
{
    for (int i = 0, n = resizes.n.load(); i < n; i++)
        resizes.fns[i](sc);
    if (queues && count) {
        std::lock_guard<std::mutex> g(lock);
        for (auto &c : chains)
            if (c.sc == sc && c.fg.empty()) {
                c.fg = "ResizeBuffers1 with per-buffer present queues";
                swofty::log("[d3d12] swapchain %p now presents through per-buffer queues; features leave it alone",
                            static_cast<void *>(sc));
            }
    }
    return h_resize1.call<HRESULT>(sc, count, w, h, fmt, flags, mask, queues);
}

HRESULT STDMETHODCALLTYPE color_space_hook(IDXGISwapChain3 *sc, DXGI_COLOR_SPACE_TYPE cs)
{
    HRESULT hr = h_color_space.call<HRESULT>(sc, cs);
    std::lock_guard<std::mutex> g(lock);
    auto it = std::find_if(chains.begin(), chains.end(), [&](const Swapchain &x) { return x.sc == sc; });
    if (it == chains.end()) {
        auto pend = std::find_if(pending_color_space.begin(), pending_color_space.end(),
                                 [&](const std::pair<void *, UINT> &x) { return x.first == sc; });
        if (SUCCEEDED(hr)) {
            if (pend != pending_color_space.end())
                pend->second = UINT(cs);
            else if (pending_color_space.size() < 16)
                pending_color_space.push_back({sc, UINT(cs)});
        }
        swofty::log("[d3d12] SetColorSpace1(%d) on swapchain %p before its creation was recorded -> 0x%08lx",
                    int(cs), static_cast<void *>(sc), static_cast<unsigned long>(hr));
    } else if (SUCCEEDED(hr) && (!it->color_space_set || it->color_space != UINT(cs))) {
        it->color_space = UINT(cs);
        it->color_space_set = true;
        swofty::log("[d3d12] swapchain %p colour space %d", static_cast<void *>(sc), int(cs));
    } else if (FAILED(hr)) {
        swofty::log("[d3d12] SetColorSpace1(%d) on swapchain %p failed: 0x%08lx", int(cs), static_cast<void *>(sc),
                    static_cast<unsigned long>(hr));
    }
    return hr;
}

void STDMETHODCALLTYPE exec_hook(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists)
{
    for (int i = 0, k = execs.n.load(); i < k; i++)
        if (execs.fns[i](q, n, lists))
            return;
    h_exec.call<void>(q, n, lists);
}

void STDMETHODCALLTYPE barrier_hook(ID3D12GraphicsCommandList *list, UINT n, const D3D12_RESOURCE_BARRIER *b)
{
    for (int i = 0, k = barriers.n.load(); i < k; i++)
        if (barriers.fns[i](list, n, b))
            return;
    h_barrier.call<void>(list, n, b);
}

HRESULT STDMETHODCALLTYPE setname_hook(ID3D12Object *o, LPCWSTR name)
{
    for (int i = 0, k = setnames.n.load(); i < k; i++)
        setnames.fns[i](o, name);
    return h_setname.call<HRESULT>(o, name);
}

bool install(safetyhook::InlineHook &h, void *target, void *detour, const char *what)
{
    auto made = safetyhook::InlineHook::create(target, detour);
    if (!made) {
        swofty::log("[d3d12] hooking %s at %p failed", what, target);
        return false;
    }
    h = std::move(*made);
    return true;
}

bool hook_factory(void **vt)
{
    struct Want {
        int slot;
        safetyhook::InlineHook *h;
        void *detour;
        HMODULE *impl;
        const char *what;
        bool required;
    } wants[] = {
        {15, &h_create_hwnd, reinterpret_cast<void *>(&create_hwnd_hook), &impl_hwnd, "CreateSwapChainForHwnd", true},
        {10, &h_create, reinterpret_cast<void *>(&create_hook), &impl_create, "CreateSwapChain", false},
        {16, &h_create_core, reinterpret_cast<void *>(&create_core_hook), &impl_core, "CreateSwapChainForCoreWindow",
         false},
        {24, &h_create_comp, reinterpret_cast<void *>(&create_comp_hook), &impl_comp, "CreateSwapChainForComposition",
         false},
    };
    std::vector<void *> done;
    std::string list;
    for (auto &w : wants) {
        void *target = vt[w.slot];
        if (std::find(done.begin(), done.end(), target) != done.end()) {
            swofty::log("[d3d12] IDXGIFactory %s shares its code with another hooked slot; not hooked twice", w.what);
            continue;
        }
        module_of(target, w.impl);
        if (!install(*w.h, target, w.detour, w.what)) {
            if (w.required)
                return false;
            continue;
        }
        done.push_back(target);
        char b[64];
        snprintf(b, sizeof b, "%s%s (vt+0x%x)", list.empty() ? "" : ", ", w.what, unsigned(w.slot * 8));
        list += b;
    }
    swofty::log("[d3d12] swapchain creation hooked on the DXGI factory (%s, in %s)", list.c_str(),
                module_of(vt[15]).c_str());
    return true;
}

using CreateFactoryFn = HRESULT(WINAPI *)(REFIID, void **);

std::mutex probe_lock;
ID3D12Device *probe_held;
int probe_scopes;

ID3D12Device *probe_device()
{
    std::lock_guard<std::mutex> g(probe_lock);
    if (probe_held) {
        probe_held->AddRef();
        return probe_held;
    }
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    auto create = d3d12 ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
    ID3D12Device *dev = nullptr;
    if (!create || FAILED(create(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev))))
        return nullptr;
    if (probe_scopes) {
        dev->AddRef();
        probe_held = dev;
    }
    return dev;
}

}

void on_present(PresentFn fn)
{
    presents.add(fn);
}

void on_presented(PresentedFn fn)
{
    presenteds.add(fn);
}

void on_resize(ResizeFn fn)
{
    resizes.add(fn);
}

void on_exec(ExecFn fn)
{
    execs.add(fn);
}

void on_barrier(BarrierFn fn)
{
    barriers.add(fn);
}

void on_setname(SetNameFn fn)
{
    setnames.add(fn);
}

void exec_original(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists)
{
    if (h_exec && hud::guard::vtable_slot_is(q, EXEC_SLOT, h_exec.target()))
        h_exec.call<void>(q, n, lists);
    else
        q->ExecuteCommandLists(n, lists);
}

void barrier_original(ID3D12GraphicsCommandList *list, UINT n, const D3D12_RESOURCE_BARRIER *b)
{
    if (h_barrier && hud::guard::vtable_slot_is(list, BARRIER_SLOT, h_barrier.target()))
        h_barrier.call<void>(list, n, b);
    else
        list->ResourceBarrier(n, b);
}

std::string module_of(const void *addr, HMODULE *mod)
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &m) ||
        !m)
        return {};
    if (mod)
        *mod = m;
    char path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(m, path, MAX_PATH);
    return std::string(hud::guard::base_name(std::string_view(path, n)));
}

ProbeScope::ProbeScope()
{
    std::lock_guard<std::mutex> g(probe_lock);
    ++probe_scopes;
}

ProbeScope::~ProbeScope()
{
    std::lock_guard<std::mutex> g(probe_lock);
    if (--probe_scopes == 0)
        release(probe_held);
}

bool arm_swapchain()
{
    static int state;
    if (state)
        return state > 0;
    state = -1;
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    auto create_factory = dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    if (!create_factory) {
        swofty::log("[d3d12] dxgi.dll!CreateDXGIFactory1 is unavailable; the swapchain hooks are not armed");
        return false;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SwoftyD3D12Probe";
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                               wc.hInstance, nullptr);
    if (!wnd) {
        swofty::log("[d3d12] could not make a probe window to read the swapchain vtable");
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }
    ID3D12Device *dev = probe_device();
    ID3D12CommandQueue *q = nullptr;
    IDXGIFactory4 *factory = nullptr;
    IDXGISwapChain1 *swap1 = nullptr;
    IDXGISwapChain3 *swap = nullptr;
    bool ok = false;
    do {
        if (!dev)
            break;
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))))
            break;
        if (FAILED(create_factory(IID_PPV_ARGS(&factory))))
            break;
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.BufferCount = 2;
        sd.Width = 64;
        sd.Height = 64;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.SampleDesc.Count = 1;
        if (FAILED(factory->CreateSwapChainForHwnd(q, wnd, &sd, nullptr, nullptr, &swap1)))
            break;
        if (FAILED(swap1->QueryInterface(IID_PPV_ARGS(&swap))))
            break;
        void **sc_vt = *reinterpret_cast<void ***>(swap);
        void **f_vt = *reinterpret_cast<void ***>(factory);
        ok = hook_factory(f_vt) &&
             install(h_present, sc_vt[8], reinterpret_cast<void *>(&present_hook), "IDXGISwapChain::Present");
        if (!ok)
            break;
        install(h_resize, sc_vt[13], reinterpret_cast<void *>(&resize_hook), "IDXGISwapChain::ResizeBuffers");
        install(h_resize1, sc_vt[39], reinterpret_cast<void *>(&resize1_hook), "IDXGISwapChain3::ResizeBuffers1");
        install(h_color_space, sc_vt[38], reinterpret_cast<void *>(&color_space_hook),
                "IDXGISwapChain3::SetColorSpace1");
        swofty::log("[d3d12] swapchain hooks armed in %s: Present (vt+0x40), ResizeBuffers (vt+0x68), "
                    "SetColorSpace1 (vt+0x130), ResizeBuffers1 (vt+0x138); only swapchains created after this point "
                    "are known",
                    module_of(sc_vt[8]).c_str());
    } while (false);
    release(swap);
    release(swap1);
    release(factory);
    release(q);
    release(dev);
    DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    state = ok ? 1 : -1;
    return ok;
}

bool arm_device()
{
    static int state;
    if (state)
        return state > 0;
    state = -1;
    ID3D12Device *dev = probe_device();
    ID3D12Resource *buffer = nullptr;
    ID3D12CommandAllocator *alloc = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12CommandQueue *q = nullptr;
    bool ok = false;
    if (dev) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = 256;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, IID_PPV_ARGS(&buffer))) &&
            SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) &&
            SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list))) &&
            SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))))
            ok = install(h_setname, (*reinterpret_cast<void ***>(buffer))[SETNAME_SLOT],
                         reinterpret_cast<void *>(&setname_hook), "ID3D12Object::SetName") &&
                 install(h_barrier, (*reinterpret_cast<void ***>(list))[BARRIER_SLOT],
                         reinterpret_cast<void *>(&barrier_hook), "ID3D12GraphicsCommandList::ResourceBarrier") &&
                 install(h_exec, (*reinterpret_cast<void ***>(q))[EXEC_SLOT], reinterpret_cast<void *>(&exec_hook),
                         "ID3D12CommandQueue::ExecuteCommandLists");
        if (list)
            list->Close();
    }
    if (ok)
        swofty::log("[d3d12] device hooks armed: SetName (vt+0x30), ResourceBarrier (vt+0xd0), ExecuteCommandLists "
                    "(vt+0x50)");
    release(q);
    release(list);
    release(alloc);
    release(buffer);
    release(dev);
    state = ok ? 1 : -1;
    return ok;
}

}
