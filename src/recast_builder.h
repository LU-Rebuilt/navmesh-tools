#pragma once
// recast_builder.h — Build a tiled Detour navmesh from zone geometry using Recast.

#include "zone_geometry.h"

#include <cstdint>

struct dtNavMesh;
struct rcConfig;

namespace navmesh {

// Recast build configuration — LU defaults based on DarkflameServer agent profile.
struct BuildConfig {
    float cell_size       = 0.3f;   // Voxel grid XZ resolution
    float cell_height     = 0.2f;   // Voxel Y resolution
    float agent_height    = 2.0f;   // Minimum ceiling clearance
    float agent_radius    = 0.6f;   // Agent collision radius
    float agent_max_climb = 0.9f;   // Max step height
    float agent_max_slope = 45.0f;  // Max walkable slope (degrees)
    int tile_size         = 32;     // Tile width in cells
    float edge_max_len    = 12.0f;  // Max edge length (in cells)
    float edge_max_error  = 1.3f;   // Edge simplification error
    int region_min_size   = 8;      // Min region area (cells²)
    int region_merge_size = 20;     // Merge regions smaller than this
    float detail_sample_dist     = 6.0f;
    float detail_sample_max_error = 1.0f;
    int max_verts_per_poly = 6;
};

// Build a tiled Detour navmesh from zone geometry.
// Returns a fully built navmesh. Caller owns it (free with dtFreeNavMesh).
dtNavMesh* build_navmesh(const ZoneGeometry& geo, const BuildConfig& cfg);

// Get navmesh statistics
struct NavmeshStats {
    int total_tiles = 0;
    int total_polys = 0;
    int total_verts = 0;
    float coverage_area = 0; // approximate walkable area in m²
};

NavmeshStats get_navmesh_stats(const dtNavMesh* mesh);

} // namespace navmesh
