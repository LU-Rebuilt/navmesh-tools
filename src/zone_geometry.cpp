#include "zone_geometry.h"

#include "netdevil/zone/luz/luz_reader.h"
#include "netdevil/zone/lvl/lvl_reader.h"
#include "netdevil/zone/terrain/terrain_reader.h"
#include "havok/reader/hkx_reader.h"
#include "havok/converters/hkx_geometry.h"
#include "gamebryo/nif/nif_reader.h"
#include "gamebryo/nif/nif_geometry.h"
#include "common/primitives/primitives.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace navmesh {

// ---------------------------------------------------------------------------
// ZoneGeometry::merge — flatten per-object geometry for Recast
// ---------------------------------------------------------------------------

void ZoneGeometry::merge(std::vector<NavVertex>& out_verts,
                          std::vector<NavTriangle>& out_tris,
                          float* bmin, float* bmax) const {
    out_verts.clear();
    out_tris.clear();
    bmin[0] = bmin[1] = bmin[2] = 1e30f;
    bmax[0] = bmax[1] = bmax[2] = -1e30f;

    for (const auto& obj : objects) {
        int base = static_cast<int>(out_verts.size());
        for (const auto& v : obj.vertices) {
            out_verts.push_back(v);
            bmin[0] = std::min(bmin[0], v.x); bmin[1] = std::min(bmin[1], v.y); bmin[2] = std::min(bmin[2], v.z);
            bmax[0] = std::max(bmax[0], v.x); bmax[1] = std::max(bmax[1], v.y); bmax[2] = std::max(bmax[2], v.z);
        }
        for (const auto& tri : obj.triangles) {
            out_tris.push_back({tri.a + base, tri.b + base, tri.c + base});
        }
    }
}

// ---------------------------------------------------------------------------
// ObjectGeo helpers
// ---------------------------------------------------------------------------

static int obj_add_vertex(ObjectGeo& obj, float x, float y, float z) {
    int idx = static_cast<int>(obj.vertices.size());
    obj.vertices.push_back({x, y, z});
    return idx;
}

static void obj_add_triangle(ObjectGeo& obj, int a, int b, int c) {
    obj.triangles.push_back({a, b, c});
}

// ---------------------------------------------------------------------------
// Terrain loading
// ---------------------------------------------------------------------------

ObjectGeo load_terrain(const std::filesystem::path& raw_path) {
    ObjectGeo obj;
    obj.source = "terrain";
    obj.label = "Terrain";
    obj.asset_path = raw_path.filename().string();

    std::ifstream f(raw_path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open terrain: " + raw_path.string());
    auto sz = f.tellg(); f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data.data()), sz);

    auto terrain = lu::assets::terrain_parse(data);
    auto mesh = lu::assets::terrain_generate_mesh(terrain);

    // Convert to ObjectGeo
    for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3)
        obj_add_vertex(obj, mesh.vertices[i], mesh.vertices[i+1], mesh.vertices[i+2]);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        obj_add_triangle(obj, mesh.indices[i], mesh.indices[i+1], mesh.indices[i+2]);

    std::cerr << "  Terrain: " << obj.triangles.size() << " triangles from "
              << terrain.chunks.size() << " chunks\n";
    return obj;
}

// ---------------------------------------------------------------------------
// HKX collision loading — delegates to shared Hkx::extractCollision()
// ---------------------------------------------------------------------------

