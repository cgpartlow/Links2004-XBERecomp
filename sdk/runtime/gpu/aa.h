#pragma once
// Anti-aliasing over the finished frame, at the size it was rendered, before it is scaled to the window: FXAA (one
// pass) or SMAA 1x (three: edges, blending weights, blending). The console drew without any, the game's own frames
// and these passes only meet in the last blit.
#include <cstdint>

#include "gpu/ext.h"

namespace gpu {

enum class AntiAliasing : uint32_t { Off = 0, Fxaa = 1, Smaa = 2 };

class PostAA {
public:
    // src (w x h, a shader resource) anti-aliased into a texture of the same size kept here, which is returned as a
    // shader resource with its format in fmt; src itself when the mode is off or its passes could not be made
    ID3D12Resource* Apply(Context& c, ID3D12Resource* src, DXGI_FORMAT& fmt, uint32_t w, uint32_t h, uint32_t mode);

private:
    struct Image {
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    };
    bool EnsureTargets(Context& c, uint32_t w, uint32_t h);
    bool EnsureLookups(Context& c);
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(uint32_t i) const;
    ID3D12Resource* Fxaa(Context& c, ID3D12Resource* src, DXGI_FORMAT fmt, uint32_t w, uint32_t h);
    ID3D12Resource* Smaa(Context& c, ID3D12Resource* src, DXGI_FORMAT fmt, uint32_t w, uint32_t h);

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;  // views of edges_, weights_ and out_, in that order
    uint32_t rtv_inc_ = 0;
    uint32_t w_ = 0, h_ = 0;
    Image edges_, weights_, out_;          // at the frame's size
    Image area_, search_;                  // SMAA's lookup textures
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_;  // their bytes, kept for the copy the frame's list records
    bool fxaa_failed_ = false, smaa_failed_ = false, lookups_failed_ = false;
    uint32_t logged_mode_ = ~0u;
};

}  // namespace gpu
