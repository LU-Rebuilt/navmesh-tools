// Navmesh Editor
// Graphical tool for generating, viewing, and editing Recast/Detour navigation
// meshes for LEGO Universe zones. Outputs DarkflameServer-compatible MSET .bin.
//
// Usage:
//   navmesh_editor                         — launch empty
//   navmesh_editor <client_root>           — set client root, show zone picker
//   navmesh_editor <client_root> <zone_id> — load zone immediately
//   navmesh_editor --mset <file.bin>       — load existing MSET file

#include "editor_window.h"

#include <QApplication>
#include <QSurfaceFormat>

#include <cstring>

int main(int argc, char* argv[]) {
    // Request an OpenGL compatibility profile for legacy fixed-function pipeline
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setOrganizationName("LU-Rebuilt");
    app.setApplicationName("NavmeshEditor");

    navmesh_editor::EditorWindow window;
    window.show();

    // Handle command-line arguments
    if (argc >= 3 && std::strcmp(argv[1], "--mset") == 0) {
        window.loadMset(argv[2]);
    } else if (argc >= 3) {
        // navmesh_editor <client_root> <zone_id>
        window.setClientRoot(argv[1]);
        uint32_t zoneId = static_cast<uint32_t>(std::stoul(argv[2]));
        window.loadZone(zoneId);
    } else if (argc >= 2 && std::strcmp(argv[1], "--mset") != 0) {
        // navmesh_editor <client_root>
        window.setClientRoot(argv[1]);
    }

    return app.exec();
}
