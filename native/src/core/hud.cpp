#include "runtime.hpp"

#include "hud_drawlist.hpp"
#include "hud_font_atlas.hpp"
#include "hud_image.hpp"
#include "hud_input.hpp"
#include "hud_guard.hpp"
#include "hud_png.hpp"
#include "perf.hpp"
#include "d3d12_hooks.hpp"

#include <safetyhook.hpp>
#include <swofty/mixin.hpp>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windowsx.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

std::string settings_addon(const void *from, std::string &dll);

namespace {

struct Caller {
    std::string id, dll;
};

Caller caller(const void *from)
{
    Caller c;
    c.id = settings_addon(from, c.dll);
    return c;
}

bool gate_on()
{
    static const bool on = runtime::enabled("overlay", nullptr);
    return on;
}

std::mutex reg_lock;
hud::Registry reg;
bool implemented;

const std::vector<std::string> &overlay_off()
{
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> out;
        for (auto &x : runtime::config()["overlay_off"].a)
            if (x.type == json::Value::Str)
                out.push_back(x.s);
        return out;
    }();
    return ids;
}

std::string owner_of(const Caller &c)
{
    return c.id.empty() ? c.dll : c.id;
}

std::string addon_image(const Caller &c, const char *file, std::string &why)
{
    if (c.id.empty())
        return why = "no addon in settings.json lists " + c.dll + ", so it has no folder", std::string();
    std::string f = file ? file : "";
    std::replace(f.begin(), f.end(), '\\', '/');
    if (f.empty() || f[0] == '/' || f.find(':') != std::string::npos || ("/" + f + "/").find("/../") != std::string::npos)
        return why = "'" + f + "' is not a relative path inside swofty/addons/" + c.id + "/", std::string();
    std::string rel = "addons/" + c.id + "/" + f;
    uint64_t size = 0;
    if (!runtime::file_size(runtime::under_swofty(rel), &size))
        return why = "swofty/" + rel + " is missing", std::string();
    return rel;
}

// ----- D3D12 present hook + minimal solid-geometry renderer -------------------------------------
// The renderer draws the draw list's batches (hud_drawlist.hpp): solid, image and SDF-text triangles,
// one texture and one scissor rect per batch. Textures (the font atlas and the addons' images) upload
// on the overlay's own command list the first frame after they load.
//
// It draws only on a swapchain whose creation it saw (the DXGI factory's CreateSwapChain* slots are
// hooked before the game makes its window) and only on the queue that swapchain was created with, so
// its work runs after the game's frame and before the present on the same queue. Swapchains that a
// frame-generation module created or presents are left alone. A ring of command lists behind one
// fence replaces any wait in Present: a frame whose slot is still on the GPU is skipped. A device
// removal stops the overlay for the rest of the session.

using Vertex = hud::Vertex;

template <class T> void release(T *&p)
{
    if (p)
        p->Release();
    p = nullptr;
}

// d3d12.dll / dxgi.dll are resolved at arm time so the core keeps no hard import on them: it loads
// unchanged where the overlay feature is off or D3D12 is absent.
using CreateDeviceFn = PFN_D3D12_CREATE_DEVICE;
using SerializeRootFn = PFN_D3D12_SERIALIZE_ROOT_SIGNATURE;
using CreateFactoryFn = HRESULT(WINAPI *)(REFIID, void **);

CreateDeviceFn d3d12_create_device;
SerializeRootFn d3d12_serialize_root;
CreateFactoryFn dxgi_create_factory;

constexpr UINT MAX_VERTS = hud::MAX_VERTS;
constexpr UINT TEX_MAX = 256;
constexpr UINT SLOTS = 3;
constexpr ULONGLONG STATS_MS = 60000;

using d3d12hooks::module_of;

bool kill_has(const std::string &list, const char *what)
{
    std::string l = "," + runtime::lower(list) + ",";
    for (auto &c : l)
        if (c == ' ' || c == ';')
            c = ',';
    return l.find(std::string(",") + what + ",") != std::string::npos;
}

bool killed(const char *what)
{
    static std::string list = [] {
        char v[256] = {};
        DWORD n = GetEnvironmentVariableA("SWOFTY_OVERLAY_KILL", v, sizeof v);
        std::string out = n && n < sizeof v ? std::string(v, n) : std::string();
        for (auto &x : runtime::config()["overlay_kill"].a)
            if (x.type == json::Value::Str)
                out += "," + x.s;
        return out;
    }();
    return kill_has(list, "all") || kill_has(list, what);
}

std::vector<std::pair<void *, bool>> told;
std::vector<void *> unknown_told;

struct Pso {
    DXGI_FORMAT fmt;
    ID3D12PipelineState *pso;
};

struct Slot {
    ID3D12CommandAllocator *alloc = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    UINT64 done_at = 0;
};

struct Renderer {
    ID3D12Device *dev = nullptr;
    ID3D12RootSignature *root = nullptr, *comp_root = nullptr;
    ID3DBlob *vs = nullptr, *ps = nullptr, *cvs = nullptr, *cps = nullptr;
    ID3D12PipelineState *geo = nullptr;
    std::vector<Pso> psos;
    ID3D12DescriptorHeap *rtv_heap = nullptr, *srv_heap = nullptr;
    UINT rtv_inc = 0, srv_inc = 0;
    Slot slots[SLOTS];
    UINT next = 0;
    ID3D12Resource *vbuf = nullptr;
    uint8_t *vmap = nullptr;
    ID3D12Fence *fence = nullptr;
    UINT64 fence_val = 0;
    ID3D12Resource *layer = nullptr;
    UINT layer_w = 0, layer_h = 0;
    D3D12_RESOURCE_STATES layer_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    std::vector<std::pair<UINT64, ID3D12Resource *>> graveyard;
    ID3D12QueryHeap *ts_heap = nullptr;
    ID3D12Resource *ts_buf = nullptr;
    UINT64 ts_freq = 0;
    double gpu_ms = 0;
    unsigned gpu_n = 0;
    bool ready = false;
    bool failed = false;

    static constexpr DXGI_FORMAT LAYER_FMT = DXGI_FORMAT_R8G8B8A8_UNORM;

    bool compile(pD3DCompile fn, const char *entry, const char *model, ID3DBlob **out)
    {
        static const char hlsl[] =
            "cbuffer C:register(b0){float2 inv;}"
            "Texture2D T:register(t1);SamplerState S:register(s0);"
            "struct I{float2 p:POSITION;float2 t:TEXCOORD;float4 c:COLOR;uint m:MODE;};"
            "struct O{float4 p:SV_POSITION;float2 t:TEXCOORD;float4 c:COLOR;nointerpolation uint m:MODE;};"
            "O VS(I i){O o;o.p=float4(i.p.x*inv.x-1.0,1.0-i.p.y*inv.y,0,1);o.t=i.t;o.c=i.c;o.m=i.m;return o;}"
            "float4 PS(O i):SV_TARGET{float4 s=T.Sample(S,i.t);float w=max(fwidth(s.r),1e-5);uint k=i.m&255;"
            "float4 t=float4(1,1,1,1);if(k==1)t=s;else if(k==2){float e=0.5-float((i.m>>8)&255)/510.0;"
            "float h=w*(0.5+float((i.m>>16)&255)/16.0);float a=smoothstep(e-h,e+h,s.r);t=float4(a,a,a,a);}"
            "return float4(t.rgb*i.c.rgb*i.c.a,t.a*i.c.a);}"
            "Texture2D L:register(t0);"
            "cbuffer K:register(b1){uint mode;float white;}"
            "float4 CVS(uint id:SV_VertexID):SV_POSITION{float2 t=float2((id<<1)&2,id&2);"
            "return float4(t*float2(2,-2)+float2(-1,1),0,1);}"
            "float3 lin(float3 c){return c<=0.04045?c/12.92:pow((c+0.055)/1.055,2.4);}"
            "float3 pq(float3 n){float3 y=pow(max(n/10000.0,0),0.1593017578125);"
            "return pow((0.8359375+18.8515625*y)/(1+18.6875*y),78.84375);}"
            "float4 CPS(float4 p:SV_POSITION):SV_TARGET{float4 v=L.Load(int3(p.xy,0));if(v.a<=0.0)discard;"
            "float3 c=v.rgb/v.a;"
            "if(mode==1){float3 l=lin(c);l=float3(dot(l,float3(0.6274040,0.3292820,0.0433136)),"
            "dot(l,float3(0.0690970,0.9195400,0.0113612)),dot(l,float3(0.0163916,0.0880132,0.8955950)));c=pq(l*white);}"
            "else if(mode==2){c=lin(c)*(white/80.0);}"
            "return float4(c*v.a,v.a);}";
        ID3DBlob *err = nullptr;
        HRESULT hr = fn(hlsl, sizeof hlsl - 1, nullptr, nullptr, nullptr, entry, model, 0, 0, out, &err);
        if (err) {
            if (FAILED(hr))
                swofty::log("[overlay] %s: %.*s", entry, int(err->GetBufferSize()),
                            static_cast<const char *>(err->GetBufferPointer()));
            err->Release();
        }
        return SUCCEEDED(hr) && *out;
    }

