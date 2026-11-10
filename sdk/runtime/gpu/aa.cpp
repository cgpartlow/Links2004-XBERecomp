// FXAA and SMAA over the finished frame (gpu/aa.h)
//
// Both run at the frame's own size (the render scale's), so they smooth the edges the game drew rather than the
// window's scaled pixels; the final blit then scales the result as it scaled the frame. The passes go through the
// renderer's Pass (a full screen triangle; b1 the constants, t0 to t2 the images, s0 a linear and s1 a point sampler,
// both clamped), into textures kept here, with views from a small heap of their own.
//
// FXAA: a version of Timothy Lottes' FXAA 3.11 (quality), written for this file: the local contrast finds an edge,
// the walk along it finds its ends, and the pixel is moved across the edge by how near it is to the nearer end.
// SMAA: Jimenez et al.'s SMAA 1x at its HIGH preset, from its own source (gpu/smaa/SMAA_hlsl.h) and lookup textures
// (gpu/smaa/AreaTex.h, SearchTex.h): luma edges, then the blending weights the edge shapes give, then the blend.
#include "gpu/aa.h"

#include <cstring>
#include <string>

#include "core/log.h"
#include "gpu/smaa/AreaTex.h"
#include "gpu/smaa/SMAA_hlsl.h"
#include "gpu/smaa/SearchTex.h"

namespace gpu {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT kOutFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr DXGI_FORMAT kEdgesFormat = DXGI_FORMAT_R8G8_UNORM;
constexpr DXGI_FORMAT kWeightsFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kAreaFormat = DXGI_FORMAT_R8G8_UNORM;
constexpr DXGI_FORMAT kSearchFormat = DXGI_FORMAT_R8_UNORM;
constexpr uint32_t kSearchWidth = 64, kSearchHeight = 16;  // SearchTex.h's, packed as SMAA_SEARCHTEX_PACKED_SIZE
constexpr uint32_t kRtvEdges = 0, kRtvWeights = 1, kRtvOut = 2, kRtvCount = 3;

// the frame's size in b1: one over the width and height, then the width and height (SMAA_RT_METRICS)
const char* kFxaaHlsl = R"(
cbuffer P : register(b1) { float4 rt; float4 unused; };
Texture2D img : register(t0);
SamplerState ls : register(s0);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
static const float3 kLuma = float3(0.299, 0.587, 0.114);
float Luma(float2 uv) { return dot(img.SampleLevel(ls, uv, 0).rgb, kLuma); }
float LumaAt(float2 uv, int2 d) { return dot(img.SampleLevel(ls, uv, 0, d).rgb, kLuma); }
// how far each step of the walk along an edge goes, in pixels, and the guess past the last
static const float kSteps[12] = {1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0};
float4 fxaa(O i) : SV_Target {
    float2 uv = i.uv;
    float4 c = img.SampleLevel(ls, uv, 0);
    float m = dot(c.rgb, kLuma);
    float n = LumaAt(uv, int2(0, -1)), s = LumaAt(uv, int2(0, 1));
    float e = LumaAt(uv, int2(1, 0)), w = LumaAt(uv, int2(-1, 0));
    float hi = max(max(max(n, s), max(e, w)), m);
    float lo = min(min(min(n, s), min(e, w)), m);
    float range = hi - lo;
    // too little contrast for an edge: FXAA's thresholds at its high quality
    [branch] if (range < max(0.0312, hi * 0.125)) return float4(c.rgb, 1);
    float ne = LumaAt(uv, int2(1, -1)), nw = LumaAt(uv, int2(-1, -1));
    float se = LumaAt(uv, int2(1, 1)), sw = LumaAt(uv, int2(-1, 1));
    // the sub-pixel blend: how far the pixel stands out from its neighbours
    float average = (2 * (n + s + e + w) + ne + nw + se + sw) / 12;
    float sub = smoothstep(0, 1, saturate(abs(average - m) / range));
    sub = sub * sub * 0.75;
    // the edge runs along the axis with the least change
    float horizontal = 2 * abs(n + s - 2 * m) + abs(ne + se - 2 * e) + abs(nw + sw - 2 * w);
    float vertical = 2 * abs(e + w - 2 * m) + abs(ne + nw - 2 * n) + abs(se + sw - 2 * s);
    bool h = horizontal >= vertical;
    float2 across = h ? float2(0, rt.y) : float2(rt.x, 0);
    float2 along = h ? float2(rt.x, 0) : float2(0, rt.y);
    float before = h ? n : w, after = h ? s : e;
    float g_before = abs(before - m), g_after = abs(after - m);
    float other = after, gradient = g_after;
    if (g_before >= g_after) {
        across = -across;
        other = before;
        gradient = g_before;
    }
    // walk both ways along the edge, half a pixel over, until the luma leaves the edge's
    float edge = (m + other) * 0.5;
    float threshold = gradient * 0.25;
    float2 start = uv + across * 0.5;
    float2 p1 = start - along * kSteps[0], p2 = start + along * kSteps[0];
    float d1 = Luma(p1) - edge, d2 = Luma(p2) - edge;
    bool end1 = abs(d1) >= threshold, end2 = abs(d2) >= threshold;
    [loop] for (int k = 1; k < 12 && !(end1 && end2); k++) {
        if (!end1) {
            p1 -= along * kSteps[k];
            d1 = Luma(p1) - edge;
            end1 = abs(d1) >= threshold;
        }
        if (!end2) {
            p2 += along * kSteps[k];
            d2 = Luma(p2) - edge;
            end2 = abs(d2) >= threshold;
        }
    }
    float dist1 = h ? uv.x - p1.x : uv.y - p1.y;
    float dist2 = h ? p2.x - uv.x : p2.y - uv.y;
    bool nearer1 = dist1 <= dist2;
    float dist = min(dist1, dist2);
    float d = nearer1 ? d1 : d2;
    // only toward an end whose luma went the other way from this pixel's
    float blend = ((d < 0) != (m < edge)) ? 0.5 - dist / (dist1 + dist2) : 0;
    blend = max(blend, sub);
    return float4(img.SampleLevel(ls, uv + across * blend, 0).rgb, 1);
}
)";

