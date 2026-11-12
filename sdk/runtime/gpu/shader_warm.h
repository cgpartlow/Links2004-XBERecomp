#pragma once
// Shaders ahead of their first use. A shader is generated from what the title draws, and compiling one takes tens of
// milliseconds, so a scene's first frame used to stall while its shaders compiled. Two files beside the exe end that:
// - shader_cache\<hash>.bin: each compiled shader, so a shader compiles once on a PC (LoadCached, StoreCached);
// - shaders.list: a recipe for every shader a play session needed, added to as new ones turn up (the Record calls). A
//   recipe is what the shader was generated from, not the shader: a pixel shader's key, a fixed function vertex
//   shader's key, and for one of the title's own vertex programs only the program's hash and how it was used. At
//   start (Start) worker threads generate and compile the listed shaders the cache lacks, in the background while the
//   title boots; a vertex program's are made once the title has loaded that program (NoteProgram), from the player's
//   own copy. The list can be shared with a release: it holds none of the title's code.
#include <cstdint>
#include <string>
#include <vector>

#include <d3dcommon.h>
#include <wrl/client.h>

#include "gpu/shaders.h"

namespace gpu {

// a point sprite's program hands its point size to SpriteHlsl in tc3, whose coordinates the sprite makes
std::string SpriteSizeHlsl(std::string s);

namespace warm {

// a shader's cache name from its source, entry point and profile; check is a second hash of the source
uint64_t ShaderHash(const std::string& src, const char* entry, const char* profile, uint64_t& check);
bool LoadCached(uint64_t hash, uint64_t check, size_t source_size, Microsoft::WRL::ComPtr<ID3DBlob>& code);
void StoreCached(uint64_t hash, uint64_t check, size_t source_size, ID3DBlob* code);

// reads shaders.list and starts compiling what the cache lacks; once, when the renderer starts
void Start();
// the title made or selected a vertex program: its listed shaders can be compiled now
void NoteProgram(const std::vector<uint32_t>& program);

// a shader the title just needed (its cache name and what it was generated from), added to the list when new
void RecordPixel(uint64_t shader, const PixelKey& key);
void RecordProgram(uint64_t shader, const std::vector<uint32_t>& program, uint16_t regs, uint64_t decode, bool sprite,
                   bool quad);
void RecordFixed(uint64_t shader, uint16_t regs, bool pretransformed, uint64_t decode, const FixedKey& key, bool quad);

}  // namespace warm
}  // namespace gpu