    ID3D12RootSignature *make_sig(const D3D12_ROOT_SIGNATURE_DESC &rs)
    {
        ID3DBlob *blob = nullptr, *rserr = nullptr;
        ID3D12RootSignature *sig = nullptr;
        HRESULT hr = d3d12_serialize_root(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &rserr);
        if (rserr)
            rserr->Release();
        if (SUCCEEDED(hr))
            hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&sig));
        release(blob);
        return SUCCEEDED(hr) ? sig : nullptr;
    }

    bool make_root()
    {
        HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
        auto compile_fn = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
        if (!compile_fn) {
            swofty::log("[overlay] d3dcompiler_47.dll!D3DCompile is unavailable; overlay rendering off");
            return false;
        }
        if (!compile(compile_fn, "VS", "vs_5_0", &vs) || !compile(compile_fn, "PS", "ps_5_0", &ps) ||
            !compile(compile_fn, "CVS", "vs_5_0", &cvs) || !compile(compile_fn, "CPS", "ps_5_0", &cps)) {
            swofty::log("[overlay] overlay shader compilation failed");
            return false;
        }
        D3D12_DESCRIPTOR_RANGE trange{};
        trange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        trange.NumDescriptors = 1;
        trange.BaseShaderRegister = 1;
        D3D12_ROOT_PARAMETER rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].Constants.Num32BitValues = 2;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &trange;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC samp{};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD = D3D12_FLOAT32_MAX;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.NumParameters = 2;
        rs.pParameters = rp;
        rs.NumStaticSamplers = 1;
        rs.pStaticSamplers = &samp;
        rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        root = make_sig(rs);
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER cp[2]{};
        cp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        cp[0].Constants.ShaderRegister = 1;
        cp[0].Constants.Num32BitValues = 2;
        cp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        cp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        cp[1].DescriptorTable.NumDescriptorRanges = 1;
        cp[1].DescriptorTable.pDescriptorRanges = &range;
        cp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC crs{};
        crs.NumParameters = 2;
        crs.pParameters = cp;
        comp_root = make_sig(crs);
        return root && comp_root && (geo = make_pso(LAYER_FMT, false));
    }

    ID3D12PipelineState *make_pso(DXGI_FORMAT fmt, bool composite)
    {
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"MODE", 0, DXGI_FORMAT_R32_UINT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = composite ? comp_root : root;
        ID3DBlob *v = composite ? cvs : vs, *p = composite ? cps : ps;
        pd.VS = {v->GetBufferPointer(), v->GetBufferSize()};
        pd.PS = {p->GetBufferPointer(), p->GetBufferSize()};
        if (!composite)
            pd.InputLayout = {layout, 4};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = fmt;
        pd.SampleDesc.Count = 1;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        auto &rt = pd.BlendState.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        ID3D12PipelineState *pso = nullptr;
        return SUCCEEDED(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso))) ? pso : nullptr;
    }

    ID3D12PipelineState *pso_for(DXGI_FORMAT fmt)
    {
        for (auto &p : psos)
            if (p.fmt == fmt)
                return p.pso;
        ID3D12PipelineState *pso = make_pso(fmt, true);
        if (pso)
            psos.push_back({fmt, pso});
        return pso;
    }

    ID3D12Resource *buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state)
    {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = type;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = size;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource *r = nullptr;
        return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&r)))
                   ? r
                   : nullptr;
    }

    bool make_resources(ID3D12CommandQueue *q)
    {
        for (auto &s : slots) {
            if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc))))
                return false;
            if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.alloc, nullptr, IID_PPV_ARGS(&s.list))))
                return false;
            s.list->Close();
        }
        if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
            return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = SLOTS * 2;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap))))
            return false;
        rtv_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_DESCRIPTOR_HEAP_DESC sd{};
        sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sd.NumDescriptors = SLOTS + TEX_MAX;
        sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&srv_heap))))
            return false;
        srv_inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_SHADER_RESOURCE_VIEW_DESC null_srv{};
        null_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_srv.Texture2D.MipLevels = 1;
        for (UINT i = 0; i < TEX_MAX; i++)
            dev->CreateShaderResourceView(nullptr, &null_srv, tex_cpu(i));
        textures.assign(TEX_MAX, {});
        vbuf = buffer(D3D12_HEAP_TYPE_UPLOAD, UINT64(sizeof(Vertex)) * MAX_VERTS * SLOTS, D3D12_RESOURCE_STATE_GENERIC_READ);
        D3D12_RANGE none{0, 0};
        if (!vbuf || FAILED(vbuf->Map(0, &none, reinterpret_cast<void **>(&vmap))))
            return false;
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = SLOTS * 2;
        if (SUCCEEDED(q->GetTimestampFrequency(&ts_freq)) && ts_freq &&
            SUCCEEDED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&ts_heap))))
            ts_buf = buffer(D3D12_HEAP_TYPE_READBACK, SLOTS * 16, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!ts_buf)
            release(ts_heap);
        return true;
    }

    std::vector<ID3D12CommandQueue *> queues;

    struct Tex {
        ID3D12Resource *res = nullptr;
        uint32_t generation = 0;
    };
    std::vector<Tex> textures;
    hud::img::Pixels font_px;
    bool font_tried = false;
    unsigned uploads = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE tex_cpu(UINT i)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = srv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += SIZE_T(SLOTS + i) * srv_inc;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE tex_gpu(UINT i)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = srv_heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += UINT64(SLOTS + i) * srv_inc;
        return h;
    }

    bool upload(ID3D12GraphicsCommandList *l, UINT index, DXGI_FORMAT fmt, UINT w, UINT h, UINT bpp,
                const uint8_t *pixels)
    {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = fmt;
        rd.SampleDesc.Count = 1;
        ID3D12Resource *tex = nullptr;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&tex))))
            return false;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 total = 0;
        dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, &total);
        ID3D12Resource *staging = buffer(D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_STATE_GENERIC_READ);
        uint8_t *map = nullptr;
        D3D12_RANGE none{0, 0};
        if (!staging || FAILED(staging->Map(0, &none, reinterpret_cast<void **>(&map))) || !map) {
            release(staging);
            tex->Release();
            return false;
        }
        for (UINT y = 0; y < h; y++)
            memcpy(map + fp.Offset + SIZE_T(y) * fp.Footprint.RowPitch, pixels + SIZE_T(y) * w * bpp, SIZE_T(w) * bpp);
        staging->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = tex;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = staging;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;
        l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transition(l, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = fmt;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(tex, &sv, tex_cpu(index));
        graveyard.push_back({fence_val + 1, staging});
        if (textures[index].res)
            graveyard.push_back({fence_val + 1, textures[index].res});
        textures[index].res = tex;
        uploads++;
        return true;
    }

    void sync_textures(ID3D12GraphicsCommandList *l)
    {
        if (!font_tried) {
            font_tried = true;
            if (!hud::img::grey(hud::font::ATLAS_PNG, hud::font::ATLAS_PNG_SIZE, font_px))
                swofty::log("[overlay] decoding the built-in font atlas failed; text draws nothing");
        }
        if (!font_px.rgba.empty()) {
            if (upload(l, hud::TEX_FONT, DXGI_FORMAT_R8_UNORM, font_px.width, font_px.height, 1, font_px.rgba.data()))
                swofty::log("[overlay] font atlas uploaded: %s, %ux%u SDF", hud::font::NAME, font_px.width,
                            font_px.height);
            else
                swofty::log("[overlay] uploading the font atlas failed; text draws nothing");
            font_px = {};
        }
        std::lock_guard<std::mutex> g(reg_lock);
        for (size_t slot = 0; slot < reg.image_count(); slot++) {
            hud::Image *im = reg.image(int(slot));
            UINT index = UINT(slot + 1);
            if (!im || im->rgba.empty() || index >= TEX_MAX || textures[index].generation == im->generation)
                continue;
            bool ok = upload(l, index, DXGI_FORMAT_R8G8B8A8_UNORM, im->width, im->height, 4, im->rgba.data());
            swofty::log("[overlay] image %s (%ux%u) %s", im->id.substr(im->id.find('\n') + 1).c_str(), im->width,
                        im->height, ok ? "uploaded" : "upload failed; it draws nothing");
            textures[index].generation = im->generation;
            im->rgba.clear();
            im->rgba.shrink_to_fit();
        }
    }

    bool same_device(ID3D12CommandQueue *q)
    {
        if (std::find(queues.begin(), queues.end(), q) != queues.end())
            return true;
        ID3D12Device *d = nullptr;
        if (FAILED(q->GetDevice(IID_PPV_ARGS(&d))) || !d)
            return false;
        bool same = d == dev;
        d->Release();
        if (same)
            queues.push_back(q);
        return same;
    }

    bool init(ID3D12CommandQueue *q)
    {
        if (FAILED(q->GetDevice(IID_PPV_ARGS(&dev))))
            return false;
        if (!make_root() || !make_resources(q))
            return false;
        ready = true;
        return true;
    }

    void bury()
    {
        UINT64 done = fence->GetCompletedValue();
        for (size_t i = 0; i < graveyard.size();)
            if (graveyard[i].first <= done) {
                graveyard[i].second->Release();
                graveyard[i] = graveyard.back();
                graveyard.pop_back();
            } else {
                i++;
            }
    }

    bool ensure_layer(UINT w, UINT h)
    {
        if (layer && layer_w == w && layer_h == h)
            return true;
        if (layer)
            graveyard.push_back({fence_val, layer});
        layer = nullptr;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = LAYER_FMT;
        rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = LAYER_FMT;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&layer))))
            return false;
        layer_w = w;
        layer_h = h;
        layer_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return true;
    }

    void read_timestamps(UINT slot)
    {
        if (!ts_buf || !slots[slot].done_at)
            return;
        D3D12_RANGE r{SIZE_T(slot) * 16, SIZE_T(slot) * 16 + 16};
        uint64_t *t = nullptr;
        if (FAILED(ts_buf->Map(0, &r, reinterpret_cast<void **>(&t))) || !t)
            return;
        uint64_t a = t[slot * 2], b = t[slot * 2 + 1];
        D3D12_RANGE none{0, 0};
        ts_buf->Unmap(0, &none);
        if (b > a) {
            gpu_ms += double(b - a) * 1000.0 / double(ts_freq);
            gpu_n++;
        }
    }

    static void transition(ID3D12GraphicsCommandList *l, ID3D12Resource *r, D3D12_RESOURCE_STATES from,
                           D3D12_RESOURCE_STATES to)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Transition.pResource = r;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        l->ResourceBarrier(1, &b);
    }

    enum Outcome { drawn, busy, broken };

    Outcome render(IDXGISwapChain3 *sc, ID3D12CommandQueue *q, DXGI_FORMAT fmt, UINT width, UINT height,
                   const hud::Frame &frame, UINT mode, float white)
    {
        const std::vector<Vertex> &verts = frame.verts;
        Slot &s = slots[next];
        if (fence->GetCompletedValue() < s.done_at)
            return busy;
        bury();
        read_timestamps(next);
        size_t n = std::min<size_t>(verts.size(), MAX_VERTS);
        float x0 = float(width), y0 = float(height), x1 = 0, y1 = 0;
        for (size_t i = 0; i < n; i++) {
            x0 = std::min(x0, verts[i].x), y0 = std::min(y0, verts[i].y);
            x1 = std::max(x1, verts[i].x), y1 = std::max(y1, verts[i].y);
        }
        D3D12_RECT box{LONG(std::max(0.0f, std::floor(x0) - 1)), LONG(std::max(0.0f, std::floor(y0) - 1)),
                       LONG(std::min(float(width), std::ceil(x1) + 1)), LONG(std::min(float(height), std::ceil(y1) + 1))};
        if (box.right <= box.left || box.bottom <= box.top)
            return drawn;
        ID3D12PipelineState *pso = pso_for(fmt);
        if (!pso || !ensure_layer(width, height))
            return broken;
        ID3D12Resource *buf = nullptr;
        if (FAILED(sc->GetBuffer(sc->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buf))) || !buf)
            return broken;
        size_t base = size_t(next) * MAX_VERTS * sizeof(Vertex);
        memcpy(vmap + base, verts.data(), n * sizeof(Vertex));
        if (FAILED(s.alloc->Reset()) || FAILED(s.list->Reset(s.alloc, geo))) {
            buf->Release();
            return broken;
        }
        ID3D12GraphicsCommandList *l = s.list;
        if (ts_heap)
            l->EndQuery(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, next * 2);
        sync_textures(l);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart(), lrtv = rtv;
        rtv.ptr += SIZE_T(next) * 2 * rtv_inc;
        lrtv.ptr += (SIZE_T(next) * 2 + 1) * rtv_inc;
        dev->CreateRenderTargetView(buf, nullptr, rtv);
        dev->CreateRenderTargetView(layer, nullptr, lrtv);
        D3D12_CPU_DESCRIPTOR_HANDLE srv = srv_heap->GetCPUDescriptorHandleForHeapStart();
        srv.ptr += SIZE_T(next) * srv_inc;
        dev->CreateShaderResourceView(layer, nullptr, srv);
        D3D12_GPU_DESCRIPTOR_HANDLE gsrv = srv_heap->GetGPUDescriptorHandleForHeapStart();
        gsrv.ptr += UINT64(next) * srv_inc;
        if (layer_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
            transition(l, layer, layer_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        layer_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        const float clear[4] = {0, 0, 0, 0};
        l->OMSetRenderTargets(1, &lrtv, FALSE, nullptr);
        l->ClearRenderTargetView(lrtv, clear, 1, &box);
        D3D12_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &box);
        l->SetGraphicsRootSignature(root);
        float inv[2] = {2.0f / float(width ? width : 1), 2.0f / float(height ? height : 1)};
        l->SetGraphicsRoot32BitConstants(0, 2, inv, 0);
        l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vbv{vbuf->GetGPUVirtualAddress() + base, UINT(n * sizeof(Vertex)), sizeof(Vertex)};
        l->IASetVertexBuffers(0, 1, &vbv);
        l->SetDescriptorHeaps(1, &srv_heap);
        for (const hud::Batch &b : frame.batches) {
            if (b.first >= n)
                break;
            D3D12_RECT sr{std::max(box.left, LONG(std::floor(b.clip.x0))), std::max(box.top, LONG(std::floor(b.clip.y0))),
                          std::min(box.right, LONG(std::ceil(b.clip.x1))), std::min(box.bottom, LONG(std::ceil(b.clip.y1)))};
            if (sr.right <= sr.left || sr.bottom <= sr.top)
                continue;
            UINT tex = b.tex >= 0 && UINT(b.tex) < TEX_MAX ? UINT(b.tex) : 0;
            l->SetGraphicsRootDescriptorTable(1, tex_gpu(tex));
            l->RSSetScissorRects(1, &sr);
            l->DrawInstanced(std::min<UINT>(b.count, UINT(n) - b.first), 1, b.first, 0);
        }
        D3D12_RESOURCE_BARRIER two[2]{};
        two[0].Transition = {layer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        two[1].Transition = {buf, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT,
                             D3D12_RESOURCE_STATE_RENDER_TARGET};
        l->ResourceBarrier(2, two);
        layer_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        l->SetGraphicsRootSignature(comp_root);
        l->SetDescriptorHeaps(1, &srv_heap);
        uint32_t k[2] = {mode, 0};
        memcpy(&k[1], &white, 4);
        l->SetGraphicsRoot32BitConstants(0, 2, k, 0);
        l->SetGraphicsRootDescriptorTable(1, gsrv);
        l->SetPipelineState(pso);
        l->DrawInstanced(3, 1, 0, 0);
        transition(l, buf, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        if (ts_heap) {
            l->EndQuery(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, next * 2 + 1);
            l->ResolveQueryData(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, next * 2, 2, ts_buf, UINT64(next) * 16);
        }
        if (FAILED(l->Close())) {
            buf->Release();
            return broken;
        }
        ID3D12CommandList *lists[] = {l};
        q->ExecuteCommandLists(1, lists);
        q->Signal(fence, ++fence_val);
        s.done_at = fence_val;
        buf->Release();
        next = (next + 1) % SLOTS;
        return drawn;
    }

    void shutdown()
    {
        release(rtv_heap);
        release(srv_heap);
        release(vbuf);
        vmap = nullptr;
        for (auto &s : slots) {
            release(s.list);
            release(s.alloc);
        }
        release(fence);
        for (auto &p : psos)
            release(p.pso);
        psos.clear();
        for (auto &g : graveyard)
            g.second->Release();
        graveyard.clear();
        for (auto &t : textures)
            release(t.res);
        textures.clear();
        release(layer);
        release(geo);
        release(ts_heap);
        release(ts_buf);
        release(root);
        release(comp_root);
        release(vs);
        release(ps);
        release(cvs);
        release(cps);
        release(dev);
        ready = false;
    }
};

Renderer renderer;
bool dropped_told;
std::atomic<bool> armed, off, lost_once;
std::atomic<ID3D12Device *> live_dev;
std::atomic<ULONGLONG> last_draw_ms;
std::string drawn_by;

struct Stats {
    ULONGLONG since = 0;
    unsigned drawn = 0, busy = 0;
} stats;


void device_lost(HRESULT present_hr)
{
    if (lost_once.exchange(true))
        return;
    off.store(true);
    ID3D12Device *dev = live_dev.load();
    HRESULT reason = dev ? dev->GetDeviceRemovedReason() : S_OK;
    ULONGLONG now = GetTickCount64(), last = last_draw_ms.load();
    if (!hud::guard::removal_after_draw(now, last)) {
        swofty::log("[overlay] the GPU was removed (present 0x%08lx, reason 0x%08lx) %s; the overlay stops drawing",
                    static_cast<unsigned long>(present_hr), static_cast<unsigned long>(reason),
                    last ? "more than 10 s after the overlay last drew" : "and the overlay had not drawn");
        return;
    }
    swofty::log("[overlay] the GPU was removed (present 0x%08lx, reason 0x%08lx) %.1f s after the overlay last drew "
                "(swapchain by %s); the overlay stops drawing",
                static_cast<unsigned long>(present_hr), static_cast<unsigned long>(reason), double(now - last) / 1000,
                drawn_by.c_str());
}

struct FgWatch {
    std::wstring path;
    FILETIME at{};
    ULONGLONG checked = 0;
    hud::guard::FgSetting now;
    bool tripped = false, told = false;
} fg_watch;

std::wstring hardware_settings_path()
{
    using GetFolder = HRESULT(WINAPI *)(HWND, int, HANDLE, DWORD, LPWSTR);
    HMODULE shell = GetModuleHandleW(L"shell32.dll");
    if (!shell)
        shell = LoadLibraryW(L"shell32.dll");
    auto get = shell ? reinterpret_cast<GetFolder>(GetProcAddress(shell, "SHGetFolderPathW")) : nullptr;
    wchar_t docs[MAX_PATH] = {};
    if (!get || FAILED(get(nullptr, 0x0005, nullptr, 0, docs)))
        return {};
    return std::wstring(docs) + L"\\My Games\\F1 25\\hardwaresettings\\hardware_settings_config.xml";
}

bool frame_gen_off(ULONGLONG now)
{
    FgWatch &w = fg_watch;
    if (w.tripped)
        return false;
    if (w.checked && now - w.checked < 2000)
        return !w.now.on();
    w.checked = now;
    if (w.path.empty())
        w.path = hardware_settings_path();
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (w.path.empty() || !GetFileAttributesExW(w.path.c_str(), GetFileExInfoStandard, &fa)) {
        w.now = {};
    } else if (CompareFileTime(&fa.ftLastWriteTime, &w.at) || !w.told) {
        w.at = fa.ftLastWriteTime;
        w.now = hud::guard::fg_setting(runtime::read_file(w.path, 1 << 20));
    }
    bool off = !w.now.on();
    if (!w.told) {
        w.told = true;
        swofty::log("[overlay] the game's hardware settings (%s) have frame generation %s (frame_gen mode %d, "
                    "multi_frame_gen %d)",
                    runtime::narrow(w.path).c_str(), !w.now.known ? "unknown" : off ? "off" : "on", w.now.mode,
                    w.now.multi);
    } else if (!off) {
        swofty::log("[overlay] the game's hardware settings now have frame generation %s (frame_gen mode %d); the "
                    "overlay stops drawing for this session",
                    w.now.known ? "on" : "unknown", w.now.mode);
    }
    if (!off)
        w.tripped = true;
    return off;
}

void flush_stats(ULONGLONG now)
{
    if (!stats.since)
        stats.since = now;
    if (now - stats.since < STATS_MS)
        return;
    if (stats.drawn || stats.busy)
        swofty::log("[overlay] last %llu s: %u frame(s) drawn, %u skipped while the GPU still held the slot; GPU %.3f ms "
                    "per drawn frame (%u timed)",
                    static_cast<unsigned long long>((now - stats.since) / 1000), stats.drawn, stats.busy,
                    renderer.gpu_n ? renderer.gpu_ms / renderer.gpu_n : 0.0, renderer.gpu_n);
    renderer.gpu_ms = 0;
    renderer.gpu_n = 0;
    stats = {now, 0, 0};
}

struct Capture {
    bool on = false;
    std::wstring dir;
    ULONGLONG every_ms = 10000, last = 0;
    unsigned count = 0;
    bool pending = false;
    std::atomic<bool> busy{false};
    UINT64 done_at = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT width = 0, height = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    ID3D12CommandAllocator *alloc = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12Resource *buf = nullptr;
    UINT64 size = 0;
    bool failed = false;
} cap;

std::string env(const char *name)
{
    char v[1024] = {};
    DWORD n = GetEnvironmentVariableA(name, v, sizeof v);
    return n && n < sizeof v ? std::string(v, n) : std::string();
}

void capture_setup()
{
    std::string dir = env("SWOFTY_FRAME_CAPTURE");
    if (dir.empty())
        return;
    std::string every = env("SWOFTY_FRAME_CAPTURE_EVERY");
    double s = every.empty() ? 10 : atof(every.c_str());
    cap.every_ms = ULONGLONG((s > 0.5 ? s : 0.5) * 1000);
    std::replace(dir.begin(), dir.end(), '/', '\\');
    while (!dir.empty() && dir.back() == '\\')
        dir.pop_back();
    cap.dir = runtime::wide(dir);
    for (size_t i = cap.dir.find(L'\\', 3); i != std::wstring::npos; i = cap.dir.find(L'\\', i + 1))
        CreateDirectoryW(cap.dir.substr(0, i).c_str(), nullptr);
    CreateDirectoryW(cap.dir.c_str(), nullptr);
    cap.on = true;
    swofty::log("[capture] frame capture on: the composed frame every %.1f s to %s", double(cap.every_ms) / 1000,
                dir.c_str());
}

DWORD WINAPI capture_write(void *)
{
    ULONGLONG t0 = GetTickCount64();
    uint8_t *mapped = nullptr;
    D3D12_RANGE all{0, SIZE_T(cap.size)};
    std::string why;
    if (FAILED(cap.buf->Map(0, &all, reinterpret_cast<void **>(&mapped))) || !mapped) {
        why = "mapping the readback buffer failed";
    } else {
        std::vector<uint8_t> rgb;
        bool ok = hud::png::to_rgb(hud::png::fmt_of(int(cap.fmt)), mapped + cap.fp.Offset, cap.fp.Footprint.RowPitch,
                                   cap.width, cap.height, rgb);
        D3D12_RANGE none{0, 0};
        cap.buf->Unmap(0, &none);
        if (!ok) {
            why = "the back buffer format is not one the capture converts";
        } else {
            std::string png = hud::png::encode_rgb(rgb.data(), cap.width, cap.height);
            wchar_t name[64];
            _snwprintf(name, 64, L"\\frame-%04u.png", cap.count);
            std::wstring path = cap.dir + name;
            if (!runtime::write_file(path, png))
                why = "writing " + runtime::narrow(path) + " failed";
            else
                swofty::log("[capture] wrote %s (%ux%u, format %d, %zu KB, %llu ms off the render thread)",
                            runtime::narrow(path).c_str(), cap.width, cap.height, int(cap.fmt), png.size() / 1024,
                            static_cast<unsigned long long>(GetTickCount64() - t0));
        }
    }
    if (!why.empty())
        swofty::log("[capture] frame %u: %s", cap.count, why.c_str());
    cap.busy.store(false);
    return 0;
}

void capture_step(IDXGISwapChain3 *sc, ID3D12CommandQueue *q, const DXGI_SWAP_CHAIN_DESC &desc, ULONGLONG now)
{
    if (cap.failed || cap.busy.load())
        return;
    if (cap.pending) {
        if (renderer.fence->GetCompletedValue() < cap.done_at)
            return;
        cap.pending = false;
        cap.busy.store(true);
        HANDLE t = CreateThread(nullptr, 0, capture_write, nullptr, 0, nullptr);
        if (t)
            CloseHandle(t);
        else
            cap.busy.store(false);
        return;
    }
    if (cap.last && now - cap.last < cap.every_ms)
        return;
    if (hud::png::fmt_of(int(desc.BufferDesc.Format)) == hud::png::Fmt::unknown) {
        cap.failed = true;
        swofty::log("[capture] back buffer format %d is not one the capture converts; capture off",
                    int(desc.BufferDesc.Format));
        return;
    }
    ID3D12Device *dev = renderer.dev;
    if (!cap.alloc && (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cap.alloc))) ||
                       FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cap.alloc, nullptr,
                                                     IID_PPV_ARGS(&cap.list))) ||
                       FAILED(cap.list->Close()))) {
        cap.failed = true;
        swofty::log("[capture] making the capture command list failed; capture off");
        return;
    }
    ID3D12Resource *bb = nullptr;
    if (FAILED(sc->GetBuffer(sc->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&bb))) || !bb)
        return;
    D3D12_RESOURCE_DESC rd = bb->GetDesc();
    UINT64 total = 0;
    dev->GetCopyableFootprints(&rd, 0, 1, 0, &cap.fp, nullptr, nullptr, &total);
    if (!cap.buf || cap.size < total) {
        release(cap.buf);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.Format = DXGI_FORMAT_UNKNOWN;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&cap.buf)))) {
            bb->Release();
            cap.failed = true;
            swofty::log("[capture] making a %llu-byte readback buffer failed; capture off",
                        static_cast<unsigned long long>(total));
            return;
        }
        cap.size = total;
    }
    if (FAILED(cap.alloc->Reset()) || FAILED(cap.list->Reset(cap.alloc, nullptr))) {
        bb->Release();
        cap.failed = true;
        return;
    }
    D3D12_RESOURCE_BARRIER b{};
    b.Transition.pResource = bb;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cap.list->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = cap.buf;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = cap.fp;
    src.pResource = bb;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    cap.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cap.list->ResourceBarrier(1, &b);
    if (FAILED(cap.list->Close())) {
        bb->Release();
        cap.failed = true;
        return;
    }
    ID3D12CommandList *lists[] = {cap.list};
    q->ExecuteCommandLists(1, lists);
    q->Signal(renderer.fence, ++renderer.fence_val);
    bb->Release();
    cap.done_at = renderer.fence_val;
    cap.pending = true;
    cap.last = now;
    cap.count++;
    cap.width = desc.BufferDesc.Width;
    cap.height = desc.BufferDesc.Height;
    cap.fmt = desc.BufferDesc.Format;
}