// SMAA's source for this renderer: its porting functions (SMAA_CUSTOM_SL) on this renderer's samplers, the metrics
// from b1, the HIGH preset; then the source, then one pass's entry points
const char* kSmaaHead = R"(
cbuffer P : register(b1) { float4 rt; float4 unused; };
#define SMAA_RT_METRICS rt
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL 1
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAATexture2DMS2(tex) Texture2DMS<float4, 2> tex
#define SMAALoad(tex, pos, sample) tex.Load(pos, sample)
float2 FullScreen(uint id, out float4 p) {
    float2 uv = float2((id << 1) & 2, id & 2);
    p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return uv;
}
)";

const char* kSmaaEdgesPass = R"(
Texture2D colorTex : register(t0);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; float4 off[3] : TEXCOORD1; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = FullScreen(id, o.p);
    SMAAEdgeDetectionVS(o.uv, o.off);
    return o;
}
float4 edges(O i) : SV_Target { return float4(SMAALumaEdgeDetectionPS(i.uv, i.off, colorTex), 0, 0); }
)";

const char* kSmaaWeightsPass = R"(
Texture2D edgesTex : register(t0);
Texture2D areaTex : register(t1);
Texture2D searchTex : register(t2);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; float2 pix : TEXCOORD1; float4 off[3] : TEXCOORD2; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = FullScreen(id, o.p);
    SMAABlendingWeightCalculationVS(o.uv, o.pix, o.off);
    return o;
}
float4 weights(O i) : SV_Target {
    return SMAABlendingWeightCalculationPS(i.uv, i.pix, i.off, edgesTex, areaTex, searchTex, float4(0, 0, 0, 0));
}
)";

const char* kSmaaBlendPass = R"(
Texture2D colorTex : register(t0);
Texture2D blendTex : register(t1);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; float4 off : TEXCOORD1; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = FullScreen(id, o.p);
    SMAANeighborhoodBlendingVS(o.uv, o.off);
    return o;
}
float4 blend(O i) : SV_Target { return float4(SMAANeighborhoodBlendingPS(i.uv, i.off, colorTex, blendTex).rgb, 1); }
)";

// one SMAA pass's whole source, made once
const std::string& SmaaSource(const char* pass) {
    static std::string sources[3];
    int i = pass == kSmaaEdgesPass ? 0 : pass == kSmaaWeightsPass ? 1 : 2;
    if (sources[i].empty()) {
        sources[i] = kSmaaHead;
        for (const char* piece : smaa::kSource) sources[i] += piece;
        sources[i] += pass;
    }
    return sources[i];
}

D3D12_RESOURCE_DESC Texture(DXGI_FORMAT fmt, uint32_t w, uint32_t h, bool target) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Flags = target ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    return d;
}

