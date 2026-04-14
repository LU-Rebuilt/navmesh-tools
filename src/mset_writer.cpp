#include "mset_writer.h"

#include <DetourNavMesh.h>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace navmesh {

// ---------------------------------------------------------------------------
// Write MSET
// ---------------------------------------------------------------------------

void write_mset(const dtNavMesh* navmesh, const std::filesystem::path& output_path) {
    if (!navmesh) throw std::runtime_error("Null navmesh");

    std::ofstream f(output_path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write: " + output_path.string());

    // Count tiles
    int num_tiles = 0;
    for (int i = 0; i < navmesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = navmesh->getTile(i);
        if (!tile || !tile->header || !tile->dataSize) continue;
        ++num_tiles;
    }

    // Write header
    NavMeshSetHeader header;
    header.magic = NAVMESHSET_MAGIC;
    header.version = NAVMESHSET_VERSION;
    header.num_tiles = num_tiles;
    f.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write dtNavMeshParams
    const dtNavMeshParams* params = navmesh->getParams();
    f.write(reinterpret_cast<const char*>(params), sizeof(dtNavMeshParams));

    // Write tiles
    for (int i = 0; i < navmesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = navmesh->getTile(i);
        if (!tile || !tile->header || !tile->dataSize) continue;

        dtTileRef ref = navmesh->getTileRef(tile);
        int32_t data_size = tile->dataSize;

        f.write(reinterpret_cast<const char*>(&ref), sizeof(ref));
        f.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
        f.write(reinterpret_cast<const char*>(tile->data), data_size);
    }
}

// ---------------------------------------------------------------------------
// Read MSET
// ---------------------------------------------------------------------------

dtNavMesh* read_mset(const std::filesystem::path& input_path) {
    std::ifstream f(input_path, std::ios::binary);
    if (!f) return nullptr;

    NavMeshSetHeader header;
    f.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.magic != NAVMESHSET_MAGIC || header.version != NAVMESHSET_VERSION)
        return nullptr;

    dtNavMeshParams params;
    f.read(reinterpret_cast<char*>(&params), sizeof(params));

    dtNavMesh* mesh = dtAllocNavMesh();
    if (!mesh) return nullptr;

    dtStatus status = mesh->init(&params);
    if (dtStatusFailed(status)) {
        dtFreeNavMesh(mesh);
        return nullptr;
    }

    for (int i = 0; i < header.num_tiles; ++i) {
        dtTileRef ref;
        int32_t data_size;
        f.read(reinterpret_cast<char*>(&ref), sizeof(ref));
        f.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));

        if (data_size <= 0) continue;

        unsigned char* tile_data = static_cast<unsigned char*>(dtAlloc(data_size, DT_ALLOC_PERM));
        if (!tile_data) continue;
        f.read(reinterpret_cast<char*>(tile_data), data_size);

        mesh->addTile(tile_data, data_size, DT_TILE_FREE_DATA, ref, nullptr);
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// Read MSET raw (for inspection)
// ---------------------------------------------------------------------------

MsetFile read_mset_raw(const std::filesystem::path& input_path) {
    std::ifstream f(input_path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read: " + input_path.string());

    MsetFile mset;
    f.read(reinterpret_cast<char*>(&mset.header), sizeof(mset.header));
    if (mset.header.magic != NAVMESHSET_MAGIC)
        throw std::runtime_error("Not an MSET file: " + input_path.string());

    // Skip dtNavMeshParams
    dtNavMeshParams params;
    f.read(reinterpret_cast<char*>(&params), sizeof(params));

    for (int i = 0; i < mset.header.num_tiles; ++i) {
        MsetTile tile;
        int32_t data_size;
        f.read(reinterpret_cast<char*>(&tile.tile_ref), sizeof(tile.tile_ref));
        f.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));

        if (data_size > 0) {
            tile.data.resize(data_size);
            f.read(reinterpret_cast<char*>(tile.data.data()), data_size);
        }
        mset.tiles.push_back(std::move(tile));
    }

    return mset;
}

} // namespace navmesh
