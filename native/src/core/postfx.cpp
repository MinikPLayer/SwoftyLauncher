#include "d3d12_hooks.hpp"
#include "hud_guard.hpp"
#include "perf.hpp"
#include "runtime.hpp"

#include <swofty/mixin.hpp>

#include <d3d12.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Post-process passes. The game tonemaps the 3D picture into eaid://ego_package/tonemap_output.image,
// grades and encodes it into the back buffer, and closes that command list; the HUD draws into the
// back buffer from the next list on. The core marks the list in which tonemap_output.image goes from
// written to read, and when the game submits it, splits the ExecuteCommandLists call after it and
// submits a list of its own on the same queue: the back buffer is copied out, each addon pass (a
// DXIL compute shader, swofty/postfx.hlsli) runs between two textures of the core's, and the result
// is copied back before the HUD lists run. The core's list never touches the game's lists or their
// state. A pass whose pipeline cannot be made, or whose GPU time stays above its budget, is turned
// off and logged; SWOFTY_POSTFX=0 or a swofty/postfx.off file turns every pass off.
//
// The game does not always close the list there: on some frames, more in garages, it records the
// tonemap, the HUD and the present transition in one list, and a split would leave those frames
// without the passes. So the core snapshots the back buffer before the HUD every frame and runs the
// passes at Present on the present queue: they grade a copy of the snapshot, and a merge adds what
// they changed to the back buffer wherever it still matches the snapshot, which leaves the HUD as
// the game drew it. The snapshot is a copy recorded into the game's list where it moves
// eaid://ego_package/hudless_backbuffer.image out of UAV, its HUD-less step between the picture and
// the HUD, or a copy list between the game's lists where a list ends with the back buffer still a
// render target; copies and barriers only, so the game's pipeline state stays as it was. Under
// Streamline the game draws into Streamline's buffers, which it copies to the swapchain's at
// Present, so the merge writes the buffer being presented. With frame generation on, passes go
// back to the split, where the game's HUD-less copy holds the picture.

namespace {

using SerializeRootFn = HRESULT(WINAPI *)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **,
                                          ID3DBlob **);

constexpr wchar_t TONEMAP_NAME[] = L"eaid://ego_package/tonemap_output.image";
constexpr wchar_t BACKBUFFER_NAME[] = L"BackBuffer";
constexpr wchar_t HUDLESS_NAME[] = L"eaid://ego_package/hudless_backbuffer.image";
constexpr wchar_t DEPTH_NAME[] = L"eaid://ego_package/gbuffer_depth_stencil.image";
constexpr wchar_t UI_PREFIX[] = L"eaid://ui_package/";
constexpr UINT FRAMES = 3, MAX_PASSES = 8, CONSTANTS = 28, BUDGET_FRAMES = 120, TABLE = 3, SNAPS = 4;
constexpr D3D12_RESOURCE_STATES READ_STATES =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

struct Pass {
    std::string addon, name, shader;
    std::vector<uint8_t> dxil;
    float options[16] = {};
    double budget_ms = 1.5;
    ID3D12PipelineState *pso = nullptr;
    bool off = false;
    double ms_sum = 0, ms_peak = 0;
    unsigned ms_frames = 0, over = 0;
};

struct Mark {
    bool tonemap = false, ui = false;
    int snap = -1;
    ID3D12Resource *target = nullptr, *hudless = nullptr, *depth = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON, hudless_state = D3D12_RESOURCE_STATE_COMMON,
                          depth_state = D3D12_RESOURCE_STATE_COMMON;
    std::vector<D3D12_RESOURCE_BARRIER> seen;
};

struct Slot {
    ID3D12CommandAllocator *alloc = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    UINT64 fence_value = 0;
    UINT passes = 0;
    int pass_of[MAX_PASSES] = {};
    bool capture = false;
};

struct Target {
    D3D12_RESOURCE_DESC desc{};
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    ID3D12Resource *tex[2] = {};
    D3D12_RESOURCE_STATES state[2] = {};
    ID3D12DescriptorHeap *heap = nullptr;
    UINT stride = 0;
    ID3D12Resource *hudless = nullptr, *back = nullptr;
    D3D12_RESOURCE_STATES hudless_state = D3D12_RESOURCE_STATE_COMMON, back_state = D3D12_RESOURCE_STATE_COMMON;
    ID3D12DescriptorHeap *merge_heap = nullptr;
};

// The resources the barrier hook watches, read without the lock. Writers hold the lock; readers only
// load. A key keeps its slot and erase only clears its bits, so lookups never walk over tombstones.
// The game names short-lived UI resources all the time, so once half the slots hold keys the writer
// copies the live keys into a new table and publishes it, which keeps probe runs short. A replaced
// table is freed a few seconds later, long after any reader that loaded it has returned.
class PtrSet {
    size_t mask;
    std::unique_ptr<std::atomic<uintptr_t>[]> key;
    std::unique_ptr<std::atomic<uint8_t>[]> on;
    size_t used = 0, live = 0;

    size_t hash(uintptr_t k) const { return size_t((k * 0x9E3779B97F4A7C15ull) >> 40) & mask; }

    size_t find(uintptr_t k, bool claim)
    {
        for (size_t i = hash(k), n = 0; n <= mask; i = (i + 1) & mask, n++) {
            uintptr_t v = key[i].load(std::memory_order_acquire);
            if (v == k)
                return i;
            if (v == 0) {
                if (!claim)
                    return mask + 1;
                key[i].store(k, std::memory_order_release);
                used++;
                return i;
            }
        }
        return mask + 1;
    }

public:
    explicit PtrSet(size_t n) : mask(n - 1), key(new std::atomic<uintptr_t>[n]()), on(new std::atomic<uint8_t>[n]()) {}

    uint8_t get(const void *p) const
    {
        uintptr_t k = uintptr_t(p);
        for (size_t i = hash(k), n = 0; n <= mask; i = (i + 1) & mask, n++) {
            uintptr_t v = key[i].load(std::memory_order_acquire);
            if (v == k)
                return on[i].load(std::memory_order_acquire);
            if (v == 0)
                return 0;
        }
        return 0;
    }
    size_t size() const { return mask + 1; }
    size_t count() const { return live; }
    bool half_full() const { return used * 2 >= mask + 1; }
    void set(const void *p, uint8_t bits)
    {
        size_t i = find(uintptr_t(p), bits != 0);
        if (i > mask)
            return;
        uint8_t was = on[i].load(std::memory_order_relaxed);
        live += (!was && bits) - (was && !bits);
        on[i].store(bits, std::memory_order_release);
    }
    template <class F> void each(F f) const
    {
        for (size_t i = 0; i <= mask; i++) {
            uintptr_t k = key[i].load(std::memory_order_relaxed);
            uint8_t b = on[i].load(std::memory_order_relaxed);
            if (k && b)
                f(reinterpret_cast<const void *>(k), b);
        }
    }
};

enum : uint8_t { W_TONEMAP = 1, W_BACK = 2, W_HUDLESS = 4, W_UI = 8, W_DEPTH = 16 };