// ----- input --------------------------------------------------------------------------------------
// The core subclasses the window the drawn swapchain presents to. Mouse, wheel, character and key
// messages are queued for the overlay; while an addon holds input focus the button-down, wheel,
// character, key-down and raw input messages do not reach the game (ups always do, so no key sticks).

constexpr ULONGLONG FOCUS_TIMEOUT_MS = 2000;

std::mutex in_lock;
std::vector<hud::input::Event> in_events;
hud::input::Snapshot in_snap, frame_snap;
hud::input::Focus in_focus, frame_focus;
std::atomic<bool> focus_held;
std::atomic<WNDPROC> game_proc;
std::atomic<HWND> game_wnd;
std::atomic<uint32_t> client_size;
std::atomic<unsigned> blocked_msgs, blocked_raw;
hud::input::Utf16 utf16;
std::atomic<ULONGLONG> input_read_ms;

using XInputGetStateFn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
XInputGetStateFn xinput_get;
bool xinput_tried;
hud::input::PadRaw pads[4];
ULONGLONG pad_probe_at[4];

void poll_pads(ULONGLONG now)
{
    if (!xinput_tried) {
        xinput_tried = true;
        HMODULE m = GetModuleHandleW(L"xinput1_4.dll");
        if (!m)
            m = LoadLibraryW(L"xinput1_4.dll");
        xinput_get = m ? reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState")) : nullptr;
        swofty::log(xinput_get ? "[overlay] input: gamepads read through xinput1_4.dll!XInputGetState"
                               : "[overlay] input: xinput1_4.dll!XInputGetState is unavailable; no gamepad input");
    }
    if (!xinput_get)
        return;
    for (DWORD i = 0; i < 4; i++) {
        if (!pads[i].connected && now < pad_probe_at[i])
            continue;
        XINPUT_STATE st{};
        bool was = pads[i].connected;
        bool on = xinput_get(i, &st) == ERROR_SUCCESS;
        if (!on) {
            pads[i] = {};
            pad_probe_at[i] = now + 2000;
        } else {
            const XINPUT_GAMEPAD &g = st.Gamepad;
            pads[i] = {true, g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY};
        }
        if (on != was)
            swofty::log("[overlay] input: gamepad %lu %s", static_cast<unsigned long>(i), on ? "connected" : "disconnected");
    }
}

