#pragma once
// cdclient_db.h — CDClient SQLite database accessor for navmesh geometry resolution.
//
// Opens a CDClient SQLite database and provides queries for:
//   - Zone list (ZoneTable: zoneID, zoneName)
//   - LOT → physics_asset (HKX) via ComponentsRegistry → PhysicsComponent
//   - LOT → render_asset (NIF) via ComponentsRegistry → RenderComponent
//
// Asset paths returned are relative to the client res/ directory.
// Use fdb_converter to convert cdclient.fdb to SQLite if needed.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace navmesh {

struct ZoneInfo {
    uint32_t zone_id = 0;
    std::string zone_name;
};

// Resolved asset for a single LOT.
struct LotAsset {
    uint32_t lot = 0;
    std::string physics_asset;   // HKX path (empty if none)
    std::string render_asset;    // NIF path (empty if none)
    int physics_component_type = 0; // ComponentsRegistry component_type (3=Simple, 0=none found)
    bool is_static = false;      // PhysicsComponent.static field
};

class CdClientDb {
public:
    CdClientDb();
    ~CdClientDb();

    // Open a CDClient SQLite database.
    // client_root is the root of the client install (parent of res/).
    // Searches for cdclient.sqlite / CDClient.sqlite / CDServer.sqlite in res/.
    void open(const std::filesystem::path& client_root);

    bool is_open() const;

    // Get all zones from ZoneTable.
    std::vector<ZoneInfo> get_zones() const;

    // Resolve a LOT to its physics_asset and render_asset paths.
    // Paths are as stored in the database (typically relative to res/).
    LotAsset resolve_lot(uint32_t lot) const;

    // Batch resolve — more efficient for many LOTs.
    // Returns a map from LOT → LotAsset (only LOTs with at least one asset).
    std::unordered_map<uint32_t, LotAsset> resolve_lots(const std::vector<uint32_t>& lots) const;

    const std::filesystem::path& client_root() const { return client_root_; }

private:
    struct Deleter {
        void operator()(sqlite3* db);
    };

    std::unique_ptr<sqlite3, Deleter> db_;
    std::filesystem::path client_root_;
};

} // namespace navmesh
