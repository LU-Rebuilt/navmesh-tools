#pragma once
// mset_writer.h — Read and write DarkflameServer MSET navmesh binary files.
//
// Format: MSET header + tiled Detour navmesh data.
// Compatible with DarkflameServer's dNavMesh.cpp loader.

#include <cstdint>
#include <filesystem>
#include <vector>

struct dtNavMesh;

namespace navmesh {

// MSET file header (matches DarkflameServer NavMeshSetHeader)
constexpr int NAVMESHSET_MAGIC = 0x4D534554; // 'MSET'
constexpr int NAVMESHSET_VERSION = 1;

struct NavMeshSetHeader {
    int magic = NAVMESHSET_MAGIC;
    int version = NAVMESHSET_VERSION;
    int num_tiles = 0;
    // dtNavMeshParams follows (filled by Detour)
};

// Write a Detour navmesh to an MSET .bin file.
// The navmesh must be fully built (all tiles added).
void write_mset(const dtNavMesh* navmesh, const std::filesystem::path& output_path);

// Read an MSET .bin file and create a Detour navmesh.
// Returns nullptr on failure. Caller owns the returned navmesh (free with dtFreeNavMesh).
dtNavMesh* read_mset(const std::filesystem::path& input_path);

// Read an MSET file and return raw tile data for inspection/editing.
struct MsetTile {
    uint64_t tile_ref = 0;
    std::vector<uint8_t> data;
};

struct MsetFile {
    NavMeshSetHeader header;
    std::vector<MsetTile> tiles;
};

MsetFile read_mset_raw(const std::filesystem::path& input_path);

} // namespace navmesh