LRESULT CALLBACK input_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    using hud::input::Ev;
    bool held = focus_held.load(std::memory_order_relaxed);
    hud::input::Event e{};
    bool record = true, block = false;
    float x = float(GET_X_LPARAM(lp)), y = float(GET_Y_LPARAM(lp));
    switch (msg) {
    case WM_MOUSEMOVE:
        e = {Ev::move, 0, x, y};
        block = held;
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        e = {Ev::down, SWOFTY_MOUSE_LEFT, x, y};
        block = held;
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        e = {Ev::down, SWOFTY_MOUSE_RIGHT, x, y};
        block = held;
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        e = {Ev::down, SWOFTY_MOUSE_MIDDLE, x, y};
        block = held;
        break;
    case WM_LBUTTONUP:
        e = {Ev::up, SWOFTY_MOUSE_LEFT, x, y};
        break;
    case WM_RBUTTONUP:
        e = {Ev::up, SWOFTY_MOUSE_RIGHT, x, y};
        break;
    case WM_MBUTTONUP:
        e = {Ev::up, SWOFTY_MOUSE_MIDDLE, x, y};
        break;
    case WM_MOUSEWHEEL:
        e = {Ev::wheel, 0, float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA, 0};
        block = held;
        break;
    case WM_CHAR:
        e = {Ev::chr, utf16.push(uint32_t(wp)), 0, 0};
        record = e.a != 0;
        block = held;
        break;
    case WM_KEYDOWN:
        e = {Ev::key_down, uint32_t(wp) & 0xff, 0, 0};
        block = held;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        e = {Ev::key_up, uint32_t(wp) & 0xff, 0, 0};
        break;
    case WM_SYSKEYDOWN:
        e = {Ev::key_down, uint32_t(wp) & 0xff, 0, 0};
        break;
    case WM_SIZE:
        client_size.store(uint32_t(lp), std::memory_order_relaxed);
        record = false;
        break;
    case WM_INPUT:
        record = false;
        if (held) {
            blocked_raw.fetch_add(1, std::memory_order_relaxed);
            return DefWindowProcW(w, msg, wp, lp);
        }
        break;
    default:
        record = false;
    }
    if (record) {
        std::lock_guard<std::mutex> g(in_lock);
        if (in_events.size() < 1024)
            in_events.push_back(e);
    }
    if (block) {
        blocked_msgs.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return CallWindowProcW(game_proc.load(), w, msg, wp, lp);
}