// The few resources the passes key on and the game's many UI images live in separate tables, so UI
// churn can never push the tonemap or the back buffers out. Freed resources leave dead slots behind,
// so once half a table's slots are taken it is rebuilt from its live keys, at least four times as
// large as they are; a lookup for an unwatched resource, nearly every barrier, stays a probe or two.
template <size_t MIN> class Table {
    std::atomic<PtrSet *> cur{new PtrSet(MIN)};
    std::vector<std::pair<PtrSet *, ULONGLONG>> retired;
    const char *what;
    size_t told = MIN;

public:
    explicit Table(const char *name) : what(name) {}

    uint8_t get(const void *p) const { return cur.load(std::memory_order_acquire)->get(p); }

    void put(const void *p, uint8_t bits)
    {
        PtrSet *w = cur.load(std::memory_order_relaxed);
        if (!bits || !w->half_full()) {
            w->set(p, bits);
            return;
        }
        size_t n = MIN;
        while (n < (w->count() + 1) * 4)
            n *= 2;
        auto *fresh = new PtrSet(n);
        w->each([&](const void *k, uint8_t b) {
            if (k != p)
                fresh->set(k, b);
        });
        fresh->set(p, bits);
        if (n > told) {
            told = n;
            swofty::log("[postfx] the %s table grew to %zu slots for %zu live resource(s)", what, n, fresh->count());
        }
        cur.store(fresh, std::memory_order_release);
        ULONGLONG now = GetTickCount64();
        std::erase_if(retired, [&](const std::pair<PtrSet *, ULONGLONG> &r) {
            if (now - r.second < 5000)
                return false;
            delete r.first;
            return true;
        });
        retired.emplace_back(w, now);
    }
};

Table<256> key_watched("key resource");
Table<4096> ui_watched("UI");

uint8_t watched_bits(const void *p)
{
    return key_watched.get(p) | ui_watched.get(p);
}

void watch(const void *p, uint8_t bits)
{
    key_watched.put(p, uint8_t(bits & ~W_UI));
    ui_watched.put(p, uint8_t(bits & W_UI));
}
std::recursive_mutex lock;
std::vector<Pass> passes;
ID3D12Resource *depth_res;
D3D12_RESOURCE_STATES depth_state;
bool depth_logged;
std::unordered_map<ID3D12Resource *, std::wstring> names;
std::unordered_map<ID3D12GraphicsCommandList *, Mark> marks;
bool diag_done, killed, broken, warned_busy, hudless_logged, saw_back;
std::wstring off_file;

ID3D12Device *device;
ID3D12RootSignature *root;
ID3D12Fence *fence;
UINT64 fence_next;
Slot slots[FRAMES];
UINT slot_at;
ID3D12QueryHeap *queries;
ID3D12Resource *query_readback;
double ticks_per_ms;
Target target;
std::vector<Target> retired;
ID3D12RootSignature *merge_root;
ID3D12PipelineState *merge_pso;
struct Snap {
    ID3D12Resource *tex = nullptr;
    ID3D12CommandAllocator *alloc = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    UINT64 read_fence = 0, copy_fence = 0;
};
Snap snaps[SNAPS];
D3D12_RESOURCE_DESC snap_desc;
ID3D12Fence *copy_fence;
UINT64 copy_next;
UINT snap_next;
int frame_snap = -1;
bool at_present, merge_told;
unsigned snapped_in_list, snapped_between, snap_missed, presented, snap_lines;
ULONGLONG snap_log_at;
const char *present_why;
std::wstring hardware_settings;
ULONGLONG started, last_check, stats_at;
unsigned frame_count, ran, ran_total, skipped_ui, skipped_state, stats_logged;

constexpr unsigned NAMED_LINES = 80, DUMPS = 6, DUMP_LINES = 160, DIAG_MINUTES = 6;
constexpr const wchar_t *INTERESTING[] = {L"tonemap", L"output", L"final", L"hudless", L"backbuffer", L"pathtrac",
                                          L"_pt", L"pt_", L"denois", L"upscal", L"composite", L"present",
                                          L"display", L"post", L"lut", L"bloom", L"exposure"};
std::vector<std::wstring> named_seen;
unsigned dumps_left = DUMPS, diag_minutes, exec_count, tonemap_lists, back_lists;
ULONGLONG dump_at, diag_at;
bool dump_armed, tonemap_named;
struct Seen {
    ID3D12Resource *res;
    D3D12_RESOURCE_STATES before, after;
    D3D12_RESOURCE_DESC desc;
};
std::vector<Seen> dump_seen;

std::string capture_dir;
ID3D12Resource *capture_rb[3];
D3D12_PLACED_SUBRESOURCE_FOOTPRINT capture_fp;
ULONGLONG capture_next, capture_checked;
unsigned captures;
std::wstring capture_file;
bool capture_asked, capture_ref;
UINT64 capture_fence;
bool capture_pending;

void release(IUnknown *p)
{
    if (p)
        p->Release();
}

std::string name_of(ID3D12Resource *r)
{
    auto it = names.find(r);
    return it == names.end() ? std::string("?") : runtime::narrow(it->second);
}

bool killed_now()
{
    ULONGLONG now = GetTickCount64();
    if (now - last_check < 1000)
        return killed;
    last_check = now;
    bool was = killed;
    killed = GetFileAttributesW(off_file.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (killed != was)
        swofty::log(killed ? "[postfx] swofty/postfx.off exists; every pass is off" : "[postfx] swofty/postfx.off is gone; passes run again");
    return killed;
}

DXGI_FORMAT view_format(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return DXGI_FORMAT_R11G11B10_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

UINT format_kind(DXGI_FORMAT view)
{
    return view == DXGI_FORMAT_R10G10B10A2_UNORM ? 1 : view == DXGI_FORMAT_R16G16B16A16_FLOAT ? 2 : 0;
}

ID3D12Resource *buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = size;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *r = nullptr;
    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&r)))
               ? r
               : nullptr;
}

bool make_root()
{
    auto serialize =
        reinterpret_cast<SerializeRootFn>(GetProcAddress(GetModuleHandleA("d3d12.dll"), "D3D12SerializeRootSignature"));
    if (!serialize)
        return false;
    D3D12_DESCRIPTOR_RANGE ranges[3]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors = 1;
    ranges[2].BaseShaderRegister = 1;
    ranges[2].OffsetInDescriptorsFromTableStart = 2;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = CONSTANTS;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 3;
    params[1].DescriptorTable.pDescriptorRanges = ranges;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (UINT i = 0; i < 2; i++) {
        samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = 2;
    rd.pParameters = params;
    rd.NumStaticSamplers = 2;
    rd.pStaticSamplers = samplers;
    ID3DBlob *blob = nullptr, *err = nullptr;
    HRESULT hr = serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (SUCCEEDED(hr))
        hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root));
    release(blob);
    release(err);
    return SUCCEEDED(hr);
}