ObjectGeo load_hkx_collision(const std::filesystem::path& hkx_path,
                              float pos_x, float pos_y, float pos_z, float scale) {
    ObjectGeo obj;
    obj.source = "hkx";
    obj.asset_path = hkx_path.filename().string();
    obj.pos_x = pos_x; obj.pos_y = pos_y; obj.pos_z = pos_z;
    obj.scale = scale;

    std::ifstream f(hkx_path, std::ios::binary | std::ios::ate);
    if (!f) return obj;
    auto sz = f.tellg(); f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data.data()), sz);

    try {
        Hkx::HkxFile hkx;
        auto result = hkx.Parse(data.data(), data.size());

        // Build the object's world transform: scale + translation from LVL placement
        Hkx::Transform objWorld;
        objWorld.col0 = {scale, 0, 0, 0};
        objWorld.col1 = {0, scale, 0, 0};
        objWorld.col2 = {0, 0, scale, 0};
        objWorld.translation = {pos_x, pos_y, pos_z, 1};

        // Use shared extraction from lu_assets
        auto collision = Hkx::extractCollision(result, objWorld);

        // Convert flat collision mesh to ObjectGeo
        for (size_t i = 0; i + 2 < collision.vertices.size(); i += 3)
            obj_add_vertex(obj, collision.vertices[i], collision.vertices[i+1], collision.vertices[i+2]);
        for (size_t i = 0; i + 2 < collision.indices.size(); i += 3)
            obj_add_triangle(obj, collision.indices[i], collision.indices[i+1], collision.indices[i+2]);
    } catch (const std::exception& e) {
        std::cerr << "  Warning: HKX parse error " << hkx_path.filename() << ": " << e.what() << "\n";
    }

    return obj;
}

// ---------------------------------------------------------------------------
// NIF mesh loading
// ---------------------------------------------------------------------------

ObjectGeo load_nif_mesh(const std::filesystem::path& nif_path,
                         float pos_x, float pos_y, float pos_z, float scale) {
    ObjectGeo obj;
    obj.source = "nif";
    obj.asset_path = nif_path.filename().string();
    obj.pos_x = pos_x; obj.pos_y = pos_y; obj.pos_z = pos_z;
    obj.scale = scale;

    std::ifstream f(nif_path, std::ios::binary | std::ios::ate);
    if (!f) return obj;
    auto sz = f.tellg(); f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data.data()), sz);

    try {
        auto nif = lu::assets::nif_parse(data);
        auto extraction = lu::assets::extractNifGeometry(nif, pos_x, pos_y, pos_z, scale);
        for (auto& em : extraction.meshes) {
            int base = static_cast<int>(obj.vertices.size());
            for (size_t i = 0; i + 2 < em.vertices.size(); i += 3)
                obj_add_vertex(obj, em.vertices[i], em.vertices[i+1], em.vertices[i+2]);
            for (size_t i = 0; i + 2 < em.indices.size(); i += 3)
                obj_add_triangle(obj, base + em.indices[i], base + em.indices[i+1], base + em.indices[i+2]);
        }
    } catch (const std::exception& e) {
        std::cerr << "  Warning: NIF parse error " << nif_path.filename() << ": " << e.what() << "\n";
    }

    return obj;
}

// ---------------------------------------------------------------------------
// LDF settings
// ---------------------------------------------------------------------------

struct ObjectLdfSettings {
    bool intangible = false;
    bool no_physics = false;
    bool ignore_collision = false;
    bool create_physics = true;
    bool add_to_navmesh = false;
    int primitive_type = -1;
    float primitive_x = 0, primitive_y = 0, primitive_z = 0;
};

static ObjectLdfSettings parse_ldf_settings(const lu::assets::LvlObject& obj) {
    ObjectLdfSettings s;
    for (const auto& entry : obj.config) {
        if (entry.key == "is_intangible" || entry.key == "intangible")
            s.intangible = (entry.raw_value == "1" || entry.raw_value == "true");
        else if (entry.key == "nophysics" || entry.key == "no_physics")
            s.no_physics = (entry.raw_value == "1" || entry.raw_value == "true");
        else if (entry.key == "ignore_collision")
            s.ignore_collision = (entry.raw_value == "1" || entry.raw_value == "true");
        else if (entry.key == "create_physics")
            s.create_physics = (entry.raw_value == "1" || entry.raw_value == "true");
        else if (entry.key == "add_to_navmesh")
            s.add_to_navmesh = (entry.raw_value == "1" || entry.raw_value == "true");
        else if (entry.key == "primitiveModelType")
            s.primitive_type = std::atoi(entry.raw_value.c_str());
        else if (entry.key == "primitiveModelValueX")
            s.primitive_x = static_cast<float>(std::atof(entry.raw_value.c_str()));
        else if (entry.key == "primitiveModelValueY")
            s.primitive_y = static_cast<float>(std::atof(entry.raw_value.c_str()));
        else if (entry.key == "primitiveModelValueZ")
            s.primitive_z = static_cast<float>(std::atof(entry.raw_value.c_str()));
    }
    return s;
}

