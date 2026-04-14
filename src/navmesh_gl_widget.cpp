// navmesh_gl_widget.cpp — Navmesh editor GL viewport.
// Subclasses BaseGLViewport for shared rendering, adding navmesh overlay.

#include "navmesh_gl_widget.h"
#include "gl_helpers.h"

#include <DetourNavMesh.h>
#include <QOpenGLFunctions>
#include <cmath>
#include <algorithm>

namespace navmesh_editor {

using gl_viewport::RenderMesh;

NavmeshGLWidget::NavmeshGLWidget(QWidget* parent) : BaseGLViewport(parent) {}

void NavmeshGLWidget::clear() {
    clearMeshes();
    navMeshes_.clear();
    stats_ = {};
    terrainIdx_ = -1;
    emit statsChanged();
}

// ---------------------------------------------------------------------------
// Source geometry
// ---------------------------------------------------------------------------

void NavmeshGLWidget::loadSourceGeometry(const navmesh::ZoneGeometry& geo) {
    clearMeshes();
    terrainIdx_ = -1;

    static const float terrainColor[] = {0.5f, 0.4f, 0.3f, 0.25f};
    static const float terrainWire[]  = {0.6f, 0.5f, 0.4f};
    static const float hkxColor[]     = {0.6f, 0.6f, 0.6f, 0.25f};
    static const float hkxWire[]      = {0.5f, 0.5f, 0.5f};
    static const float nifColor[]     = {0.4f, 0.5f, 0.6f, 0.25f};
    static const float nifWire[]      = {0.5f, 0.6f, 0.7f};
    static const float primColor[]    = {0.7f, 0.7f, 0.3f, 0.3f};
    static const float primWire[]     = {0.8f, 0.8f, 0.4f};

    for (size_t i = 0; i < geo.objects.size(); ++i) {
        const auto& obj = geo.objects[i];
        const float* col = hkxColor;
        const float* wire = hkxWire;
        if (obj.source == "terrain") { col = terrainColor; wire = terrainWire; terrainIdx_ = static_cast<int>(i); }
        else if (obj.source == "nif") { col = nifColor; wire = nifWire; }
        else if (obj.source == "primitive") { col = primColor; wire = primWire; }

        RenderMesh rm;
        std::copy(col, col + 4, rm.color);
        std::copy(wire, wire + 3, rm.wireColor);
        rm.label = obj.label;
        rm.vertices.reserve(obj.vertices.size() * 3);
        for (const auto& v : obj.vertices) { rm.vertices.push_back(v.x); rm.vertices.push_back(v.y); rm.vertices.push_back(v.z); }
        rm.indices.reserve(obj.triangles.size() * 3);
        for (const auto& tri : obj.triangles) { rm.indices.push_back(tri.a); rm.indices.push_back(tri.b); rm.indices.push_back(tri.c); }
        addMesh(std::move(rm));
    }

    // Auto-hide terrain if far from objects
    if (terrainIdx_ >= 0 && !terrainOverlapsObjects())
        setMeshVisible(terrainIdx_, false);

    stats_.source_objects = static_cast<int>(geo.objects.size());
    stats_.terrain_tris = geo.terrain_tris;
    stats_.hkx_tris = geo.hkx_tris;
    stats_.nif_tris = geo.nif_tris;
    stats_.prim_tris = geo.prim_tris;
    stats_.source_triangles = geo.terrain_tris + geo.hkx_tris + geo.nif_tris + geo.prim_tris;

    fitToVisible();
    emit statsChanged();
}

int NavmeshGLWidget::deleteSelected() {
    int idx = selectedIndex();
    if (idx < 0 || idx >= meshCount()) return -1;
    int deleted = idx;
    removeMesh(deleted);
    emit objectDeleted(deleted);
    return deleted;
}

bool NavmeshGLWidget::terrainOverlapsObjects(float threshold) const {
    if (terrainIdx_ < 0 || terrainIdx_ >= meshCount()) return true;
    float tb[3], tB[3]; meshBounds(meshAt(terrainIdx_), tb, tB);
    float ob[3]={1e30f,1e30f,1e30f}, oB[3]={-1e30f,-1e30f,-1e30f};
    for (int i = 0; i < meshCount(); ++i) {
        if (i == terrainIdx_ || meshAt(i).vertices.empty()) continue;
        float mb[3], mB[3]; meshBounds(meshAt(i), mb, mB);
        for (int k = 0; k < 3; k++) { ob[k]=std::min(ob[k],mb[k]); oB[k]=std::max(oB[k],mB[k]); }
    }
    if (ob[0] > oB[0]) return true;
    for (int k = 0; k < 3; k++) if (tB[k]+threshold < ob[k] || oB[k]+threshold < tb[k]) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Navmesh loading
// ---------------------------------------------------------------------------

void NavmeshGLWidget::loadNavmesh(const dtNavMesh* mesh) {
    navMeshes_.clear();
    if (mesh) buildNavmeshMeshes(mesh);
    fitToVisible();
    emit statsChanged();
    update();
}

void NavmeshGLWidget::buildNavmeshMeshes(const dtNavMesh* mesh) {
    stats_.nav_tiles = 0; stats_.nav_polys = 0; stats_.nav_verts = 0;
    static const float palette[][3] = {
        {0.2f,0.7f,0.3f},{0.3f,0.6f,0.9f},{0.9f,0.6f,0.2f},{0.7f,0.3f,0.8f},
        {0.2f,0.8f,0.8f},{0.9f,0.9f,0.2f},{0.8f,0.3f,0.3f},{0.5f,0.8f,0.5f},
    };
    int tileIdx = 0;
    for (int i = 0; i < mesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh->getTile(i);
        if (!tile || !tile->header || tile->header->polyCount <= 0) continue;
        const dtMeshHeader* hdr = tile->header;
        stats_.nav_tiles++; stats_.nav_polys += hdr->polyCount; stats_.nav_verts += hdr->vertCount;
        RenderMesh rm;
        const float* col = palette[tileIdx % 8];
        rm.color[0]=col[0]; rm.color[1]=col[1]; rm.color[2]=col[2]; rm.color[3]=0.45f;
        rm.wireColor[0]=1; rm.wireColor[1]=1; rm.wireColor[2]=1;

        if (hdr->detailMeshCount > 0 && tile->detailMeshes && tile->detailVerts && tile->detailTris) {
            for (int p = 0; p < hdr->polyCount; ++p) {
                const dtPoly& poly = tile->polys[p];
                if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
                const dtPolyDetail& pd = tile->detailMeshes[p];
                for (unsigned int t = 0; t < pd.triCount; ++t) {
                    const unsigned char* dtri = &tile->detailTris[(pd.triBase+t)*4];
                    uint32_t base = static_cast<uint32_t>(rm.vertices.size()/3);
                    for (int v = 0; v < 3; ++v) {
                        const float* src = (dtri[v] < poly.vertCount) ? &tile->verts[poly.verts[dtri[v]]*3] : &tile->detailVerts[(pd.vertBase+dtri[v]-poly.vertCount)*3];
                        rm.vertices.push_back(src[0]); rm.vertices.push_back(src[1]); rm.vertices.push_back(src[2]);
                    }
                    rm.indices.push_back(base); rm.indices.push_back(base+1); rm.indices.push_back(base+2);
                }
            }
        } else {
            for (int p = 0; p < hdr->polyCount; ++p) {
                const dtPoly& poly = tile->polys[p];
                if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
                uint32_t base = static_cast<uint32_t>(rm.vertices.size()/3);
                for (int v = 0; v < poly.vertCount; ++v) { const float* tv = &tile->verts[poly.verts[v]*3]; rm.vertices.push_back(tv[0]); rm.vertices.push_back(tv[1]); rm.vertices.push_back(tv[2]); }
                for (int v = 1; v < poly.vertCount-1; ++v) { rm.indices.push_back(base); rm.indices.push_back(base+v); rm.indices.push_back(base+v+1); }
            }
        }
        if (!rm.indices.empty()) navMeshes_.push_back(std::move(rm));
        tileIdx++;
    }
}

// ---------------------------------------------------------------------------
// Overrides
// ---------------------------------------------------------------------------

void NavmeshGLWidget::drawBackground() {
    gl_viewport::drawGrid(500.0f, 50.0f);
    gl_viewport::drawAxes(30.0f);
}

bool NavmeshGLWidget::shouldDrawMesh(int /*idx*/) const {
    return showSourceGeo_;
}

void NavmeshGLWidget::drawOverlay() {
    if (!showNavmesh_) return;
    for (const auto& mesh : navMeshes_) {
        if (mesh.vertices.empty() || mesh.indices.empty()) continue;
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, mesh.vertices.data());
        if (showSolid) {
            glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1,1);
            glColor4f(mesh.color[0], mesh.color[1], mesh.color[2], mesh.color[3]);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, mesh.indices.data());
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        if (showWireframe) {
            glColor4f(mesh.wireColor[0], mesh.wireColor[1], mesh.wireColor[2], 1.0f);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, mesh.indices.data());
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisableClientState(GL_VERTEX_ARRAY);
    }
}

} // namespace navmesh_editor
