// navmesh_gl_widget.h — Navmesh editor GL viewport subclass.
//
// Renders source geometry (per-object, selectable/deletable) and navmesh
// polygons as an overlay. Subclasses BaseGLViewport.
#pragma once

#include "gl_viewport_widget.h"
#include "zone_geometry.h"

#include <vector>
#include <cstdint>

struct dtNavMesh;

namespace navmesh_editor {

struct EditorStats {
    int source_objects = 0;
    int source_triangles = 0;
    int terrain_tris = 0;
    int hkx_tris = 0;
    int nif_tris = 0;
    int prim_tris = 0;
    int nav_tiles = 0;
    int nav_polys = 0;
    int nav_verts = 0;
};

class NavmeshGLWidget : public gl_viewport::BaseGLViewport {
    Q_OBJECT
public:
    explicit NavmeshGLWidget(QWidget* parent = nullptr);

    // Load source geometry (per-object) for display.
    void loadSourceGeometry(const navmesh::ZoneGeometry& geo);

    // Load a built navmesh for display.
    void loadNavmesh(const dtNavMesh* mesh);

    // Clear all geometry.
    void clear();

    // Delete the currently selected source object. Returns the index deleted, or -1.
    int deleteSelected();

    // Per-object visibility.
    void setObjectVisible(int idx, bool vis) { setMeshVisible(idx, vis); }
    bool isObjectVisible(int idx) const { return isMeshVisible(idx); }

    // Check if terrain overlaps with other objects.
    bool terrainOverlapsObjects(float threshold = 100.0f) const;

    const EditorStats& stats() const { return stats_; }

    bool showSourceGeo_ = true;
    bool showNavmesh_ = true;

signals:
    void statsChanged();
    void objectSelected(int index);
    void objectDeleted(int index);
    void contextMenuRequested(int objectIndex, QPoint globalPos);

protected:
    void drawBackground() override;
    void drawOverlay() override;
    bool shouldDrawMesh(int idx) const override;

private:
    void buildNavmeshMeshes(const dtNavMesh* mesh);

    std::vector<gl_viewport::RenderMesh> navMeshes_; // navmesh polygons (separate from source)
    EditorStats stats_;
    int terrainIdx_ = -1;
};

} // namespace navmesh_editor