bool make_merge()
{
    static const char hlsl[] =
        "Texture2D<float4> G:register(t0);Texture2D<float4> H:register(t1);Texture2D<float4> B:register(t2);"
        "RWTexture2D<float4> O:register(u0);cbuffer C:register(b0){uint2 size;}"
        "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(any(id.xy>=size))return;"
        "float4 b=B[id.xy],h=H[id.xy],g=G[id.xy];float3 d=abs(b.rgb-h.rgb);"
        "float w=1-smoothstep(1.0/255,24.0/255,max(d.r,max(d.g,d.b)));"
        "O[id.xy]=float4(saturate(b.rgb+(g.rgb-h.rgb)*w),b.a);}";
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    auto compile = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    auto serialize =
        reinterpret_cast<SerializeRootFn>(GetProcAddress(GetModuleHandleA("d3d12.dll"), "D3D12SerializeRootSignature"));
    if (!compile || !serialize)
        return false;
    ID3DBlob *cs = nullptr, *err = nullptr;
    HRESULT hr = compile(hlsl, sizeof hlsl - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs, &err);
    if (err) {
        if (FAILED(hr))
            swofty::log("[postfx] the merge shader did not compile: %.*s", int(err->GetBufferSize()),
                        static_cast<const char *>(err->GetBufferPointer()));
        release(err);
        err = nullptr;
    }
    if (FAILED(hr) || !cs)
        return false;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 3;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 3;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 2;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2;
    params[1].DescriptorTable.pDescriptorRanges = ranges;
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = 2;
    rd.pParameters = params;
    ID3DBlob *blob = nullptr;
    hr = serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (SUCCEEDED(hr))
        hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&merge_root));
    release(blob);
    release(err);
    if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = merge_root;
        pd.CS.pShaderBytecode = cs->GetBufferPointer();
        pd.CS.BytecodeLength = cs->GetBufferSize();
        hr = device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&merge_pso));
    }
    release(cs);
    return SUCCEEDED(hr);
}

// The device comes from the game's resource, never from the queue: a queue can be Streamline's proxy,
// whose device makes proxy command lists that the hooked ExecuteCommandLists cannot take.
bool init(ID3D12CommandQueue *q, ID3D12Resource *res)
{
    if (FAILED(res->GetDevice(IID_PPV_ARGS(&device))))
        return false;
    if (!make_root()) {
        swofty::log("[postfx] could not make the passes' root signature; post-processing is off");
        return false;
    }
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
        return false;
    for (auto &s : slots) {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.alloc, nullptr, IID_PPV_ARGS(&s.list))))
            return false;
        s.list->Close();
    }
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = FRAMES * MAX_PASSES * 2;
    UINT64 freq = 0;
    if (SUCCEEDED(device->CreateQueryHeap(&qd, IID_PPV_ARGS(&queries))) && SUCCEEDED(q->GetTimestampFrequency(&freq)) &&
        freq)
        ticks_per_ms = double(freq) / 1000.0;
    query_readback = buffer(D3D12_HEAP_TYPE_READBACK, qd.Count * 8, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!query_readback)
        ticks_per_ms = 0;
    for (auto &p : passes) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = root;
        pd.CS.pShaderBytecode = p.dxil.data();
        pd.CS.BytecodeLength = p.dxil.size();
        HRESULT hr = device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&p.pso));
        if (FAILED(hr)) {
            p.off = true;
            swofty::log("[postfx] %s's pass %s is off: its pipeline could not be made (0x%08lx)", p.addon.c_str(),
                        p.name.c_str(), hr);
        } else
            swofty::log("[postfx] %s's pass %s is ready (%s, %zu bytes of DXIL)", p.addon.c_str(), p.name.c_str(),
                        p.shader.c_str(), p.dxil.size());
    }
    if (!make_merge())
        swofty::log("[postfx] the merge for passes at Present could not be made; passes stay before the HUD");
    started = GetTickCount64();
    return true;
}

bool make_target(const D3D12_RESOURCE_DESC &d)
{
    DXGI_FORMAT view = view_format(d.Format);
    if (view == DXGI_FORMAT_UNKNOWN) {
        swofty::log("[postfx] the game's picture has format %d, which the passes do not take; post-processing is off",
                    int(d.Format));
        return false;
    }
    if (target.heap)
        retired.push_back(target);
    target = Target{};
    target.desc = d;
    target.view = view;
    D3D12_RESOURCE_DESC td = d;
    td.Format = view;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    td.MipLevels = 1;
    td.DepthOrArraySize = 1;
    td.SampleDesc = {1, 0};
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    for (int i = 0; i < 2; i++) {
        target.state[i] = D3D12_RESOURCE_STATE_COMMON;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, target.state[i], nullptr,
                                                   IID_PPV_ARGS(&target.tex[i]))))
            return false;
    }
    if (merge_pso) {
        td.Flags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_DESCRIPTOR_HEAP_DESC md{};
        md.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        md.NumDescriptors = 2 * 4;
        md.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, target.hudless_state, nullptr,
                                                   IID_PPV_ARGS(&target.hudless))) ||
            FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, target.back_state, nullptr,
                                                   IID_PPV_ARGS(&target.back))) ||
            FAILED(device->CreateDescriptorHeap(&md, IID_PPV_ARGS(&target.merge_heap))))
            return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = FRAMES * 2 * TABLE;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&target.heap))))
        return false;
    target.stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = target.heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT slot = 0; slot < FRAMES; slot++)
        for (UINT i = 0; i < 2; i++) {
            D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
            sv.Format = view;
            sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sv.Texture2D.MipLevels = 1;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
            uv.Format = view;
            uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            SIZE_T at = h.ptr + SIZE_T((slot * 2 + i) * TABLE) * target.stride;
            device->CreateShaderResourceView(target.tex[i], &sv, {at});
            device->CreateUnorderedAccessView(target.tex[1 - i], nullptr, &uv, {at + target.stride});
            if (target.merge_heap && slot == 0) {
                SIZE_T m = target.merge_heap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(i * 4) * target.stride;
                device->CreateShaderResourceView(target.tex[i], &sv, {m});
                device->CreateShaderResourceView(target.hudless, &sv, {m + target.stride});
                device->CreateShaderResourceView(target.back, &sv, {m + 2 * target.stride});
                device->CreateUnorderedAccessView(target.tex[1 - i], nullptr, &uv, {m + 3 * target.stride});
            }
        }
    if (!capture_dir.empty() && !capture_pending) {
        for (auto &r : capture_rb)
            release(r), r = nullptr;
        UINT rows = 0;
        UINT64 row_bytes = 0, total = 0;
        device->GetCopyableFootprints(&d, 0, 1, 0, &capture_fp, &rows, &row_bytes, &total);
        for (auto &r : capture_rb)
            r = buffer(D3D12_HEAP_TYPE_READBACK, total, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    swofty::log("[postfx] passes run on the game's %llux%u picture (format %d)", (unsigned long long)d.Width, d.Height,
                int(d.Format));
    return true;
}

void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *r, D3D12_RESOURCE_STATES from,
                D3D12_RESOURCE_STATES to)
{
    if (from == to)
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    list->ResourceBarrier(1, &b);
}

void move(ID3D12GraphicsCommandList *list, int i, D3D12_RESOURCE_STATES to)
{
    transition(list, target.tex[i], target.state[i], to);
    target.state[i] = to;
}