void hook_window(HWND wnd)
{
    if (!wnd || game_wnd.load() || killed("input"))
        return;
    WNDPROC prev = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(wnd, GWLP_WNDPROC));
    if (!prev)
        return;
    game_proc.store(prev);
    RECT rc{};
    if (GetClientRect(wnd, &rc))
        client_size.store(uint32_t(rc.right & 0xffff) | uint32_t(rc.bottom & 0xffff) << 16);
    game_wnd.store(wnd);
    SetWindowLongPtrW(wnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&input_proc));
    swofty::log("[overlay] input: window %p subclassed (procedure %p); addons can read the mouse and keyboard and "
                "take input focus",
                static_cast<void *>(wnd), reinterpret_cast<void *>(prev));
}

void input_frame(const std::vector<hud::Paintsub> &subs, float fb_w, float fb_h, ULONGLONG now)
{
    uint32_t cs = client_size.load(std::memory_order_relaxed);
    float sx = 1, sy = 1, cw = float(cs & 0xffff), ch = float(cs >> 16);
    if (cw > 0 && ch > 0) {
        sx = fb_w / cw;
        sy = fb_h / ch;
    }
    std::string expired;
    bool pads_wanted = now - input_read_ms.load(std::memory_order_relaxed) < 2000 && !killed("pad");
    if (pads_wanted)
        poll_pads(now);
    {
        std::lock_guard<std::mutex> g(in_lock);
        hud::input::fold(in_snap, in_events, sx, sy);
        if (pads_wanted)
            hud::input::fold_pads(in_snap, pads, 4, now);
        else
            in_snap.pad_pressed = 0;
        in_events.clear();
        for (const auto &s : subs)
            in_focus.painted(s.owner, now);
        std::string owner = in_focus.owner;
        if (in_focus.expire(now, FOCUS_TIMEOUT_MS))
            expired = owner;
        focus_held.store(in_focus.held());
        frame_snap = in_snap;
        frame_focus = in_focus;
    }
    if (!expired.empty())
        swofty::log("[overlay] input: %s held focus but has not painted for %llu ms; focus released", expired.c_str(),
                    static_cast<unsigned long long>(FOCUS_TIMEOUT_MS));
    static ULONGLONG reported;
    if (now - reported >= 60000) {
        unsigned m = blocked_msgs.exchange(0), r = blocked_raw.exchange(0);
        if (m || r)
            swofty::log("[overlay] input: last %llu s: %u window message(s) and %u raw input message(s) kept from the "
                        "game while an addon held focus",
                        static_cast<unsigned long long>(reported ? (now - reported) / 1000 : 60), m, r);
        reported = now;
    }
}