static bool node_type_can_contribute(lu::assets::LvlNodeType type) {
    using NT = lu::assets::LvlNodeType;
    switch (type) {
        case NT::EnvironmentObj: case NT::Building: case NT::Rebuilder:
        case NT::Bouncer: case NT::Exhibit: case NT::Springpad:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Primitive geometry generation
// ---------------------------------------------------------------------------

static ObjectGeo make_box_primitive(uint32_t lot, float cx, float cy, float cz,
                                     float hx, float hy, float hz) {
    ObjectGeo obj;
    obj.lot = lot; obj.source = "primitive";
    obj.pos_x = cx; obj.pos_y = cy; obj.pos_z = cz;
    obj.label = "LOT " + std::to_string(lot) + " — box primitive";

    auto pm = lu::assets::generate_box(cx, cy, cz, hx, hy, hz);
    for (size_t i = 0; i + 2 < pm.vertices.size(); i += 3)
        obj_add_vertex(obj, pm.vertices[i], pm.vertices[i+1], pm.vertices[i+2]);
    for (size_t i = 0; i + 2 < pm.indices.size(); i += 3)
        obj_add_triangle(obj, pm.indices[i], pm.indices[i+1], pm.indices[i+2]);
    return obj;
}

static ObjectGeo make_sphere_primitive(uint32_t lot, float cx, float cy, float cz,
                                        float radius) {
    ObjectGeo obj;
    obj.lot = lot; obj.source = "primitive";
    obj.pos_x = cx; obj.pos_y = cy; obj.pos_z = cz;
    obj.label = "LOT " + std::to_string(lot) + " — sphere primitive";

    auto pm = lu::assets::generate_sphere(cx, cy, cz, radius);
    for (size_t i = 0; i + 2 < pm.vertices.size(); i += 3)
        obj_add_vertex(obj, pm.vertices[i], pm.vertices[i+1], pm.vertices[i+2]);
    for (size_t i = 0; i + 2 < pm.indices.size(); i += 3)
        obj_add_triangle(obj, pm.indices[i], pm.indices[i+1], pm.indices[i+2]);
    return obj;
}

// ---------------------------------------------------------------------------
// Normalize asset path / find asset file
// ---------------------------------------------------------------------------

static std::string normalize_asset_path(const std::string& path) {
    std::string result = path;
    for (auto& c : result) { if (c == '\\') c = '/'; }
    while (!result.empty() && result[0] == '/') result.erase(result.begin());
    return result;
}

static std::filesystem::path find_asset_file(
    const std::filesystem::path& client_root, const std::string& raw_asset_path) {
    namespace fs = std::filesystem;
    if (raw_asset_path.empty()) return {};
    std::string asset_path = normalize_asset_path(raw_asset_path);

    fs::path direct = client_root / "res" / asset_path;
    if (fs::exists(direct)) return direct;

    if (asset_path.size() > 4 &&
        (asset_path.substr(0, 4) == "res/" || asset_path.substr(0, 4) == "res\\")) {
        fs::path stripped = client_root / asset_path;
        if (fs::exists(stripped)) return stripped;
    }

    fs::path parent = direct.parent_path();
    std::string target_lower = direct.filename().string();
    for (auto& c : target_lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (fs::exists(parent) && fs::is_directory(parent)) {
        for (const auto& entry : fs::directory_iterator(parent)) {
            std::string name = entry.path().filename().string();
            for (auto& c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (name == target_lower) return entry.path();
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Zone loading — full pipeline
// ---------------------------------------------------------------------------

ZoneGeometry load_zone_geometry(const CdClientDb& cdclient, uint32_t zone_id) {
    namespace fs = std::filesystem;
    ZoneGeometry geo;

    fs::path client_root = cdclient.client_root();
    fs::path maps_dir = client_root / "res" / "maps";

    // Find the LUZ file
    fs::path luz_path;
    lu::assets::LuzFile luz;
    for (const auto& entry : fs::recursive_directory_iterator(maps_dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (ext != ".luz") continue;

        std::ifstream f(entry.path(), std::ios::binary | std::ios::ate);
        auto sz = f.tellg(); f.seekg(0);
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.read(reinterpret_cast<char*>(data.data()), sz);

        try {
            auto parsed = lu::assets::luz_parse(data);
            if (parsed.world_id == zone_id) {
                luz_path = entry.path();
                luz = std::move(parsed);
                std::cerr << "Found zone " << zone_id << ": " << luz_path << "\n";
                break;
            }
        } catch (...) { continue; }
    }
    if (luz_path.empty())
        throw std::runtime_error("Zone " + std::to_string(zone_id) + " not found in " + maps_dir.string());

    // Load terrain as the first object
    if (!luz.raw_path.empty()) {
        fs::path raw_full = luz_path.parent_path() / luz.raw_path;
        if (fs::exists(raw_full)) {
            auto terrain_obj = load_terrain(raw_full);
            geo.terrain_tris = static_cast<int>(terrain_obj.triangles.size());
            geo.objects.push_back(std::move(terrain_obj));
        } else {
            std::cerr << "  Warning: terrain not found: " << raw_full << "\n";
        }
    }

    // Collect objects from LVL scenes
    struct ObjInst {
        uint32_t lot;
        float px, py, pz, scale;
        ObjectLdfSettings ldf;
    };
    std::vector<ObjInst> instances;
    std::unordered_set<uint32_t> unique_lots;

    for (const auto& scene : luz.scenes) {
        if (scene.filename.empty()) continue;
        fs::path lvl_path = luz_path.parent_path() / scene.filename;
        if (!fs::exists(lvl_path)) { std::cerr << "  Warning: scene not found: " << lvl_path << "\n"; continue; }

        std::ifstream f(lvl_path, std::ios::binary | std::ios::ate);
        auto sz = f.tellg(); f.seekg(0);
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.read(reinterpret_cast<char*>(data.data()), sz);

        try {
            auto lvl = lu::assets::lvl_parse(data);
            std::cerr << "  Scene '" << scene.name << "': " << lvl.objects.size() << " objects\n";

            for (const auto& obj : lvl.objects) {
                geo.total_objects++;
                if (!node_type_can_contribute(obj.node_type)) continue;
                auto ldf = parse_ldf_settings(obj);
                if (ldf.intangible || ldf.no_physics || ldf.ignore_collision) continue;
                if (!ldf.create_physics && !ldf.add_to_navmesh) continue;

                instances.push_back({obj.lot, obj.position.x, obj.position.y,
                                     obj.position.z, obj.scale, ldf});
                unique_lots.insert(obj.lot);
            }
        } catch (const std::exception& e) {
            std::cerr << "  Warning: LVL parse error " << scene.filename << ": " << e.what() << "\n";
        }
    }

    std::cerr << "  " << instances.size() << " candidate objects, " << unique_lots.size() << " unique LOTs\n";

    // Batch resolve LOTs
    std::vector<uint32_t> lot_list(unique_lots.begin(), unique_lots.end());
    auto asset_map = cdclient.resolve_lots(lot_list);

    int hkx_loaded = 0, prim_loaded = 0, nif_loaded = 0, skipped = 0;

    for (const auto& inst : instances) {
        const auto& ldf = inst.ldf;

        // Primitive shapes from LDF override CDClient
        if (ldf.primitive_type >= 1 && ldf.primitive_type <= 4) {
            float sx = ldf.primitive_x * inst.scale;
            float sy = ldf.primitive_y * inst.scale;
            float sz = ldf.primitive_z * inst.scale;

            ObjectGeo prim;
            if (ldf.primitive_type == 3) {
                prim = make_sphere_primitive(inst.lot, inst.px, inst.py, inst.pz, sx > 0 ? sx : 1.0f);
            } else {
                // Box (1), Cylinder (2), Capsule (4) → box approximation
                prim = make_box_primitive(inst.lot, inst.px, inst.py, inst.pz,
                                          sx * 0.5f, sy * 0.5f, sz * 0.5f);
            }
            if (!prim.triangles.empty()) {
                geo.prim_tris += static_cast<int>(prim.triangles.size());
                geo.objects.push_back(std::move(prim));
                geo.objects_with_geo++;
                prim_loaded++;
            }
            continue;
        }

        auto it = asset_map.find(inst.lot);
        if (it == asset_map.end()) {
            std::cerr << "    LOT " << inst.lot << ": no CDClient entry\n";
            skipped++; continue;
        }
        const auto& asset = it->second;

        // Skip trigger/sound assets — check both physics and render paths
        auto is_trigger_asset = [](const std::string& path) {
            std::string lower = path;
            for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            return lower.find("trigger") != std::string::npos ||
                   lower.find("sound_trigger") != std::string::npos;
        };
        if (is_trigger_asset(asset.physics_asset) || is_trigger_asset(asset.render_asset)) {
            skipped++; continue;
        }

        // Only load if it has SimplePhysics (3) or ControllablePhysics (1) with an asset.
        // physics_component_type == 0 means no physics component found in CDClient.
        if (asset.physics_component_type == 0 && !ldf.add_to_navmesh) {
            skipped++; continue;
        }

        // Try to find HKX collision file. Three strategies:
        // 1. Explicit physics_asset from CDClient (prepend "physics/")
        // 2. Explicit physics_asset without prefix
        // 3. Derive from render_asset: mesh/foo/bar.nif → physics/foo/bar.hkx
        fs::path hkx_file;

        if (!asset.physics_asset.empty()) {
            hkx_file = find_asset_file(client_root, "physics/" + asset.physics_asset);
            if (hkx_file.empty())
                hkx_file = find_asset_file(client_root, asset.physics_asset);
        }

        // Derive HKX from NIF render_asset if explicit path didn't resolve
        if (hkx_file.empty() && !asset.render_asset.empty()) {
            std::string derived = normalize_asset_path(asset.render_asset);
            auto meshPos = derived.find("mesh/");
            if (meshPos != std::string::npos) {
                derived.replace(meshPos, 5, "physics/");
                if (derived.size() >= 4 && derived.substr(derived.size() - 4) == ".nif")
                    derived = derived.substr(0, derived.size() - 4) + ".hkx";
                hkx_file = find_asset_file(client_root, derived);
            }
        }

        if (!hkx_file.empty()) {
            auto hkx_obj = load_hkx_collision(hkx_file, inst.px, inst.py, inst.pz, inst.scale);
            hkx_obj.lot = inst.lot;
            hkx_obj.label = "LOT " + std::to_string(inst.lot) + " — " + hkx_obj.asset_path;
            if (!hkx_obj.triangles.empty()) {
                geo.hkx_tris += static_cast<int>(hkx_obj.triangles.size());
                geo.objects.push_back(std::move(hkx_obj));
                geo.objects_with_geo++;
                hkx_loaded++;
            }
            continue;
        }

        // NIF fallback only if add_to_navmesh
        if (ldf.add_to_navmesh && !asset.render_asset.empty()) {
            fs::path nif_file = find_asset_file(client_root, asset.render_asset);
            if (!nif_file.empty()) {
                auto nif_obj = load_nif_mesh(nif_file, inst.px, inst.py, inst.pz, inst.scale);
                nif_obj.lot = inst.lot;
                nif_obj.label = "LOT " + std::to_string(inst.lot) + " — " + nif_obj.asset_path;
                if (!nif_obj.triangles.empty()) {
                    geo.nif_tris += static_cast<int>(nif_obj.triangles.size());
                    geo.objects.push_back(std::move(nif_obj));
                    geo.objects_with_geo++;
                    nif_loaded++;
                }
                continue;
            }
        }
        skipped++;
    }

    std::cerr << "Zone " << zone_id << " geometry loaded:\n"
              << "  " << geo.objects.size() << " object meshes\n"
              << "  " << geo.terrain_tris << " terrain + " << geo.hkx_tris << " HKX + "
              << geo.nif_tris << " NIF + " << geo.prim_tris << " primitive\n"
              << "  " << hkx_loaded << " HKX, " << prim_loaded << " primitive, "
              << nif_loaded << " NIF, " << skipped << " skipped\n";

    return geo;
}

} // namespace navmesh