const char* ModeName(uint32_t mode) {
    return mode == uint32_t(AntiAliasing::Fxaa) ? "FXAA" : mode == uint32_t(AntiAliasing::Smaa) ? "SMAA" : "off";
}

}  // namespace

D3D12_CPU_DESCRIPTOR_HANDLE PostAA::Rtv(uint32_t i) const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += size_t(i) * rtv_inc_;
    return h;
}

// edges_, weights_ and out_ at w x h, made again when the frame's size changes; the old ones go once the GPU is done
bool PostAA::EnsureTargets(Context& c, uint32_t w, uint32_t h) {
    ID3D12Device* dev = c.Device();
    if (!rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = kRtvCount;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap_)))) return false;
        rtv_inc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    if (w == w_ && h == h_ && out_.res) return true;
    Image* images[kRtvCount] = {&edges_, &weights_, &out_};
    const DXGI_FORMAT formats[kRtvCount] = {kEdgesFormat, kWeightsFormat, kOutFormat};
    for (uint32_t i = 0; i < kRtvCount; i++) {
        if (images[i]->res) c.Release(std::move(images[i]->res));
        images[i]->res.Reset();
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC td = Texture(formats[i], w, h, true);
        D3D12_CLEAR_VALUE cv{};
        cv.Format = formats[i];
        images[i]->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, images[i]->state, &cv,
                                                IID_PPV_ARGS(&images[i]->res)))) {
            w_ = h_ = 0;
            return false;
        }
        dev->CreateRenderTargetView(images[i]->res.Get(), nullptr, Rtv(i));
    }
    w_ = w;
    h_ = h;
    return true;
}