void draw_cursor(hud::Frame &f)
{
    if (!frame_focus.held() || frame_snap.x < 0)
        return;
    hud::reset_state(f);
    float s = std::max(0.75f, hud::ui_scale(f)), x = frame_snap.x, y = frame_snap.y;
    const float m[6] = {s, 0, 0, s, x, y};
    hud::push_transform(f, m);
    const float tip[] = {0, 0, 0, 17, 12.5f, 12.5f};
    const float tail[] = {3.2f, 13.5f, 6.4f, 12.2f, 10.2f, 20.5f, 7.0f, 21.8f};
    hud::polygon(f, tail, 4, 0xffffffffu);
    hud::polygon(f, tip, 3, 0xffffffffu);
    hud::polyline(f, tip, 3, 1.2f, 0x000000ffu, SWOFTY_POLYLINE_CLOSED);
    hud::pop_transform(f);
}

void paint_frame(IDXGISwapChain3 *sc, ID3D12CommandQueue *q, const std::vector<hud::Paintsub> &subs,
                 const DXGI_SWAP_CHAIN_DESC &desc, ULONGLONG now, UINT color_space);

int game_hdr_mode()
{
    static std::wstring path;
    static FILETIME at{};
    static ULONGLONG checked;
    static int mode = -1;
    ULONGLONG now = GetTickCount64();
    if (checked && now - checked < 2000)
        return mode;
    checked = now;
    if (path.empty())
        path = hardware_settings_path();
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!path.empty() && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) &&
        CompareFileTime(&fa.ftLastWriteTime, &at)) {
        at = fa.ftLastWriteTime;
        mode = hud::guard::hdr_mode(runtime::read_file(path, 1 << 20));
    }
    return mode;
}

float paper_white()
{
    static float nits = [] {
        char v[32] = {};
        DWORD n = GetEnvironmentVariableA("SWOFTY_OVERLAY_NITS", v, sizeof v);
        float f = n && n < sizeof v ? float(atof(v)) : 0;
        return f >= 80 && f <= 1000 ? f : 350.0f;
    }();
    return nits;
}

UINT output_mode(IDXGISwapChain3 *sc, DXGI_FORMAT fmt, UINT color_space)
{
    int game = color_space == hud::guard::COLOR_SPACE_UNSET && fmt == DXGI_FORMAT_R10G10B10A2_UNORM ? game_hdr_mode() : -1;
    auto out = hud::guard::output_for(int(fmt), color_space, game);
    static void *told_sc;
    static unsigned told_out = ~0u;
    if (told_sc != sc || told_out != out) {
        told_sc = sc;
        told_out = out;
        static const char *names[] = {"SDR", "HDR10 (BT.2020 PQ)", "scRGB (linear)"};
        char why[96];
        if (color_space != hud::guard::COLOR_SPACE_UNSET)
            snprintf(why, sizeof why, "colour space %u from SetColorSpace1", color_space);
        else if (game >= 0)
            snprintf(why, sizeof why, "no SetColorSpace1 seen; the game's hdr mode is %d", game);
        else
            snprintf(why, sizeof why, "no SetColorSpace1 seen");
        swofty::log("[overlay] swapchain %p output: %s, paper white %.0f nits (format %d, %s)",
                    static_cast<void *>(sc), names[out], double(paper_white()), int(fmt), why);
    }
    return out;
}

void draw_frame(IDXGISwapChain3 *sc, const d3d12hooks::Swapchain *known, void *)
{
    if (off.load() || renderer.failed)
        return;
    static const uint32_t probe = perf::probe("core", "overlay present");
    perf::Scope timed(probe);
    std::vector<hud::Paintsub> subs;
    if (implemented) {
        std::lock_guard<std::mutex> g(reg_lock);
        subs = reg.subs();
    }
    if (subs.empty() && !cap.on)
        return;
    if (!known) {
        if (std::find(unknown_told.begin(), unknown_told.end(), sc) == unknown_told.end()) {
            unknown_told.push_back(sc);
            swofty::log("[overlay] swapchain %p was created before the core hooked swapchain creation, so its "
                        "present queue is unknown; the overlay does not draw on it",
                        static_cast<void *>(sc));
        }
        return;
    }
    const d3d12hooks::Swapchain &c = *known;
    std::pair<void *, bool> seen{sc, !c.fg.empty()};
    bool fresh = std::find(told.begin(), told.end(), seen) == told.end();
    if (fresh)
        told.push_back(seen);
    if (!c.fg.empty()) {
        if (fresh)
            swofty::log("[overlay] frame generation (%s) owns swapchain %p; the overlay does not draw on it",
                        c.fg.c_str(), static_cast<void *>(sc));
        return;
    }
    if (c.streamline && !frame_gen_off(GetTickCount64())) {
        if (!fg_watch.told || fresh)
            swofty::log("[overlay] swapchain %p was created through Streamline's frame generation hook "
                        "(sl.dlss_g.dll) and frame generation is not off in the game's settings; the overlay does not "
                        "draw on it",
                        static_cast<void *>(sc));
        return;
    }
    if (!renderer.ready) {
        if (!renderer.init(c.queue)) {
            renderer.failed = true;
            renderer.shutdown();
            swofty::log("[overlay] renderer init failed; the overlay stays armed but draws nothing");
            return;
        }
        live_dev.store(renderer.dev);
    }
    if (!renderer.same_device(c.queue)) {
        renderer.failed = true;
        swofty::log("[overlay] swapchain %p presents on queue %p of another device than the overlay renderer's; the "
                    "overlay stops drawing",
                    static_cast<void *>(sc), static_cast<void *>(c.queue));
        return;
    }
    if (renderer.dev->GetDeviceRemovedReason() != S_OK) {
        device_lost(S_OK);
        return;
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc)))
        return;
    if (!(desc.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT)) {
        if (fresh)
            swofty::log("[overlay] swapchain %p back buffers are not render targets (usage 0x%x); the overlay does not "
                        "draw on it",
                        static_cast<void *>(sc), unsigned(desc.BufferUsage));
        return;
    }
    if (fresh && implemented)
        hook_window(desc.OutputWindow);
    if (fresh) {
        drawn_by = c.by;
        swofty::log("[overlay] drawing on swapchain %p with the %s queue %p it was created with (%ux%u, format %d, %u "
                    "buffer(s), created by %s)",
                    static_cast<void *>(sc), "direct", static_cast<void *>(c.queue), desc.BufferDesc.Width,
                    desc.BufferDesc.Height, int(desc.BufferDesc.Format), desc.BufferCount, c.by.c_str());
    }
    ULONGLONG now = GetTickCount64();
    if (!subs.empty() && !killed("paint"))
        paint_frame(sc, c.queue, subs, desc, now, c.color_space_set ? c.color_space : hud::guard::COLOR_SPACE_UNSET);
    if (cap.on && !killed("capture"))
        capture_step(sc, c.queue, desc, now);
}

void paint_frame(IDXGISwapChain3 *sc, ID3D12CommandQueue *q, const std::vector<hud::Paintsub> &subs,
                 const DXGI_SWAP_CHAIN_DESC &desc, ULONGLONG now, UINT color_space)
{
    static const uint32_t paint_probe = perf::probe("core", "overlay paint callbacks");
    static hud::Frame frame;
    {
        perf::Scope painting(paint_probe);
        input_frame(subs, float(desc.BufferDesc.Width), float(desc.BufferDesc.Height), now);
        hud::build(subs, frame, float(desc.BufferDesc.Width), float(desc.BufferDesc.Height), overlay_off());
        draw_cursor(frame);
    }
    const std::vector<Vertex> &verts = frame.verts;
    flush_stats(now);
    if (frame.dropped && !dropped_told) {
        dropped_told = true;
        swofty::log("[overlay] a frame's paint callbacks drew more than %u vertices; %u primitive(s) past that were "
                    "dropped",
                    hud::MAX_VERTS, frame.dropped);
    }
    if (verts.empty())
        return;
    static const uint32_t probe = perf::probe("core", "overlay record");
    perf::Scope timed(probe);
    UINT mode = killed("hdr") ? 0 : output_mode(sc, desc.BufferDesc.Format, color_space);
    switch (renderer.render(sc, q, desc.BufferDesc.Format, desc.BufferDesc.Width, desc.BufferDesc.Height, frame, mode,
                            paper_white())) {
    case Renderer::drawn:
        if (!stats.drawn && !last_draw_ms.load())
            swofty::log("[overlay] first overlay frame submitted: %zu vertices", verts.size());
        last_draw_ms.store(now);
        stats.drawn++;
        break;
    case Renderer::busy:
        stats.busy++;
        break;
    case Renderer::broken:
        renderer.failed = true;
        swofty::log("[overlay] recording the overlay for swapchain %p failed (format %d); the overlay stops drawing",
                    static_cast<void *>(sc), int(desc.BufferDesc.Format));
        break;
    }
}