void write_ppm(const std::string &path, const uint8_t *src, UINT pitch, UINT w, UINT h, DXGI_FORMAT view)
{
    std::string out = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    size_t at = out.size();
    out.resize(at + size_t(w) * h * 3);
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++) {
            uint32_t v;
            memcpy(&v, src + size_t(y) * pitch + size_t(x) * 4, 4);
            uint8_t *o = reinterpret_cast<uint8_t *>(&out[at + (size_t(y) * w + x) * 3]);
            if (view == DXGI_FORMAT_R10G10B10A2_UNORM) {
                o[0] = uint8_t((v & 1023) >> 2), o[1] = uint8_t(((v >> 10) & 1023) >> 2), o[2] = uint8_t(((v >> 20) & 1023) >> 2);
            } else {
                o[0] = uint8_t(v), o[1] = uint8_t(v >> 8), o[2] = uint8_t(v >> 16);
            }
        }
    runtime::write_file(runtime::wide(path), out);
}

void land_capture()
{
    if (!capture_pending || fence->GetCompletedValue() < capture_fence)
        return;
    capture_pending = false;
    if (target.view != DXGI_FORMAT_R8G8B8A8_UNORM && target.view != DXGI_FORMAT_R10G10B10A2_UNORM)
        return;
    const char *which[3] = {"before", "after", "hudless"};
    for (int i = 0; i < (capture_ref ? 3 : 2); i++) {
        uint8_t *mapped = nullptr;
        if (!capture_rb[i] || FAILED(capture_rb[i]->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
            continue;
        char file[64];
        snprintf(file, sizeof file, "\\postfx-%02u-%s.ppm", captures, which[i]);
        write_ppm(capture_dir + file, mapped + capture_fp.Offset, capture_fp.Footprint.RowPitch, UINT(target.desc.Width),
                  target.desc.Height, target.view);
        D3D12_RANGE none{0, 0};
        capture_rb[i]->Unmap(0, &none);
    }
    swofty::log("[postfx] wrote the picture before and after the passes to %s\\postfx-%02u-{before,after%s}.ppm",
                capture_dir.c_str(), captures, capture_ref ? ",hudless" : "");
    captures++;
}

void read_times(Slot &s)
{
    if (!ticks_per_ms || !s.passes)
        return;
    UINT base = UINT(&s - slots) * MAX_PASSES * 2;
    D3D12_RANGE r{SIZE_T(base) * 8, SIZE_T(base + s.passes * 2) * 8};
    uint64_t *t = nullptr;
    if (FAILED(query_readback->Map(0, &r, reinterpret_cast<void **>(&t))))
        return;
    for (UINT i = 0; i < s.passes; i++) {
        Pass &p = passes[size_t(s.pass_of[i])];
        uint64_t a = t[base + i * 2], b = t[base + i * 2 + 1];
        if (b < a)
            continue;
        double ms = double(b - a) / ticks_per_ms;
        p.ms_sum += ms;
        p.ms_peak = std::max(p.ms_peak, ms);
        if (++p.ms_frames < BUDGET_FRAMES)
            continue;
        double avg = p.ms_sum / p.ms_frames;
        if (avg > p.budget_ms) {
            if (++p.over >= 3 && !p.off) {
                p.off = true;
                swofty::log("[postfx] %s's pass %s is off: it took %.3f ms of GPU time a frame, over its %.2f ms budget",
                            p.addon.c_str(), p.name.c_str(), avg, p.budget_ms);
            }
        } else
            p.over = 0;
        if (ran_total <= 4 * BUDGET_FRAMES)
            swofty::log("[postfx] %s's pass %s: %.3f ms of GPU time a frame on average, %.3f ms at most", p.addon.c_str(),
                        p.name.c_str(), avg, p.ms_peak);
        p.ms_sum = 0;
        p.ms_peak = 0;
        p.ms_frames = 0;
    }
    D3D12_RANGE none{0, 0};
    query_readback->Unmap(0, &none);
}

DXGI_FORMAT depth_view(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

// Writes this slot's depth descriptors: the game's depth buffer when it can be read, else a null view
// that reads 0. Returns whether the depth buffer is bound, with the state to move it through.
bool bind_depth(UINT slot, D3D12_RESOURCE_STATES &read_from, uint32_t size[2], bool allowed)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    ID3D12Resource *r = nullptr;
    if (allowed && depth_res &&
        (depth_state & (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_WRITE))) {
        D3D12_RESOURCE_DESC dd = depth_res->GetDesc();
        DXGI_FORMAT f = depth_view(dd.Format);
        if (f != DXGI_FORMAT_UNKNOWN && dd.SampleDesc.Count == 1) {
            r = depth_res;
            sv.Format = f;
            size[0] = UINT(dd.Width);
            size[1] = dd.Height;
            read_from = depth_state;
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE h = target.heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; i++)
        device->CreateShaderResourceView(r, &sv, {h.ptr + SIZE_T((slot * 2 + i) * TABLE + 2) * target.stride});
    if (r && !depth_logged) {
        depth_logged = true;
        swofty::log("[postfx] passes read the game's depth buffer (%ux%u, format %d) as t1", size[0], size[1],
                    int(depth_res->GetDesc().Format));
    }
    return r != nullptr;
}

// Snapshot textures shaped like the back buffer, kept in COPY_DEST outside the core's own lists.
bool make_snaps(ID3D12Device *dev, const D3D12_RESOURCE_DESC &back)
{
    if (snaps[0].tex && back.Width == snap_desc.Width && back.Height == snap_desc.Height &&
        back.Format == snap_desc.Format)
        return true;
    D3D12_RESOURCE_DESC d = back;
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    d.MipLevels = 1;
    d.DepthOrArraySize = 1;
    d.SampleDesc = {1, 0};
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (!copy_fence && FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_fence))))
        return false;
    for (auto &sn : snaps) {
        ID3D12Resource *tex = nullptr;
        if (FAILED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&tex))))
            return false;
        if (sn.tex)
            retired.push_back(Target{.tex = {sn.tex, nullptr}});
        sn.tex = tex;
        if (!sn.alloc &&
            (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&sn.alloc))) ||
             FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, sn.alloc, nullptr, IID_PPV_ARGS(&sn.list)))))
            return false;
        if (sn.list)
            sn.list->Close();
    }
    snap_desc = back;
    swofty::log("[postfx] the back buffer is snapshotted before the HUD (%llux%u, format %d)",
                (unsigned long long)back.Width, back.Height, int(back.Format));
    return true;
}

void copy_back(ID3D12GraphicsCommandList *l, ID3D12Resource *back, D3D12_RESOURCE_STATES state, ID3D12Resource *to,
               bool hooked)
{
    D3D12_RESOURCE_BARRIER b[2]{};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource = back;
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[0].Transition.StateBefore = state;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (hooked)
        d3d12hooks::barrier_original(l, 1u, b);
    else
        l->ResourceBarrier(1, b);
    l->CopyResource(to, back);
    std::swap(b[0].Transition.StateBefore, b[0].Transition.StateAfter);
    if (hooked)
        d3d12hooks::barrier_original(l, 1u, b);
    else
        l->ResourceBarrier(1, b);
}

