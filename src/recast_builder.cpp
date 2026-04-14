#include "recast_builder.h"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>

#include <cstring>
#include <cmath>
#include <iostream>
#include <vector>

namespace navmesh {

// Build a single tile at grid position (tx, tz).
static bool build_tile(const std::vector<NavVertex>& verts,
                        const std::vector<NavTriangle>& tris,
                        const BuildConfig& cfg,
                        dtNavMesh* navmesh, int tx, int tz,
                        const float* bmin, const float* bmax) {
    const float cs = cfg.cell_size;
    const float ch = cfg.cell_height;
    const int ts = cfg.tile_size;
    const float tcs = ts * cs; // tile world size

    // Tile bounds with border padding
    int borderSize = static_cast<int>(std::ceil(cfg.agent_radius / cs)) + 3;
    float border = borderSize * cs;

    float tileBmin[3], tileBmax[3];
    tileBmin[0] = bmin[0] + tx * tcs - border;
    tileBmin[1] = bmin[1];
    tileBmin[2] = bmin[2] + tz * tcs - border;
    tileBmax[0] = bmin[0] + (tx + 1) * tcs + border;
    tileBmax[1] = bmax[1];
    tileBmax[2] = bmin[2] + (tz + 1) * tcs + border;

    // Recast config
    rcConfig rc_cfg;
    std::memset(&rc_cfg, 0, sizeof(rc_cfg));
    rc_cfg.cs = cs;
    rc_cfg.ch = ch;
    rc_cfg.walkableSlopeAngle = cfg.agent_max_slope;
    rc_cfg.walkableHeight = static_cast<int>(std::ceil(cfg.agent_height / ch));
    rc_cfg.walkableClimb = static_cast<int>(std::floor(cfg.agent_max_climb / ch));
    rc_cfg.walkableRadius = static_cast<int>(std::ceil(cfg.agent_radius / cs));
    rc_cfg.maxEdgeLen = static_cast<int>(cfg.edge_max_len / cs);
    rc_cfg.maxSimplificationError = cfg.edge_max_error;
    rc_cfg.minRegionArea = cfg.region_min_size * cfg.region_min_size;
    rc_cfg.mergeRegionArea = cfg.region_merge_size * cfg.region_merge_size;
    rc_cfg.maxVertsPerPoly = cfg.max_verts_per_poly;
    rc_cfg.tileSize = ts;
    rc_cfg.borderSize = borderSize;
    rc_cfg.width = ts + borderSize * 2;
    rc_cfg.height = ts + borderSize * 2;
    rc_cfg.detailSampleDist = cfg.detail_sample_dist < 0.9f ? 0 : cs * cfg.detail_sample_dist;
    rc_cfg.detailSampleMaxError = ch * cfg.detail_sample_max_error;
    rcVcopy(rc_cfg.bmin, tileBmin);
    rcVcopy(rc_cfg.bmax, tileBmax);

    rcContext ctx;

    // Rasterize
    rcHeightfield* hf = rcAllocHeightfield();
    if (!hf || !rcCreateHeightfield(&ctx, *hf, rc_cfg.width, rc_cfg.height,
                                     rc_cfg.bmin, rc_cfg.bmax, cs, ch)) {
        rcFreeHeightField(hf);
        return false;
    }

    const int ntris = static_cast<int>(tris.size());
    std::vector<unsigned char> areas(ntris, 0);

    const float* v = reinterpret_cast<const float*>(verts.data());
    const int* t = reinterpret_cast<const int*>(tris.data());
    rcMarkWalkableTriangles(&ctx, rc_cfg.walkableSlopeAngle, v,
                            static_cast<int>(verts.size()), t, ntris, areas.data());
    if (!rcRasterizeTriangles(&ctx, v, static_cast<int>(verts.size()),
                               t, areas.data(), ntris, *hf, rc_cfg.walkableClimb)) {
        rcFreeHeightField(hf);
        return false;
    }

    // Filter
    rcFilterLowHangingWalkableObstacles(&ctx, rc_cfg.walkableClimb, *hf);
    rcFilterLedgeSpans(&ctx, rc_cfg.walkableHeight, rc_cfg.walkableClimb, *hf);
    rcFilterWalkableLowHeightSpans(&ctx, rc_cfg.walkableHeight, *hf);

    // Compact heightfield
    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    if (!chf || !rcBuildCompactHeightfield(&ctx, rc_cfg.walkableHeight, rc_cfg.walkableClimb,
                                            *hf, *chf)) {
        rcFreeHeightField(hf);
        rcFreeCompactHeightfield(chf);
        return false;
    }
    rcFreeHeightField(hf);

    if (!rcErodeWalkableArea(&ctx, rc_cfg.walkableRadius, *chf)) {
        rcFreeCompactHeightfield(chf);
        return false;
    }

    // Build regions
    if (!rcBuildDistanceField(&ctx, *chf)) {
        rcFreeCompactHeightfield(chf);
        return false;
    }
    if (!rcBuildRegions(&ctx, *chf, rc_cfg.borderSize, rc_cfg.minRegionArea,
                         rc_cfg.mergeRegionArea)) {
        rcFreeCompactHeightfield(chf);
        return false;
    }

    // Contours
    rcContourSet* cset = rcAllocContourSet();
    if (!cset || !rcBuildContours(&ctx, *chf, rc_cfg.maxSimplificationError,
                                   rc_cfg.maxEdgeLen, *cset)) {
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        return false;
    }

    // Poly mesh
    rcPolyMesh* pmesh = rcAllocPolyMesh();
    if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, rc_cfg.maxVertsPerPoly, *pmesh)) {
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        rcFreePolyMesh(pmesh);
        return false;
    }

    // Detail mesh
    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf,
                                          rc_cfg.detailSampleDist,
                                          rc_cfg.detailSampleMaxError, *dmesh)) {
        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);

    // Set poly flags (all walkable)
    for (int i = 0; i < pmesh->npolys; ++i) {
        pmesh->flags[i] = 1;
    }

    // Create Detour tile data
    dtNavMeshCreateParams params;
    std::memset(&params, 0, sizeof(params));
    params.verts = pmesh->verts;
    params.vertCount = pmesh->nverts;
    params.polys = pmesh->polys;
    params.polyAreas = pmesh->areas;
    params.polyFlags = pmesh->flags;
    params.polyCount = pmesh->npolys;
    params.nvp = pmesh->nvp;
    params.detailMeshes = dmesh->meshes;
    params.detailVerts = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris = dmesh->tris;
    params.detailTriCount = dmesh->ntris;
    params.walkableHeight = cfg.agent_height;
    params.walkableRadius = cfg.agent_radius;
    params.walkableClimb = cfg.agent_max_climb;
    params.tileX = tx;
    params.tileY = tz;
    params.tileLayer = 0;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = cs;
    params.ch = ch;
    params.buildBvTree = true;

    unsigned char* navData = nullptr;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        return false;
    }

    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    dtStatus status = navmesh->addTile(navData, navDataSize, DT_TILE_FREE_DATA, 0, nullptr);
    if (dtStatusFailed(status)) {
        dtFree(navData);
        return false;
    }

    return true;
}

