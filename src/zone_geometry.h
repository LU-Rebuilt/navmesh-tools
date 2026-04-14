#pragma once
// zone_geometry.h — Collect all geometry for a zone into a merged triangle mesh.
//
// Pipeline: LUZ → LVL scenes → per-object CDClient lookup → HKX (preferred) / NIF fallback.
// Also loads terrain heightmap from the .raw file referenced by the LUZ.
// Tracks per-object geometry for selection/deletion in the editor.

#include "cdclient_db.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace navmesh {

struct NavVertex {
    float x, y, z;
};

struct NavTriangle {
    int a, b, c;
};

// Per-object geometry chunk — tracks which triangles belong to which object.
struct ObjectGeo {
    uint32_t lot = 0;
    float pos_x = 0, pos_y = 0, pos_z = 0;
    float scale = 1.0f;
    std::string source;      // "terrain", "hkx", "nif", "primitive"
    std::string asset_path;  // filename of asset loaded (empty for terrain/primitive)
    std::string label;       // display label (e.g. "LOT 1234 — HKX")

    // Geometry owned by this object (self-contained, world space)
    std::vector<NavVertex> vertices;
    std::vector<NavTriangle> triangles;
};

struct ZoneGeometry {
    // Per-object geometry (terrain is objects[0] if present)
    std::vector<ObjectGeo> objects;

    // Stats
    int terrain_tris = 0;
    int hkx_tris = 0;
    int nif_tris = 0;
    int prim_tris = 0;
    int total_objects = 0;
    int objects_with_geo = 0;

    // Merge all object geometry into flat vertex/triangle arrays for Recast.
    // Call this after loading or after deleting objects.
    void merge(std::vector<NavVertex>& out_verts,
               std::vector<NavTriangle>& out_tris,
               float* bmin, float* bmax) const;
};

// Load all geometry for a zone using the full pipeline:
// LUZ → terrain + LVL scenes → CDClient LOT resolution → HKX / NIF loading.
ZoneGeometry load_zone_geometry(const CdClientDb& cdclient, uint32_t zone_id);

// Load just terrain heightmap triangles from a .raw file.
// Returns an ObjectGeo for the terrain.
ObjectGeo load_terrain(const std::filesystem::path& raw_path);

// Load HKX collision shapes from a single .hkx file.
// Returns an ObjectGeo with the collision triangles.
ObjectGeo load_hkx_collision(const std::filesystem::path& hkx_path,
                              float pos_x = 0, float pos_y = 0, float pos_z = 0,
                              float scale = 1.0f);

// Load NIF mesh geometry from a single .nif file.
ObjectGeo load_nif_mesh(const std::filesystem::path& nif_path,
                         float pos_x = 0, float pos_y = 0, float pos_z = 0,
                         float scale = 1.0f);

} // namespace navmesh