void presented(IDXGISwapChain3 *, HRESULT hr)
{
    if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) && armed.load())
        device_lost(hr);
}

bool resolve_d3d12()
{
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    d3d12_create_device = d3d12 ? reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
    d3d12_serialize_root =
        d3d12 ? reinterpret_cast<SerializeRootFn>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")) : nullptr;
    dxgi_create_factory = dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
    if (d3d12_create_device && d3d12_serialize_root && dxgi_create_factory)
        return true;
    swofty::log("[overlay] d3d12.dll / dxgi.dll entry points are unavailable; the present hook is not armed");
    return false;
}

bool arm()
{
    if (!resolve_d3d12())
        return false;
    d3d12hooks::on_present(draw_frame);
    d3d12hooks::on_presented(presented);
    return d3d12hooks::arm_swapchain();
}

// ----- SwoftyOverlay table ----------------------------------------------------------------------

int ov_available()
{
    if (!implemented || !armed.load())
        return 0;
    return hud::listed(overlay_off(), owner_of(caller(__builtin_return_address(0)))) ? 0 : 1;
}

int ov_on_paint(SwoftyPaintFn fn, void *user, uint32_t *token)
{
    Caller c = caller(__builtin_return_address(0));
    if (hud::listed(overlay_off(), owner_of(c))) {
        swofty::log("[overlay] api: %s on_paint -> unavailable (turned off for this addon in the launcher)",
                    owner_of(c).c_str());
        return SWOFTY_ASSET_UNAVAILABLE;
    }
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    if (!fn)
        return SWOFTY_ASSET_INVALID;
    uint32_t t;
    {
        std::lock_guard<std::mutex> g(reg_lock);
        t = reg.add(fn, user, owner_of(c));
    }
    if (token)
        *token = t;
    swofty::log("[overlay] api: %s on_paint -> ok (token %u)", c.id.empty() ? c.dll.c_str() : c.id.c_str(), t);
    return SWOFTY_ASSET_OK;
}