dtNavMesh* build_navmesh(const ZoneGeometry& geo, const BuildConfig& cfg) {
    // Merge per-object geometry into flat arrays
    std::vector<NavVertex> verts;
    std::vector<NavTriangle> tris;
    float bmin[3], bmax[3];
    geo.merge(verts, tris, bmin, bmax);

    if (verts.empty() || tris.empty()) {
        throw std::runtime_error("No geometry to build navmesh from");
    }

    const float cs = cfg.cell_size;
    const int ts = cfg.tile_size;
    const float tcs = ts * cs;

    int gw = static_cast<int>(std::ceil((bmax[0] - bmin[0]) / tcs));
    int gh = static_cast<int>(std::ceil((bmax[2] - bmin[2]) / tcs));
    if (gw <= 0) gw = 1;
    if (gh <= 0) gh = 1;

    std::cerr << "Building navmesh: " << gw << "x" << gh << " tiles ("
              << gw * gh << " total)\n";

    dtNavMeshParams navParams;
    std::memset(&navParams, 0, sizeof(navParams));
    rcVcopy(navParams.orig, bmin);
    navParams.tileWidth = tcs;
    navParams.tileHeight = tcs;
    navParams.maxTiles = gw * gh;
    navParams.maxPolys = 1 << 14;

    dtNavMesh* navmesh = dtAllocNavMesh();
    if (!navmesh) throw std::runtime_error("Failed to allocate navmesh");

    dtStatus status = navmesh->init(&navParams);
    if (dtStatusFailed(status)) {
        dtFreeNavMesh(navmesh);
        throw std::runtime_error("Failed to initialize navmesh");
    }

    int built = 0;
    for (int tz = 0; tz < gh; ++tz) {
        for (int tx = 0; tx < gw; ++tx) {
            if (build_tile(verts, tris, cfg, navmesh, tx, tz, bmin, bmax)) {
                ++built;
            }
        }
    }

    std::cerr << "Built " << built << "/" << gw * gh << " tiles\n";
    return navmesh;
}

NavmeshStats get_navmesh_stats(const dtNavMesh* mesh) {
    NavmeshStats stats;
    if (!mesh) return stats;

    for (int i = 0; i < mesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh->getTile(i);
        if (!tile || !tile->header) continue;
        stats.total_tiles++;
        stats.total_polys += tile->header->polyCount;
        stats.total_verts += tile->header->vertCount;
    }
    return stats;
}

} // namespace navmesh