// Records and submits the core's list on the game's queue. Returns false when nothing ran. With a
// back buffer, res is the game's HUD-less picture: the passes grade a copy of it and the merge carries
// the change into the back buffer.
bool run(ID3D12CommandQueue *q, ID3D12Resource *res, D3D12_RESOURCE_STATES state, ID3D12Resource *hudless,
         D3D12_RESOURCE_STATES hudless_state, ID3D12Resource *back = nullptr)
{
    if (!device && !init(q, res)) {
        broken = true;
        return false;
    }
    if (device->GetDeviceRemovedReason() != S_OK) {
        broken = true;
        swofty::log("[postfx] the GPU device was removed; post-processing is off");
        return false;
    }
    land_capture();
    Slot &s = slots[slot_at];
    if (fence->GetCompletedValue() < s.fence_value) {
        if (!warned_busy)
            swofty::log("[postfx] the GPU is %u frames behind; passes skip frames until it catches up", FRAMES);
        warned_busy = true;
        return false;
    }
    read_times(s);
    D3D12_RESOURCE_DESC d = res->GetDesc();
    if (!target.heap || d.Width != target.desc.Width || d.Height != target.desc.Height || d.Format != target.desc.Format)
        if (!make_target(d)) {
            broken = true;
            return false;
        }
    std::vector<int> live;
    for (size_t i = 0; i < passes.size() && live.size() < MAX_PASSES; i++)
        if (!passes[i].off)
            live.push_back(int(i));
    if (live.empty())
        return false;
    if (FAILED(s.alloc->Reset()) || FAILED(s.list->Reset(s.alloc, nullptr))) {
        broken = true;
        swofty::log("[postfx] could not record the passes' command list; post-processing is off");
        return false;
    }
    ID3D12GraphicsCommandList *l = s.list;
    ULONGLONG now = GetTickCount64();
    if (!capture_dir.empty() && now - capture_checked >= 1000) {
        capture_checked = now;
        if (GetFileAttributesW(capture_file.c_str()) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(capture_file.c_str());
            capture_asked = true;
        }
    }
    s.capture = !capture_dir.empty() && !capture_pending && capture_rb[0] && capture_rb[1] &&
                (capture_asked || (captures < 6 && now >= capture_next));
    if (s.capture) {
        capture_ref = back && capture_rb[2];
        capture_asked = false;
    }
    D3D12_TEXTURE_COPY_LOCATION rb{}, from{};
    rb.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    rb.PlacedFootprint = capture_fp;
    from.pResource = back ? back : res;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    transition(l, res, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (back) {
        transition(l, back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(l, target.hudless, target.hudless_state, D3D12_RESOURCE_STATE_COPY_DEST);
        transition(l, target.back, target.back_state, D3D12_RESOURCE_STATE_COPY_DEST);
        target.hudless_state = target.back_state = D3D12_RESOURCE_STATE_COPY_DEST;
        l->CopyResource(target.hudless, res);
        l->CopyResource(target.back, back);
    }
    if (s.capture) {
        rb.pResource = capture_rb[0];
        l->CopyTextureRegion(&rb, 0, 0, 0, &from, nullptr);
    }
    if (capture_ref) {
        D3D12_TEXTURE_COPY_LOCATION ref = from;
        ref.pResource = res;
        rb.pResource = capture_rb[2];
        l->CopyTextureRegion(&rb, 0, 0, 0, &ref, nullptr);
    }
    move(l, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    l->CopyResource(target.tex[0], res);
    l->SetComputeRootSignature(root);
    l->SetDescriptorHeaps(1, &target.heap);
    uint32_t c[CONSTANTS] = {};
    c[0] = UINT(d.Width);
    c[1] = d.Height;
    float texel[2] = {1.0f / float(d.Width), 1.0f / float(d.Height)};
    memcpy(&c[2], texel, 8);
    float t = float(double(now - started) / 1000.0);
    memcpy(&c[4], &t, 4);
    c[5] = frame_count;
    c[6] = format_kind(target.view);
    D3D12_RESOURCE_STATES depth_from{};
    bool depth_bound = bind_depth(slot_at, depth_from, &c[24], !back);
    c[26] = depth_bound;
    D3D12_RESOURCE_STATES depth_read = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bool depth_moved = depth_bound && !(depth_from & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (depth_moved)
        transition(l, depth_res, depth_from, depth_read);
    UINT base = slot_at * MAX_PASSES * 2;
    int src = 0;
    s.passes = 0;
    for (int index : live) {
        Pass &p = passes[size_t(index)];
        move(l, src, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        move(l, 1 - src, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        c[7] = s.passes;
        memcpy(&c[8], p.options, sizeof p.options);
        if (ticks_per_ms)
            l->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, base + s.passes * 2);
        l->SetPipelineState(p.pso);
        l->SetComputeRoot32BitConstants(0, CONSTANTS, c, 0);
        D3D12_GPU_DESCRIPTOR_HANDLE table = target.heap->GetGPUDescriptorHandleForHeapStart();
        table.ptr += UINT64((slot_at * 2 + UINT(src)) * TABLE) * target.stride;
        l->SetComputeRootDescriptorTable(1, table);
        l->Dispatch((UINT(d.Width) + 7) / 8, (d.Height + 7) / 8, 1);
        if (ticks_per_ms)
            l->EndQuery(queries, D3D12_QUERY_TYPE_TIMESTAMP, base + s.passes * 2 + 1);
        s.pass_of[s.passes++] = index;
        src = 1 - src;
    }
    if (depth_moved)
        transition(l, depth_res, depth_read, depth_from);
    if (ticks_per_ms)
        l->ResolveQueryData(queries, D3D12_QUERY_TYPE_TIMESTAMP, base, s.passes * 2, query_readback, UINT64(base) * 8);
    if (back) {
        move(l, src, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        move(l, 1 - src, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(l, target.hudless, target.hudless_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(l, target.back, target.back_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        target.hudless_state = target.back_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        uint32_t size[2] = {UINT(d.Width), d.Height};
        D3D12_GPU_DESCRIPTOR_HANDLE table = target.merge_heap->GetGPUDescriptorHandleForHeapStart();
        table.ptr += UINT64(src * 4) * target.stride;
        l->SetComputeRootSignature(merge_root);
        l->SetDescriptorHeaps(1, &target.merge_heap);
        l->SetPipelineState(merge_pso);
        l->SetComputeRoot32BitConstants(0, 2, size, 0);
        l->SetComputeRootDescriptorTable(1, table);
        l->Dispatch((UINT(d.Width) + 7) / 8, (d.Height + 7) / 8, 1);
        src = 1 - src;
        move(l, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(l, back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        l->CopyResource(back, target.tex[src]);
        if (s.capture) {
            transition(l, back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            rb.pResource = capture_rb[1];
            l->CopyTextureRegion(&rb, 0, 0, 0, &from, nullptr);
            transition(l, back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        } else
            transition(l, back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        transition(l, res, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
        hudless = nullptr;
    } else {
        move(l, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(l, res, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        l->CopyResource(res, target.tex[src]);
        if (s.capture) {
            transition(l, res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            rb.pResource = capture_rb[1];
            l->CopyTextureRegion(&rb, 0, 0, 0, &from, nullptr);
            transition(l, res, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
        } else
            transition(l, res, D3D12_RESOURCE_STATE_COPY_DEST, state);
    }
    if (hudless) {
        D3D12_RESOURCE_DESC hd = hudless->GetDesc();
        if (hd.Width == d.Width && hd.Height == d.Height && view_format(hd.Format) == target.view) {
            transition(l, hudless, hudless_state, D3D12_RESOURCE_STATE_COPY_DEST);
            l->CopyResource(hudless, target.tex[src]);
            transition(l, hudless, D3D12_RESOURCE_STATE_COPY_DEST, hudless_state);
            if (!hudless_logged)
                swofty::log("[postfx] the game's HUD-less copy of the picture gets the passes too, for Frame Generation");
            hudless_logged = true;
        }
    }
    if (FAILED(l->Close())) {
        broken = true;
        swofty::log("[postfx] the passes' command list did not close; post-processing is off");
        return false;
    }
    ID3D12CommandList *lists[] = {l};
    d3d12hooks::exec_original(q, 1u, lists);
    s.fence_value = ++fence_next;
    q->Signal(fence, s.fence_value);
    if (s.capture) {
        capture_pending = true;
        capture_fence = s.fence_value;
        capture_next = now + 10000;
    }
    slot_at = (slot_at + 1) % FRAMES;
    ran++;
    if (++ran_total == 1) {
        capture_next = now + 30000;
        swofty::log("[postfx] the picture now goes through %zu pass(es) before the HUD, on the game's queue %p",
                    live.size(), static_cast<void *>(q));
    }
    if (back && !merge_told) {
        merge_told = true;
        swofty::log("[postfx] passes now run at Present on a snapshot of the picture before the HUD (queue %p), so "
                    "every frame gets them",
                    static_cast<void *>(q));
    }
    return true;
}

const char *state_name(D3D12_RESOURCE_STATES s)
{
    static char buf[16];
    snprintf(buf, sizeof buf, "0x%x", unsigned(s));
    return buf;
}

void log_diag(ID3D12GraphicsCommandList *list, const Mark &m)
{
    swofty::log("[postfx] the first tonemap list %p has %zu transitions; back buffer %p ends in state 0x%x; %s",
                static_cast<void *>(list), m.seen.size(), static_cast<void *>(m.target), unsigned(m.state),
                m.ui ? "it draws UI too" : "the UI comes in a later list");
    for (auto &b : m.seen) {
        D3D12_RESOURCE_DESC d = b.Transition.pResource->GetDesc();
        swofty::log("[postfx]   %s %llux%u fmt %d: %s -> 0x%x", name_of(b.Transition.pResource).c_str(),
                    (unsigned long long)d.Width, d.Height, int(d.Format), state_name(b.Transition.StateBefore),
                    unsigned(b.Transition.StateAfter));
    }
}

bool interesting(LPCWSTR name)
{
    if (!wcsstr(name, L"eaid://") || wcsncmp(name, UI_PREFIX, wcslen(UI_PREFIX)) == 0)
        return false;
    std::wstring lower(name);
    for (auto &c : lower)
        c = wchar_t(towlower(c));
    for (auto *k : INTERESTING)
        if (lower.find(k) != std::wstring::npos)
            return true;
    return false;
}

void log_named(ID3D12Object *o, LPCWSTR name)
{
    if (named_seen.size() >= NAMED_LINES ||
        std::find(named_seen.begin(), named_seen.end(), name) != named_seen.end())
        return;
    ID3D12Resource *res = nullptr;
    if (FAILED(o->QueryInterface(__uuidof(ID3D12Resource), reinterpret_cast<void **>(&res))) || !res)
        return;
    D3D12_RESOURCE_DESC d = res->GetDesc();
    res->Release();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return;
    named_seen.emplace_back(name);
    swofty::log("[postfx] diag: named texture %s %llux%u fmt %d flags 0x%x", runtime::narrow(name).c_str(),
                (unsigned long long)d.Width, d.Height, int(d.Format), unsigned(d.Flags));
}

void log_dump(ID3D12GraphicsCommandList *list, const Mark &m)
{
    swofty::log("[postfx] diag %u of %u: list %p writes the back buffer %p (ends in 0x%x)%s%s; the last %zu transition(s) "
                "before it:",
                DUMPS - dumps_left + 1, DUMPS, static_cast<void *>(list), static_cast<void *>(m.target),
                unsigned(m.state), m.tonemap ? ", marked tonemap" : "", m.ui ? ", draws UI" : "", dump_seen.size());
    for (auto &b : dump_seen)
        swofty::log("[postfx]   %s %llux%u fmt %d: 0x%x -> 0x%x", name_of(b.res).c_str(),
                    (unsigned long long)b.desc.Width, b.desc.Height, int(b.desc.Format), unsigned(b.before),
                    unsigned(b.after));
}

void log_minute(ULONGLONG now)
{
    if (diag_minutes >= DIAG_MINUTES || now < diag_at)
        return;
    if (diag_at)
        swofty::log("[postfx] diag: last 60 s: %u submission(s), %u list(s) marked tonemap, %u wrote the back "
                    "buffer, %u frame(s) through the passes; tonemap output %s; off switch %s",
                    exec_count, tonemap_lists, back_lists, ran_total, tonemap_named ? "named" : "never named",
                    killed ? "on" : "off");
    diag_minutes += diag_at != 0;
    diag_at = now + 60000;
    exec_count = tonemap_lists = back_lists = 0;
}

void on_setname(ID3D12Object *o, LPCWSTR name)
{
    static const uint32_t probe = perf::probe("core", "postfx SetName");
    if (name) {
        perf::Scope p(probe);
        auto *res = reinterpret_cast<ID3D12Resource *>(o);
        std::lock_guard<std::recursive_mutex> g(lock);
        if (watched_bits(res)) {
            names.erase(res);
            watch(res, 0);
        }
        if (res == depth_res)
            depth_res = nullptr;
        bool tonemap = wcscmp(name, TONEMAP_NAME) == 0, back = wcscmp(name, BACKBUFFER_NAME) == 0;
        uint8_t bits = (tonemap ? W_TONEMAP : 0) | (back ? W_BACK : 0) |
                       (wcscmp(name, HUDLESS_NAME) == 0 ? W_HUDLESS : 0) |
                       (wcscmp(name, DEPTH_NAME) == 0 ? W_DEPTH : 0) |
                       (wcsncmp(name, UI_PREFIX, wcslen(UI_PREFIX)) == 0 ? W_UI : 0);
        if (tonemap || back || (!diag_done && wcsstr(name, L"eaid://")))
            names[res] = name;
        if (back && !saw_back) {
            saw_back = true;
            swofty::log("[postfx] the game names its back buffers \"BackBuffer\"");
        }
        if (tonemap && !tonemap_named) {
            tonemap_named = true;
            swofty::log("[postfx] the game named its tonemap output %p", static_cast<void *>(res));
        }
        if (interesting(name))
            log_named(o, name);
        if (bits)
            watch(res, bits);
    }
}

bool on_barrier(ID3D12GraphicsCommandList *list, UINT n, const D3D12_RESOURCE_BARRIER *barriers)
{
    static const uint32_t probe = perf::probe("core", "postfx ResourceBarrier");
    if (barriers && n && !broken) {
        perf::Scope p(probe);
        if (dump_armed) {
            std::lock_guard<std::recursive_mutex> g(lock);
            for (UINT i = 0; i < n; i++) {
                if (barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                    continue;
                if (dump_seen.size() >= DUMP_LINES)
                    dump_seen.erase(dump_seen.begin());
                const auto &t = barriers[i].Transition;
                dump_seen.push_back({t.pResource, t.StateBefore, t.StateAfter, t.pResource->GetDesc()});
            }
        }
        bool look = false;
        for (UINT i = 0; i < n && !look; i++)
            look = barriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
                   watched_bits(barriers[i].Transition.pResource);
        if (!look)
            return false;
        static const uint32_t marking = perf::probe("core", "postfx ResourceBarrier (watched)");
        perf::Scope m_scope(marking);
        std::lock_guard<std::recursive_mutex> g(lock);
        Mark *m = nullptr;
        bool snap_here = false, moves_back = false;
        for (UINT i = 0; i < n; i++) {
            const D3D12_RESOURCE_BARRIER &b = barriers[i];
            if (b.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                continue;
            ID3D12Resource *r = b.Transition.pResource;
            uint8_t c = watched_bits(r);
            bool tonemap = c & W_TONEMAP, back = c & W_BACK, depth = c & W_DEPTH;
            moves_back |= back;
            if (!m) {
                auto it = marks.find(list);
                if (it == marks.end() && !tonemap && !back && !depth && !(c & W_HUDLESS))
                    continue;
                m = &marks[list];
            }
            if ((c & W_HUDLESS) && (b.Transition.StateBefore & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) && m->target &&
                m->state == D3D12_RESOURCE_STATE_RENDER_TARGET && m->snap < 0)
                snap_here = true;
            if (depth) {
                m->depth = r;
                m->depth_state = b.Transition.StateAfter;
            }
            if (c & W_UI)
                m->ui = true;
            if (c & W_HUDLESS) {
                m->hudless = r;
                m->hudless_state = b.Transition.StateAfter;
            }
            if (!diag_done && m->seen.size() < 96)
                m->seen.push_back(b);
            if (tonemap && (b.Transition.StateBefore & (D3D12_RESOURCE_STATE_RENDER_TARGET |
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) &&
                (b.Transition.StateAfter & READ_STATES))
                m->tonemap = true;
            if (back) {
                m->target = r;
                m->state = b.Transition.StateAfter;
            }
        }
        if (snap_here && !moves_back && at_present && !killed && !passes.empty()) {
            ID3D12Device *dev = nullptr;
            if (SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&dev)))) {
                if (make_snaps(dev, m->target->GetDesc())) {
                    UINT k = snap_next++ % SNAPS;
                    copy_back(list, m->target, D3D12_RESOURCE_STATE_RENDER_TARGET, snaps[k].tex, true);
                    m->snap = int(k);
                }
                dev->Release();
            }
        }
    }
    return false;
}

// The copy list for a frame whose picture list ends with the back buffer still a render target.
void snapshot_between(ID3D12CommandQueue *q, ID3D12Resource *back, D3D12_RESOURCE_STATES state)
{
    ID3D12Device *dev = nullptr;
    if (FAILED(q->GetDevice(IID_PPV_ARGS(&dev))))
        return;
    bool made = make_snaps(dev, back->GetDesc());
    dev->Release();
    if (!made)
        return;
    UINT k = snap_next++ % SNAPS;
    Snap &sn = snaps[k];
    if (copy_fence->GetCompletedValue() < sn.copy_fence || FAILED(sn.alloc->Reset()) ||
        FAILED(sn.list->Reset(sn.alloc, nullptr)))
        return;
    copy_back(sn.list, back, state, sn.tex, false);
    if (FAILED(sn.list->Close()))
        return;
    if (fence && sn.read_fence > fence->GetCompletedValue())
        q->Wait(fence, sn.read_fence);
    ID3D12CommandList *one[] = {sn.list};
    d3d12hooks::exec_original(q, 1u, one);
    sn.copy_fence = ++copy_next;
    q->Signal(copy_fence, sn.copy_fence);
    frame_snap = int(k);
    snapped_between++;
}

bool on_exec(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists)
{
    static const uint32_t probe = perf::probe("core", "postfx ExecuteCommandLists");
    perf::Scope p(probe);
    UINT split = n;
    ID3D12Resource *res = nullptr, *hudless = nullptr, *later_depth = nullptr;
    D3D12_RESOURCE_STATES state{}, hudless_state{}, later_depth_state{};
    if (lists && n && !broken) {
        std::lock_guard<std::recursive_mutex> g(lock);
        ULONGLONG at = GetTickCount64();
        exec_count++;
        if (!dump_armed && dumps_left && at >= dump_at) {
            dump_armed = true;
            dump_seen.clear();
        }
        if (!marks.empty()) {
            for (UINT i = 0; i < n; i++) {
                auto it = marks.find(reinterpret_cast<ID3D12GraphicsCommandList *>(lists[i]));
                if (it == marks.end())
                    continue;
                Mark &m = it->second;
                tonemap_lists += m.tonemap;
                if (m.snap >= 0) {
                    Snap &sn = snaps[m.snap];
                    if (fence && sn.read_fence > fence->GetCompletedValue())
                        q->Wait(fence, sn.read_fence);
                    frame_snap = m.snap;
                    snapped_in_list++;
                }
                if (m.target) {
                    back_lists++;
                    if (dump_armed && dump_seen.size() >= 8) {
                        log_dump(reinterpret_cast<ID3D12GraphicsCommandList *>(lists[i]), m);
                        dumps_left--;
                        dump_armed = false;
                        dump_seen.clear();
                        dump_at = at + 60000;
                    }
                }
                if (m.tonemap && split == n) {
                    if (!diag_done) {
                        log_diag(reinterpret_cast<ID3D12GraphicsCommandList *>(lists[i]), m);
                        diag_done = true;
                    }
                    if (m.ui)
                        skipped_ui++;
                    else if (!m.target || m.state != D3D12_RESOURCE_STATE_RENDER_TARGET)
                        skipped_state++;
                    else if (!at_present || (m.snap < 0 && !killed)) {
                        split = i;
                        res = m.target;
                        state = m.state;
                        hudless = m.hudless;
                        hudless_state = m.hudless_state;
                    }
                }
                if (m.depth) {
                    if (split == n || i <= split) {
                        depth_res = m.depth;
                        depth_state = m.depth_state;
                    } else {
                        later_depth = m.depth;
                        later_depth_state = m.depth_state;
                    }
                }
                marks.erase(it);
            }
        }
        frame_count++;
        ULONGLONG now = GetTickCount64();
        log_minute(now);
        if (stats_logged < 6 && now >= stats_at && (ran || skipped_ui || skipped_state)) {
            if (stats_at)
                swofty::log("[postfx] last 10 s: passes ran on %u frame(s); %u tonemap list(s) also drew UI and %u left "
                            "no back buffer to draw on, so passes skipped them",
                            ran, skipped_ui, skipped_state);
            stats_logged += stats_at != 0;
            stats_at = now + 10000;
            ran = skipped_ui = skipped_state = 0;
        }
    }
    if (split == n || killed_now()) {
        if (later_depth) {
            std::lock_guard<std::recursive_mutex> g(lock);
            depth_res = later_depth;
            depth_state = later_depth_state;
        }
        return false;
    }
    {
        perf::Pause game;
        d3d12hooks::exec_original(q, split + 1, lists);
    }
    {
        std::lock_guard<std::recursive_mutex> g(lock);
        if (at_present)
            snapshot_between(q, res, state);
        else
            run(q, res, state, hudless, hudless_state);
        if (later_depth) {
            depth_res = later_depth;
            depth_state = later_depth_state;
        }
    }
    if (split + 1 < n) {
        perf::Pause game;
        d3d12hooks::exec_original(q, n - split - 1, lists + split + 1);
    }
    return true;
}

// Frame generation from the game's hardware settings, read every two seconds; on while unknown.
bool frame_gen_on()
{
    static ULONGLONG checked;
    static bool on = true;
    ULONGLONG now = GetTickCount64();
    if (checked && now - checked < 2000)
        return on;
    checked = now;
    if (hardware_settings.empty()) {
        wchar_t docs[MAX_PATH] = {};
        using GetFolder = HRESULT(WINAPI *)(HWND, int, HANDLE, DWORD, LPWSTR);
        HMODULE shell = LoadLibraryW(L"shell32.dll");
        auto get = shell ? reinterpret_cast<GetFolder>(GetProcAddress(shell, "SHGetFolderPathW")) : nullptr;
        if (get && SUCCEEDED(get(nullptr, 0x0005, nullptr, 0, docs)))
            hardware_settings = std::wstring(docs) + L"\\My Games\\F1 25\\hardwaresettings\\hardware_settings_config.xml";
    }
    on = hud::guard::fg_setting(runtime::read_file(hardware_settings, size_t(1) << 20)).on();
    return on;
}

// Runs the passes for the frame being presented on this frame's snapshot. Whether passes run at
// Present is decided here every frame, so the snapshots start one frame after it turns on.
void on_present(IDXGISwapChain3 *sc, const d3d12hooks::Swapchain *known, void *)
{
    static const uint32_t probe = perf::probe("core", "postfx Present");
    perf::Scope p(probe);
    std::lock_guard<std::recursive_mutex> g(lock);
    int k = frame_snap;
    frame_snap = -1;
    if (broken || !merge_pso)
        return;
    const char *why = nullptr;
    ID3D12Resource *shown = nullptr;
    if (!known || !known->queue)
        why = "its present queue is unknown";
    else if (!known->fg.empty() || (known->streamline && frame_gen_on()))
        why = "frame generation is on or unknown in the game's settings";
    else if (FAILED(sc->GetBuffer(sc->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&shown))) || !shown)
        why = "its back buffer could not be read";
    else if (view_format(shown->GetDesc().Format) == DXGI_FORMAT_UNKNOWN)
        why = "its back buffer format is not one the passes take";
    if (why) {
        release(shown);
        if (why != present_why) {
            present_why = why;
            swofty::log("[postfx] swapchain %p: passes stay before the HUD, as %s", static_cast<void *>(sc), why);
        }
        at_present = false;
        return;
    }
    if (present_why || !at_present) {
        present_why = nullptr;
        merge_told = false;
        at_present = true;
    }
    presented++;
    ULONGLONG now = GetTickCount64();
    if (snap_lines < 30 && now >= snap_log_at) {
        if (snap_log_at)
            swofty::log("[postfx] last 60 s at Present: %u frame(s); snapshots before the HUD: %u in the game's lists, "
                        "%u between them; %u frame(s) without one",
                        presented, snapped_in_list, snapped_between, snap_missed);
        snap_lines += snap_log_at != 0;
        snap_log_at = now + 60000;
        presented = snapped_in_list = snapped_between = snap_missed = 0;
    }
    D3D12_RESOURCE_DESC bd = shown->GetDesc();
    if (killed_now() || k < 0 || bd.Width != snap_desc.Width || bd.Height != snap_desc.Height ||
        view_format(bd.Format) != view_format(snap_desc.Format)) {
        snap_missed += !killed && ran_total;
        release(shown);
        return;
    }
    if (run(known->queue, snaps[k].tex, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, D3D12_RESOURCE_STATE_COMMON, shown))
        snaps[k].read_fence = fence_next;
    release(shown);
}

bool load_passes()
{
    for (auto &e : runtime::config()["postfx"].a) {
        Pass p;
        p.addon = e["addon"].str("an addon");
        p.name = e["name"].str("pass");
        p.shader = e["shader"].str();
        const json::Value &b = e["budget_ms"];
        if (b.type == json::Value::Num && b.n > 0)
            p.budget_ms = b.n;
        size_t k = 0;
        for (auto &o : e["options"].a)
            if (k < 16)
                p.options[k++] = o.type == json::Value::Num ? float(o.n) : 0.0f;
        std::string bytes = runtime::read_file(runtime::under_swofty(p.shader), size_t(4) << 20);
        if (bytes.size() < 32 || memcmp(bytes.data(), "DXBC", 4) != 0) {
            swofty::log("[postfx] %s's pass %s is off: %s is %s", p.addon.c_str(), p.name.c_str(), p.shader.c_str(),
                        bytes.empty() ? "missing" : "not a DXIL container");
            continue;
        }
        p.dxil.assign(bytes.begin(), bytes.end());
        passes.push_back(std::move(p));
    }
    return !passes.empty();
}

}

namespace runtime {

void postfx_setup()
{
    if (!enabled("postfx", nullptr))
        return;
    char v[260] = {};
    if (GetEnvironmentVariableA("SWOFTY_POSTFX", v, sizeof v) && v[0] == '0') {
        swofty::log("[postfx] SWOFTY_POSTFX=0; every pass is off");
        return;
    }
    off_file = swofty_root() + L"postfx.off";
    if (!load_passes()) {
        swofty::log("[postfx] the postfx feature is on, but assets.json names no usable pass");
        return;
    }
    if (GetEnvironmentVariableA("SWOFTY_POSTFX_CAPTURE", v, sizeof v) && v[0]) {
        capture_dir = v;
        capture_file = swofty_root() + L"postfx.capture";
    }
    d3d12hooks::on_setname(on_setname);
    d3d12hooks::on_barrier(on_barrier);
    d3d12hooks::on_exec(on_exec);
    d3d12hooks::on_present(on_present);
    d3d12hooks::ProbeScope probe;
    bool hooked = d3d12hooks::arm_device();
    if (hooked && !d3d12hooks::arm_swapchain())
        swofty::log("[postfx] the swapchain hooks did not arm; passes stay before the HUD");
    if (!hooked)
        swofty::log("[postfx] the core's D3D12 device hooks did not arm; post-processing is off");
    if (hooked)
        swofty::log("[postfx] %zu pass(es) wait for the game's tonemap%s%s", passes.size(),
                    capture_dir.empty() ? "" : "; captures go to ", capture_dir.c_str());
}

}
