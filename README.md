# navmesh-tools

Qt6/OpenGL navmesh editor for LEGO Universe zones.

> **Status:** Early work in progress. The editor can load and display zone geometry but there is no interactive editing yet. Expect significant changes.

> **Note:** This project was developed with significant AI assistance (Claude by Anthropic). All code has been reviewed and validated by the project maintainer, but AI-generated code may contain subtle issues. Contributions and reviews are welcome.

Part of the [LU-Rebuilt](https://github.com/LU-Rebuilt) project.

## Usage

```
navmesh_editor
```

**Features:**
- Zone picker from CDClient database
- Loads zone terrain, HKX collision, and NIF geometry
- Recast/Detour navmesh generation with configurable parameters
- MSET binary format output for DarkflameServer
- Object list with visibility toggles
- Navmesh overlay rendering

## Building

```bash
cmake -B build
cmake --build build -j$(nproc)
```

For local development:

```bash
cmake -B build -DFETCHCONTENT_SOURCE_DIR_LU_ASSETS=/path/to/local/lu-assets \
               -DFETCHCONTENT_SOURCE_DIR_TOOL_COMMON=/path/to/local/tool-common
```

## Acknowledgments

Format parsers built from:
- **[DarkflameServer](https://github.com/DarkflameServer/DarkflameServer)** — MSET format reference (dNavMesh.cpp), agent profile parameters
- **[lcdr/lu_formats](https://github.com/lcdr/lu_formats)** — Kaitai Struct LUZ/LVL format definitions
- **[nif.xml](https://github.com/niftools/nifxml)** — NIF geometry format definitions
- **[HKXDocs](https://github.com/SimonNitzsche/HKXDocs)** — HKX collision format documentation
- **Ghidra reverse engineering** of the original LEGO Universe client binary

Third-party libraries:
- **[Recast/Detour](https://github.com/recastnavigation/recastnavigation)** v1.6.0 — Navigation mesh generation (zlib license)

## License

[GNU Affero General Public License v3.0](https://www.gnu.org/licenses/agpl-3.0.html) (AGPLv3)