int ov_remove(uint32_t token)
{
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    std::lock_guard<std::mutex> g(reg_lock);
    return reg.remove(token) ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

void ov_size(const SwoftyPaint *paint, float *width, float *height)
{
    auto *f = reinterpret_cast<const hud::Frame *>(paint);
    if (width)
        *width = f ? f->width : 0;
    if (height)
        *height = f ? f->height : 0;
}

void ov_rect(SwoftyPaint *paint, float x, float y, float w, float h, uint32_t rgba)
{
    if (paint)
        hud::rect(*reinterpret_cast<hud::Frame *>(paint), x, y, w, h, rgba);
}

void ov_rect_outline(SwoftyPaint *paint, float x, float y, float w, float h, float thickness, uint32_t rgba)
{
    if (paint)
        hud::rect_outline(*reinterpret_cast<hud::Frame *>(paint), x, y, w, h, thickness, rgba);
}

void ov_line(SwoftyPaint *paint, float x0, float y0, float x1, float y1, float thickness, uint32_t rgba)
{
    if (paint)
        hud::line(*reinterpret_cast<hud::Frame *>(paint), x0, y0, x1, y1, thickness, rgba);
}

void ov_text(SwoftyPaint *paint, float x, float y, float size, uint32_t rgba, const char *utf8)
{
    if (paint && utf8)
        hud::text(*reinterpret_cast<hud::Frame *>(paint), x, y, size, rgba, utf8);
}

void ov_measure(const SwoftyPaint *, float size, const char *utf8, float *width, float *height)
{
    hud::measure(size, utf8 ? utf8 : "", width, height);
}

int ov_load_image(const char *id, const char *file)
{
    Caller c = caller(__builtin_return_address(0));
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    const char *who = c.id.empty() ? c.dll.c_str() : c.id.c_str();
    if (!id || !*id)
        return SWOFTY_ASSET_INVALID;
    std::string why, rel = addon_image(c, file, why);
    hud::img::Pixels px;
    if (!rel.empty() && !hud::img::decode(runtime::read_file(runtime::under_swofty(rel), size_t(64) << 20), px, why))
        why = "swofty/" + rel + ": " + why;
    if (rel.empty() || px.rgba.empty()) {
        swofty::log("[overlay] api: %s load_image %s -> invalid: %s", who, id, why.c_str());
        return SWOFTY_ASSET_INVALID;
    }
    uint32_t w = px.width, h = px.height;
    int slot;
    {
        std::lock_guard<std::mutex> g(reg_lock);
        slot = reg.set_image(owner_of(c) + "\n" + id, w, h, std::move(px.rgba));
    }
    if (slot + 1 >= int(TEX_MAX)) {
        swofty::log("[overlay] api: %s load_image %s -> invalid: the overlay holds at most %u images", who, id,
                    TEX_MAX - 1);
        return SWOFTY_ASSET_INVALID;
    }
    swofty::log("[overlay] api: %s load_image %s (swofty/%s, %ux%u) -> ok (slot %d)", who, id, rel.c_str(), w, h, slot);
    return SWOFTY_ASSET_OK;
}

hud::Frame *frame_of(SwoftyPaint *paint)
{
    return reinterpret_cast<hud::Frame *>(paint);
}

struct Found {
    int slot = -1;
    float w = 0, h = 0;
};

Found find_image(const std::string &owner, const char *id)
{
    Found f;
    if (!id || !*id)
        return f;
    static thread_local std::string key;
    key.assign(owner).append(1, '\n').append(id);
    std::lock_guard<std::mutex> g(reg_lock);
    f.slot = reg.find_image(key);
    if (const hud::Image *im = reg.image(f.slot)) {
        f.w = float(im->width);
        f.h = float(im->height);
    }
    return f;
}

int ov_image(SwoftyPaint *paint, const char *id, float x, float y, float w, float h, uint32_t tint)
{
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    Found f = find_image(paint ? frame_of(paint)->owner : owner_of(caller(__builtin_return_address(0))), id);
    if (f.slot < 0)
        return SWOFTY_ASSET_INVALID;
    if (paint)
        hud::image(*frame_of(paint), f.slot, x, y, w, h, tint);
    return SWOFTY_ASSET_OK;
}

void ov_rect_rounded(SwoftyPaint *paint, float x, float y, float w, float h, float radius, uint32_t rgba)
{
    if (paint)
        hud::rect_rounded(*frame_of(paint), x, y, w, h, radius, rgba);
}

void ov_rect_rounded_outline(SwoftyPaint *paint, float x, float y, float w, float h, float radius, float thickness,
                             uint32_t rgba)
{
    if (paint)
        hud::rect_rounded_outline(*frame_of(paint), x, y, w, h, radius, thickness, rgba);
}

void ov_rect_gradient(SwoftyPaint *paint, float x, float y, float w, float h, float radius, uint32_t from, uint32_t to,
                      uint32_t flags)
{
    if (paint)
        hud::rect_gradient(*frame_of(paint), x, y, w, h, radius, from, to, flags);
}

void ov_circle(SwoftyPaint *paint, float cx, float cy, float radius, uint32_t rgba)
{
    if (paint)
        hud::circle(*frame_of(paint), cx, cy, radius, rgba);
}

void ov_circle_outline(SwoftyPaint *paint, float cx, float cy, float radius, float thickness, uint32_t rgba)
{
    if (paint)
        hud::circle_outline(*frame_of(paint), cx, cy, radius, thickness, rgba);
}

void ov_arc(SwoftyPaint *paint, float cx, float cy, float radius, float from, float to, float thickness, uint32_t rgba)
{
    if (paint)
        hud::arc(*frame_of(paint), cx, cy, radius, from, to, thickness, rgba);
}

void ov_polyline(SwoftyPaint *paint, const float *xy, uint32_t count, float thickness, uint32_t rgba, uint32_t flags)
{
    if (paint)
        hud::polyline(*frame_of(paint), xy, std::min<uint32_t>(count, hud::MAX_VERTS / 18), thickness, rgba, flags);
}

void ov_polygon(SwoftyPaint *paint, const float *xy, uint32_t count, uint32_t rgba)
{
    if (paint)
        hud::polygon(*frame_of(paint), xy, std::min<uint32_t>(count, hud::MAX_VERTS / 9), rgba);
}

hud::TextStyle text_style(const SwoftyTextStyle *s)
{
    hud::TextStyle t;
    if (!s || s->size < offsetof(SwoftyTextStyle, px) + sizeof(float)) {
        t.px = 0;
        return t;
    }
    auto has = [&](size_t end) { return s->size >= end; };
    t.align = s->align;
    t.px = s->px;
    if (has(offsetof(SwoftyTextStyle, rgba) + 4))
        t.rgba = s->rgba;
    if (has(offsetof(SwoftyTextStyle, tracking) + 4))
        t.tracking = s->tracking;
    if (has(offsetof(SwoftyTextStyle, line_height) + 4))
        t.line_height = s->line_height;
    if (has(offsetof(SwoftyTextStyle, outline_px) + 4)) {
        t.outline_rgba = s->outline_rgba;
        t.outline_px = s->outline_px;
    }
    if (has(offsetof(SwoftyTextStyle, shadow_blur) + 4)) {
        t.shadow_rgba = s->shadow_rgba;
        t.shadow_dx = s->shadow_dx;
        t.shadow_dy = s->shadow_dy;
        t.shadow_blur = s->shadow_blur;
    }
    if (has(offsetof(SwoftyTextStyle, max_width) + 4))
        t.max_width = s->max_width;
    return t;
}

void ov_text_styled(SwoftyPaint *paint, float x, float y, const SwoftyTextStyle *style, const char *utf8)
{
    if (paint && utf8)
        hud::text(*frame_of(paint), x, y, text_style(style), utf8);
}

void ov_measure_styled(const SwoftyPaint *, const SwoftyTextStyle *style, const char *utf8, float *width, float *height)
{
    hud::measure(text_style(style), utf8 ? utf8 : "", width, height);
}

int ov_image_uv(SwoftyPaint *paint, const char *id, float x, float y, float w, float h, float u0, float v0, float u1,
                float v1, uint32_t tint)
{
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    Found f = find_image(paint ? frame_of(paint)->owner : owner_of(caller(__builtin_return_address(0))), id);
    if (f.slot < 0)
        return SWOFTY_ASSET_INVALID;
    if (paint)
        hud::image_uv(*frame_of(paint), f.slot, x, y, w, h, u0, v0, u1, v1, tint);
    return SWOFTY_ASSET_OK;
}

int ov_image_nine(SwoftyPaint *paint, const char *id, float x, float y, float w, float h, float left, float top,
                  float right, float bottom, float scale, uint32_t tint)
{
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    Found f = find_image(paint ? frame_of(paint)->owner : owner_of(caller(__builtin_return_address(0))), id);
    if (f.slot < 0)
        return SWOFTY_ASSET_INVALID;
    if (paint)
        hud::image_nine(*frame_of(paint), f.slot, f.w, f.h, x, y, w, h, left, top, right, bottom, scale, tint);
    return SWOFTY_ASSET_OK;
}

int ov_image_size(const char *id, float *width, float *height)
{
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    Found f = find_image(owner_of(caller(__builtin_return_address(0))), id);
    if (width)
        *width = f.w;
    if (height)
        *height = f.h;
    return f.slot < 0 ? SWOFTY_ASSET_INVALID : SWOFTY_ASSET_OK;
}

void ov_push_clip(SwoftyPaint *paint, float x, float y, float w, float h)
{
    if (paint)
        hud::push_clip(*frame_of(paint), x, y, w, h);
}

void ov_pop_clip(SwoftyPaint *paint)
{
    if (paint)
        hud::pop_clip(*frame_of(paint));
}

void ov_push_transform(SwoftyPaint *paint, const float *m)
{
    if (paint)
        hud::push_transform(*frame_of(paint), m);
}

void ov_pop_transform(SwoftyPaint *paint)
{
    if (paint)
        hud::pop_transform(*frame_of(paint));
}

void ov_push_opacity(SwoftyPaint *paint, float opacity)
{
    if (paint)
        hud::push_opacity(*frame_of(paint), opacity);
}

void ov_pop_opacity(SwoftyPaint *paint)
{
    if (paint)
        hud::pop_opacity(*frame_of(paint));
}

void ov_safe_area(const SwoftyPaint *paint, float *x, float *y, float *w, float *h)
{
    hud::Frame empty;
    hud::safe_area(paint ? *reinterpret_cast<const hud::Frame *>(paint) : empty, x, y, w, h);
}

float ov_ui_scale(const SwoftyPaint *paint)
{
    return paint ? hud::ui_scale(*reinterpret_cast<const hud::Frame *>(paint)) : 1.0f;
}

int ov_input(const SwoftyPaint *paint, SwoftyInput *out)
{
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    if (!paint || !out || out->size < offsetof(SwoftyInput, x))
        return SWOFTY_ASSET_INVALID;
    input_read_ms.store(GetTickCount64(), std::memory_order_relaxed);
    SwoftyInput full{};
    full.size = sizeof full;
    hud::input::view(frame_snap, frame_focus, reinterpret_cast<const hud::Frame *>(paint)->owner, full);
    uint32_t size = out->size;
    memcpy(out, &full, std::min<size_t>(size, sizeof full));
    out->size = size;
    return SWOFTY_ASSET_OK;
}

int ov_focus(int on)
{
    Caller c = caller(__builtin_return_address(0));
    if (!implemented || !gate_on())
        return SWOFTY_ASSET_UNAVAILABLE;
    std::string who = owner_of(c), before;
    bool changed;
    {
        std::lock_guard<std::mutex> g(in_lock);
        before = in_focus.owner;
        changed = in_focus.take(who, on != 0, GetTickCount64());
        focus_held.store(in_focus.held());
    }
    if (changed && before != (on ? who : std::string()))
        swofty::log("[overlay] api: %s focus %s -> ok%s%s", who.c_str(), on ? "on" : "off",
                    on && !before.empty() && before != who ? " (taken from " : "",
                    on && !before.empty() && before != who ? (before + ")").c_str() : "");
    if (!game_wnd.load())
        return SWOFTY_ASSET_LATE;
    return changed ? SWOFTY_ASSET_OK : SWOFTY_ASSET_INVALID;
}

const SwoftyOverlay table = {ov_available,
                             ov_on_paint,
                             ov_remove,
                             ov_size,
                             ov_rect,
                             ov_rect_outline,
                             ov_line,
                             ov_text,
                             ov_measure,
                             ov_load_image,
                             ov_image,
                             ov_rect_rounded,
                             ov_rect_rounded_outline,
                             ov_rect_gradient,
                             ov_circle,
                             ov_circle_outline,
                             ov_arc,
                             ov_polyline,
                             ov_polygon,
                             ov_text_styled,
                             ov_measure_styled,
                             ov_image_uv,
                             ov_image_nine,
                             ov_image_size,
                             ov_push_clip,
                             ov_pop_clip,
                             ov_push_transform,
                             ov_pop_transform,
                             ov_push_opacity,
                             ov_pop_opacity,
                             ov_safe_area,
                             ov_ui_scale,
                             ov_input,
                             ov_focus};

void setup()
{
    if (killed("all")) {
        swofty::log("[overlay] off: SWOFTY_OVERLAY_KILL or \"overlay_kill\" in swofty/assets.json lists all");
        return;
    }
    capture_setup();
    bool on = gate_on();
    if (!on && !cap.on) {
        swofty::log("[overlay] the overlay feature is experimental and off; the HUD present hook is not armed");
        return;
    }
    if (!on)
        swofty::log("[overlay] the overlay is off; the present hook arms for SWOFTY_FRAME_CAPTURE only");
    implemented = on;
    if (on) {
        renderer.font_tried = true;
        if (!hud::img::grey(hud::font::ATLAS_PNG, hud::font::ATLAS_PNG_SIZE, renderer.font_px))
            swofty::log("[overlay] decoding the built-in font atlas failed; text draws nothing");
    }
    if (arm())
        armed.store(true);
    else
        swofty::log("[overlay] the present hook did not arm; on_paint callbacks will not be drawn");
}

}

extern "C" __declspec(dllexport) uint32_t SwoftyCoreOverlayVersion(void)
{
    return SWOFTY_OVERLAY_VERSION;
}

namespace runtime {

const SwoftyOverlay *overlay_table()
{
    return &table;
}

void hud_setup()
{
    setup();
}

void hud_fatal()
{
    ID3D12Device *dev = live_dev.load();
    if (dev && !lost_once.load() && dev->GetDeviceRemovedReason() != S_OK)
        device_lost(S_OK);
}

}