// SMAA's area and search textures, copied in by the frame's command list
bool PostAA::EnsureLookups(Context& c) {
    if (area_.res) return true;
    if (lookups_failed_) return false;
    lookups_failed_ = true;
    ID3D12Device* dev = c.Device();
    struct Lookup {
        Image* image;
        DXGI_FORMAT fmt;
        uint32_t w, h, bpp;
        const unsigned char* bytes;
    };
    const Lookup lookups[2] = {{&area_, kAreaFormat, AREATEX_WIDTH, AREATEX_HEIGHT, 2, areaTexBytes},
                               {&search_, kSearchFormat, kSearchWidth, kSearchHeight, 1, searchTexBytes}};
    static_assert(sizeof(searchTexBytes) == kSearchWidth * kSearchHeight, "SearchTex.h is 64 x 16, one byte each");
    static_assert(sizeof(areaTexBytes) == AREATEX_SIZE, "AreaTex.h is 160 x 560, two bytes each");
    // each image's rows at the copy's pitch, the second image at the copy's placement alignment
    uint64_t offsets[2], pitches[2], total = 0;
    for (int i = 0; i < 2; i++) {
        pitches[i] = (uint64_t(lookups[i].w) * lookups[i].bpp + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                     ~uint64_t(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        offsets[i] = total;
        total += (pitches[i] * lookups[i].h + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) &
                 ~uint64_t(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    }
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&upload_)))) {
        return false;
    }
    uint8_t* map = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(upload_->Map(0, &none, reinterpret_cast<void**>(&map)))) return false;
    for (int i = 0; i < 2; i++) {
        const Lookup& l = lookups[i];
        uint32_t row = l.w * l.bpp;
        for (uint32_t y = 0; y < l.h; y++) std::memcpy(map + offsets[i] + pitches[i] * y, l.bytes + size_t(row) * y, row);
    }
    upload_->Unmap(0, nullptr);
    ID3D12GraphicsCommandList* cl = c.List();
    for (int i = 0; i < 2; i++) {
        const Lookup& l = lookups[i];
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC td = Texture(l.fmt, l.w, l.h, false);
        l.image->state = D3D12_RESOURCE_STATE_COPY_DEST;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, l.image->state, nullptr,
                                                IID_PPV_ARGS(&l.image->res)))) {
            area_.res.Reset();
            search_.res.Reset();
            return false;
        }
        D3D12_TEXTURE_COPY_LOCATION dst{l.image->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        D3D12_TEXTURE_COPY_LOCATION src{upload_.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        src.PlacedFootprint.Offset = offsets[i];
        src.PlacedFootprint.Footprint = {l.fmt, l.w, l.h, 1, uint32_t(pitches[i])};
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        c.Transition(l.image->res.Get(), l.image->state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    lookups_failed_ = false;
    return true;
}

ID3D12Resource* PostAA::Fxaa(Context& c, ID3D12Resource* src, DXGI_FORMAT fmt, uint32_t w, uint32_t h) {
    float p[8] = {1.0f / float(w), 1.0f / float(h), float(w), float(h)};
    ID3D12Resource* in[3] = {src, nullptr, nullptr};
    DXGI_FORMAT in_fmt[3] = {fmt, kOutFormat, kOutFormat};
    c.Transition(out_.res.Get(), out_.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    bool ok = c.Pass(kFxaaHlsl, "fxaa", kOutFormat, Rtv(kRtvOut), w, h, in, in_fmt, p, -1);
    c.Transition(out_.res.Get(), out_.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!ok) {
        fxaa_failed_ = true;
        LOGE("aa: the FXAA pass could not be made, the frame goes without");
        return src;
    }
    return out_.res.Get();
}

ID3D12Resource* PostAA::Smaa(Context& c, ID3D12Resource* src, DXGI_FORMAT fmt, uint32_t w, uint32_t h) {
    if (!EnsureLookups(c)) {
        smaa_failed_ = true;
        LOGE("aa: no SMAA lookup textures, the frame goes without");
        return src;
    }
    ID3D12GraphicsCommandList* cl = c.List();
    float p[8] = {1.0f / float(w), 1.0f / float(h), float(w), float(h)};
    const float zero[4] = {0, 0, 0, 0};
    bool ok = true;
    // 1: edges, where the luma changes enough; the rest stay as cleared
    {
        ID3D12Resource* in[3] = {src, nullptr, nullptr};
        DXGI_FORMAT in_fmt[3] = {fmt, kOutFormat, kOutFormat};
        c.Transition(edges_.res.Get(), edges_.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cl->ClearRenderTargetView(Rtv(kRtvEdges), zero, 0, nullptr);
        ok = c.Pass(SmaaSource(kSmaaEdgesPass).c_str(), "edges", kEdgesFormat, Rtv(kRtvEdges), w, h, in, in_fmt, p, -1);
        c.Transition(edges_.res.Get(), edges_.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    // 2: how much each pixel blends with its neighbours, from the shapes the edges make
    if (ok) {
        ID3D12Resource* in[3] = {edges_.res.Get(), area_.res.Get(), search_.res.Get()};
        DXGI_FORMAT in_fmt[3] = {kEdgesFormat, kAreaFormat, kSearchFormat};
        c.Transition(weights_.res.Get(), weights_.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cl->ClearRenderTargetView(Rtv(kRtvWeights), zero, 0, nullptr);
        ok = c.Pass(SmaaSource(kSmaaWeightsPass).c_str(), "weights", kWeightsFormat, Rtv(kRtvWeights), w, h, in,
                    in_fmt, p, -1);
        c.Transition(weights_.res.Get(), weights_.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    // 3: the blend
    if (ok) {
        ID3D12Resource* in[3] = {src, weights_.res.Get(), nullptr};
        DXGI_FORMAT in_fmt[3] = {fmt, kWeightsFormat, kOutFormat};
        c.Transition(out_.res.Get(), out_.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ok = c.Pass(SmaaSource(kSmaaBlendPass).c_str(), "blend", kOutFormat, Rtv(kRtvOut), w, h, in, in_fmt, p, -1);
        c.Transition(out_.res.Get(), out_.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    if (!ok) {
        smaa_failed_ = true;
        LOGE("aa: an SMAA pass could not be made, the frame goes without");
        return src;
    }
    return out_.res.Get();
}

ID3D12Resource* PostAA::Apply(Context& c, ID3D12Resource* src, DXGI_FORMAT& fmt, uint32_t w, uint32_t h,
                              uint32_t mode) {
    if (mode != logged_mode_) {
        logged_mode_ = mode;
        LOGR("aa: anti-aliasing %s", ModeName(mode));
    }
    bool fxaa = mode == uint32_t(AntiAliasing::Fxaa) && !fxaa_failed_;
    bool smaa = mode == uint32_t(AntiAliasing::Smaa) && !smaa_failed_;
    if ((!fxaa && !smaa) || !src || w < 16 || h < 16) return src;
    if (!EnsureTargets(c, w, h)) {
        LOG_FIRST(1, "aa: no %ux%u targets for anti-aliasing, the frame goes without", w, h);
        return src;
    }
    ID3D12Resource* out = fxaa ? Fxaa(c, src, fmt, w, h) : Smaa(c, src, fmt, w, h);
    if (out != src) fmt = kOutFormat;
    return out;
}

}  // namespace gpu
