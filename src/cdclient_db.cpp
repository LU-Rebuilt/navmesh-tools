#include "cdclient_db.h"

#include <sqlite3.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace navmesh {

// ---------------------------------------------------------------------------
// SQLite RAII helpers (same pattern as skill_editor/db_source.cpp)
// ---------------------------------------------------------------------------

namespace {

struct StmtDeleter {
    void operator()(sqlite3_stmt* s) { sqlite3_finalize(s); }
};
using SqliteStmt = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

SqliteStmt prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQL error: ") + sqlite3_errmsg(db));
    }
    return SqliteStmt(raw);
}

std::string col_text(sqlite3_stmt* s, int col) {
    auto p = sqlite3_column_text(s, col);
    return p ? std::string(reinterpret_cast<const char*>(p)) : "";
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CdClientDb
// ---------------------------------------------------------------------------

void CdClientDb::Deleter::operator()(sqlite3* db) {
    sqlite3_close(db);
}

CdClientDb::CdClientDb() = default;

CdClientDb::~CdClientDb() = default;

void CdClientDb::open(const std::filesystem::path& client_root) {
    namespace fs = std::filesystem;

    // User can point to either:
    //   1. The client root (parent of res/), e.g. /path/to/client/
    //   2. The res/ directory itself, e.g. /path/to/client/res/
    // We normalize client_root_ to always be the parent of res/ so that
    // asset lookups like client_root_ / "res" / "maps" work consistently.

    fs::path db_path;

    // Try as client root (contains res/)
    std::vector<fs::path> via_res = {
        client_root / "res" / "cdclient.sqlite",
        client_root / "res" / "CDClient.sqlite",
        client_root / "res" / "CDServer.sqlite",
    };
    for (const auto& c : via_res) {
        if (fs::exists(c)) {
            db_path = c;
            client_root_ = client_root;
            break;
        }
    }

    // Try as res/ directory itself (contains cdclient directly)
    if (db_path.empty()) {
        std::vector<fs::path> direct = {
            client_root / "cdclient.sqlite",
            client_root / "CDClient.sqlite",
            client_root / "CDServer.sqlite",
        };
        for (const auto& c : direct) {
            if (fs::exists(c)) {
                db_path = c;
                // The user pointed at res/ — go up one level so
                // client_root_ / "res" resolves correctly.
                client_root_ = client_root.parent_path();
                break;
            }
        }
    }

    if (db_path.empty()) {
        throw std::runtime_error(
            "No CDClient SQLite database found in " + client_root.string() +
            ". Expected cdclient.sqlite in the selected directory or its res/ subdirectory.\n"
            "Use fdb_converter to convert cdclient.fdb to SQLite first.");
    }

    // Open SQLite
    sqlite3* raw_db = nullptr;
    if (sqlite3_open_v2(db_path.string().c_str(), &raw_db,
                        SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Cannot open CDClient: " + db_path.string());
    }
    db_.reset(raw_db);

    std::cerr << "CDClient loaded: " << db_path << "\n";
}

bool CdClientDb::is_open() const {
    return db_ != nullptr;
}

std::vector<ZoneInfo> CdClientDb::get_zones() const {
    if (!db_) return {};

    std::vector<ZoneInfo> zones;
    auto stmt = prepare(db_.get(),
        "SELECT zoneID, zoneName FROM ZoneTable ORDER BY zoneID");

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        ZoneInfo z;
        z.zone_id = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 0));
        z.zone_name = col_text(stmt.get(), 1);
        zones.push_back(std::move(z));
    }

    return zones;
}

LotAsset CdClientDb::resolve_lot(uint32_t lot) const {
    LotAsset result;
    result.lot = lot;
    if (!db_) return result;

    // Look up physics asset — SimplePhysics (3) or ControllablePhysics (1).
    // Prefer type 3 (static) over type 1 by ordering.
    {
        auto stmt = prepare(db_.get(),
            "SELECT pc.physics_asset, cr.component_type, pc.\"static\" "
            "FROM PhysicsComponent pc "
            "INNER JOIN ComponentsRegistry cr ON cr.component_id = pc.id "
            "WHERE cr.id = ? AND cr.component_type IN (1, 3) "
            "ORDER BY cr.component_type DESC "
            "LIMIT 1");
        sqlite3_bind_int(stmt.get(), 1, static_cast<int>(lot));

        if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            result.physics_asset = col_text(stmt.get(), 0);
            result.physics_component_type = sqlite3_column_int(stmt.get(), 1);
            result.is_static = sqlite3_column_double(stmt.get(), 2) >= 1.0;
        }
    }

    // Look up render asset (component_type 2 = Render)
    {
        auto stmt = prepare(db_.get(),
            "SELECT rc.render_asset FROM RenderComponent rc "
            "INNER JOIN ComponentsRegistry cr ON cr.component_id = rc.id "
            "WHERE cr.id = ? AND cr.component_type = 2 "
            "LIMIT 1");
        sqlite3_bind_int(stmt.get(), 1, static_cast<int>(lot));

        if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            result.render_asset = col_text(stmt.get(), 0);
        }
    }

    return result;
}

std::unordered_map<uint32_t, LotAsset> CdClientDb::resolve_lots(
    const std::vector<uint32_t>& lots) const
{
    std::unordered_map<uint32_t, LotAsset> results;
    if (!db_ || lots.empty()) return results;

    // Deduplicate LOTs
    std::vector<uint32_t> unique_lots = lots;
    std::sort(unique_lots.begin(), unique_lots.end());
    unique_lots.erase(std::unique(unique_lots.begin(), unique_lots.end()),
                      unique_lots.end());

    auto phys_stmt = prepare(db_.get(),
        "SELECT pc.physics_asset, cr.component_type, pc.\"static\" "
        "FROM PhysicsComponent pc "
        "INNER JOIN ComponentsRegistry cr ON cr.component_id = pc.id "
        "WHERE cr.id = ? AND cr.component_type IN (1, 3) "
        "ORDER BY cr.component_type DESC "
        "LIMIT 1");

    auto render_stmt = prepare(db_.get(),
        "SELECT rc.render_asset FROM RenderComponent rc "
        "INNER JOIN ComponentsRegistry cr ON cr.component_id = rc.id "
        "WHERE cr.id = ? AND cr.component_type = 2 "
        "LIMIT 1");

    for (uint32_t lot : unique_lots) {
        LotAsset asset;
        asset.lot = lot;

        sqlite3_reset(phys_stmt.get());
        sqlite3_bind_int(phys_stmt.get(), 1, static_cast<int>(lot));
        if (sqlite3_step(phys_stmt.get()) == SQLITE_ROW) {
            asset.physics_asset = col_text(phys_stmt.get(), 0);
            asset.physics_component_type = sqlite3_column_int(phys_stmt.get(), 1);
            asset.is_static = sqlite3_column_double(phys_stmt.get(), 2) >= 1.0;
        }

        // Render
        sqlite3_reset(render_stmt.get());
        sqlite3_bind_int(render_stmt.get(), 1, static_cast<int>(lot));
        if (sqlite3_step(render_stmt.get()) == SQLITE_ROW) {
            asset.render_asset = col_text(render_stmt.get(), 0);
        }

        if (!asset.physics_asset.empty() || !asset.render_asset.empty()) {
            results[lot] = std::move(asset);
        }
    }

    return results;
}

} // namespace navmesh
