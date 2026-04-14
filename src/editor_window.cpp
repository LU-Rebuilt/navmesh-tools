// editor_window.cpp — EditorWindow implementation for the navmesh editor.

#include "editor_window.h"
#include "mset_writer.h"
#include "netdevil/zone/luz/luz_reader.h"

#include <DetourNavMesh.h>

#include "file_browser.h"

#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QDockWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QApplication>
#include <QLineEdit>
#include <QSettings>
#include <QCloseEvent>

#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace navmesh_editor {

EditorWindow::EditorWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Navmesh Editor");
    resize(1280, 800);

    // Central widget — 3D viewport
    glWidget_ = new NavmeshGLWidget(this);
    setCentralWidget(glWidget_);

    // ---- Menu bar ----

    auto* fileMenu = menuBar()->addMenu("&File");

    fileMenu->addAction("Set Client &Root...", QKeySequence("Ctrl+R"),
                        this, &EditorWindow::onSetClientRoot);

    fileMenu->addSeparator();

    fileMenu->addAction("&Open MSET...", QKeySequence::Open,
                        this, &EditorWindow::onLoadMset);

    fileMenu->addAction("&Save MSET...", QKeySequence::Save,
                        this, &EditorWindow::onSaveMset);

    fileMenu->addSeparator();

    fileMenu->addAction("&Quit", QKeySequence::Quit,
                        this, &QWidget::close);

    // Build menu
    auto* buildMenu = menuBar()->addMenu("&Build");

    buildMenu->addAction("&Generate Navmesh", QKeySequence("Ctrl+G"),
                         this, &EditorWindow::onGenerate);

    // View menu
    auto* viewMenu = menuBar()->addMenu("&View");

    auto* showSourceAct = viewMenu->addAction("Show &Source Geometry");
    showSourceAct->setCheckable(true);
    showSourceAct->setChecked(true);
    connect(showSourceAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showSourceGeo_ = checked;
        glWidget_->update();
    });

    auto* showNavAct = viewMenu->addAction("Show &Navmesh");
    showNavAct->setCheckable(true);
    showNavAct->setChecked(true);
    connect(showNavAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showNavmesh_ = checked;
        glWidget_->update();
    });

    viewMenu->addSeparator();

    auto* showWireAct = viewMenu->addAction("Show &Wireframe");
    showWireAct->setCheckable(true);
    showWireAct->setChecked(true);
    connect(showWireAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showWireframe = checked;
        glWidget_->update();
    });

    auto* showTerrainAct = viewMenu->addAction("Show &Terrain");
    showTerrainAct->setCheckable(true);
    showTerrainAct->setChecked(true);
    connect(showTerrainAct, &QAction::toggled, [this](bool checked) {
        // Toggle terrain visibility (index 0 if present)
        if (!geometry_.objects.empty() && geometry_.objects[0].source == "terrain") {
            glWidget_->setObjectVisible(0, checked);
        }
    });
    // Store so we can update it when terrain auto-hides
    terrainToggle_ = showTerrainAct;

    auto* showSolidAct = viewMenu->addAction("Show S&olid Fill");
    showSolidAct->setCheckable(true);
    showSolidAct->setChecked(true);
    connect(showSolidAct, &QAction::toggled, [this](bool checked) {
        glWidget_->showSolid = checked;
        glWidget_->update();
    });

    viewMenu->addSeparator();

    viewMenu->addAction("&Fit to Visible", QKeySequence("F"),
                        [this]() { glWidget_->fitToVisible(); });

    viewMenu->addAction("Fit to &Selected", QKeySequence("Shift+F"),
                        [this]() {
                            int idx = glWidget_->selectedIndex();
                            if (idx >= 0) glWidget_->fitToMesh(idx);
                        });

    // Help menu
    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&Controls", [this]() {
        QMessageBox::information(this, "Navmesh Editor — Controls",
            "<h3>Camera Controls</h3>"
            "<ul>"
            "<li><b>Left-drag</b> — Orbit camera</li>"
            "<li><b>Right-drag</b> — Pan camera</li>"
            "<li><b>Scroll wheel / Middle-drag</b> — Zoom in/out</li>"
            "</ul>"
            "<h3>Layers</h3>"
            "<ul>"
            "<li><b style='color:#999'>Grey</b> — Source geometry (terrain + HKX/NIF collision)</li>"
            "<li><b style='color:#44bb55'>Colored</b> — Navmesh polygons (per-tile coloring)</li>"
            "</ul>"
            "<h3>Workflow</h3>"
            "<ol>"
            "<li>File &gt; Set Client Root (point to LU client directory)</li>"
            "<li>Select a zone from the dropdown and click Load Zone</li>"
            "<li>Adjust Recast parameters in the side panel</li>"
            "<li>Build &gt; Generate Navmesh (Ctrl+G)</li>"
            "<li>File &gt; Save MSET to export for DarkflameServer</li>"
            "</ol>"
        );
    });
    helpMenu->addAction("&About", [this]() {
        QMessageBox::about(this, "About Navmesh Editor",
            "<b>Navmesh Editor</b><br>"
            "Part of the LU-Rebuilt project<br><br>"
            "Generates and visualizes Recast/Detour navigation meshes<br>"
            "for LEGO Universe zones. Output is DarkflameServer-compatible<br>"
            "MSET binary format.<br><br>"
            "Pipeline: LUZ &rarr; LVL scenes &rarr; CDClient LOT lookup<br>"
            "&rarr; HKX collision (preferred) / NIF mesh (fallback)");
    });

    // ---- Parameter dock (right) ----

    auto* paramDock = new QDockWidget("Zone && Parameters", this);
    paramDock->setWidget(createParamPanel());
    paramDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, paramDock);

    // ---- Object list dock (right, below params) ----

    auto* objDock = new QDockWidget("Objects", this);
    auto* objPanel = new QWidget;
    auto* objLayout = new QVBoxLayout(objPanel);
    objLayout->setContentsMargins(2, 2, 2, 2);

    objectList_ = new QListWidget;
    objectList_->setSelectionMode(QAbstractItemView::SingleSelection);
    objLayout->addWidget(objectList_);

    auto* deleteBtn = new QPushButton("Delete Selected");
    connect(deleteBtn, &QPushButton::clicked, this, &EditorWindow::onDeleteSelected);
    objLayout->addWidget(deleteBtn);

    objDock->setWidget(objPanel);
    objDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, objDock);
    tabifyDockWidget(paramDock, objDock);
    paramDock->raise(); // show params tab by default

    connect(objectList_, &QListWidget::currentRowChanged,
            this, &EditorWindow::onObjectListClicked);

    // ---- Console dock (bottom) ----

    auto* consoleDock = new QDockWidget("Console", this);
    console_ = new QTextEdit;
    console_->setReadOnly(true);
    console_->setFont(QFont("monospace", 9));
    console_->setMaximumHeight(200);
    consoleDock->setWidget(console_);
    consoleDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);

    // ---- Status bar ----

    statusLabel_ = new QLabel("Set client root to begin (File > Set Client Root)");
    statusBar()->addWidget(statusLabel_, 1);

    connect(glWidget_, &NavmeshGLWidget::statsChanged,
            this, &EditorWindow::onStatsChanged);
    connect(glWidget_, &NavmeshGLWidget::meshClicked,
            this, &EditorWindow::onObjectSelected);
    connect(glWidget_, &NavmeshGLWidget::contextRequested,
            this, &EditorWindow::onContextMenu);

    // Restore persisted settings from previous session
    restoreSettings();
}

EditorWindow::~EditorWindow() {
    if (navmesh_) {
        dtFreeNavMesh(navmesh_);
        navmesh_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Parameter panel
// ---------------------------------------------------------------------------

QWidget* EditorWindow::createParamPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);

    // ---- Zone selector group ----
    auto* zoneGroup = new QGroupBox("Zone");
    auto* zoneLayout = new QVBoxLayout(zoneGroup);

    auto* rootLayout = new QHBoxLayout;
    clientRootBtn_ = new QPushButton("Set Client Root...");
    connect(clientRootBtn_, &QPushButton::clicked, this, &EditorWindow::onSetClientRoot);
    rootLayout->addWidget(clientRootBtn_);
    zoneLayout->addLayout(rootLayout);

    zoneCombo_ = new QComboBox;
    zoneCombo_->setEnabled(false);
    zoneCombo_->setPlaceholderText("No client root set");
    zoneCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    zoneLayout->addWidget(zoneCombo_);

    loadZoneBtn_ = new QPushButton("Load Zone");
    loadZoneBtn_->setEnabled(false);
    connect(loadZoneBtn_, &QPushButton::clicked, [this]() {
        int idx = zoneCombo_->currentIndex();
        if (idx >= 0) onZoneSelected(idx);
    });
    zoneLayout->addWidget(loadZoneBtn_);

    layout->addWidget(zoneGroup);

    // ---- Recast parameters ----

    navmesh::BuildConfig defaults;

    // Agent group
    auto* agentGroup = new QGroupBox("Agent");
    auto* agentForm = new QFormLayout(agentGroup);

    agentHeightSpin_ = new QDoubleSpinBox;
    agentHeightSpin_->setRange(0.1, 10.0);
    agentHeightSpin_->setSingleStep(0.1);
    agentHeightSpin_->setValue(defaults.agent_height);
    agentForm->addRow("Height:", agentHeightSpin_);

    agentRadiusSpin_ = new QDoubleSpinBox;
    agentRadiusSpin_->setRange(0.1, 5.0);
    agentRadiusSpin_->setSingleStep(0.1);
    agentRadiusSpin_->setValue(defaults.agent_radius);
    agentForm->addRow("Radius:", agentRadiusSpin_);

    agentMaxClimbSpin_ = new QDoubleSpinBox;
    agentMaxClimbSpin_->setRange(0.0, 5.0);
    agentMaxClimbSpin_->setSingleStep(0.1);
    agentMaxClimbSpin_->setValue(defaults.agent_max_climb);
    agentForm->addRow("Max Climb:", agentMaxClimbSpin_);

    agentMaxSlopeSpin_ = new QDoubleSpinBox;
    agentMaxSlopeSpin_->setRange(0.0, 90.0);
    agentMaxSlopeSpin_->setSingleStep(1.0);
    agentMaxSlopeSpin_->setValue(defaults.agent_max_slope);
    agentMaxSlopeSpin_->setSuffix("\u00B0");
    agentForm->addRow("Max Slope:", agentMaxSlopeSpin_);

    layout->addWidget(agentGroup);

    // Voxel group
    auto* voxelGroup = new QGroupBox("Voxelization");
    auto* voxelForm = new QFormLayout(voxelGroup);

    cellSizeSpin_ = new QDoubleSpinBox;
    cellSizeSpin_->setRange(0.05, 5.0);
    cellSizeSpin_->setSingleStep(0.05);
    cellSizeSpin_->setDecimals(2);
    cellSizeSpin_->setValue(defaults.cell_size);
    voxelForm->addRow("Cell Size:", cellSizeSpin_);

    cellHeightSpin_ = new QDoubleSpinBox;
    cellHeightSpin_->setRange(0.05, 5.0);
    cellHeightSpin_->setSingleStep(0.05);
    cellHeightSpin_->setDecimals(2);
    cellHeightSpin_->setValue(defaults.cell_height);
    voxelForm->addRow("Cell Height:", cellHeightSpin_);

    tileSizeSpin_ = new QSpinBox;
    tileSizeSpin_->setRange(8, 256);
    tileSizeSpin_->setValue(defaults.tile_size);
    voxelForm->addRow("Tile Size:", tileSizeSpin_);

    layout->addWidget(voxelGroup);

    // Region group
    auto* regionGroup = new QGroupBox("Region");
    auto* regionForm = new QFormLayout(regionGroup);

    regionMinSizeSpin_ = new QSpinBox;
    regionMinSizeSpin_->setRange(1, 100);
    regionMinSizeSpin_->setValue(defaults.region_min_size);
    regionForm->addRow("Min Size:", regionMinSizeSpin_);

    regionMergeSizeSpin_ = new QSpinBox;
    regionMergeSizeSpin_->setRange(1, 100);
    regionMergeSizeSpin_->setValue(defaults.region_merge_size);
    regionForm->addRow("Merge Size:", regionMergeSizeSpin_);

    layout->addWidget(regionGroup);

    // Edge group
    auto* edgeGroup = new QGroupBox("Edge");
    auto* edgeForm = new QFormLayout(edgeGroup);

    edgeMaxLenSpin_ = new QDoubleSpinBox;
    edgeMaxLenSpin_->setRange(0.0, 100.0);
    edgeMaxLenSpin_->setSingleStep(1.0);
    edgeMaxLenSpin_->setValue(defaults.edge_max_len);
    edgeForm->addRow("Max Length:", edgeMaxLenSpin_);

    edgeMaxErrorSpin_ = new QDoubleSpinBox;
    edgeMaxErrorSpin_->setRange(0.0, 10.0);
    edgeMaxErrorSpin_->setSingleStep(0.1);
    edgeMaxErrorSpin_->setValue(defaults.edge_max_error);
    edgeForm->addRow("Max Error:", edgeMaxErrorSpin_);

    maxVertsPerPolySpin_ = new QSpinBox;
    maxVertsPerPolySpin_->setRange(3, 12);
    maxVertsPerPolySpin_->setValue(defaults.max_verts_per_poly);
    edgeForm->addRow("Max Verts/Poly:", maxVertsPerPolySpin_);

    layout->addWidget(edgeGroup);

    // Detail group
    auto* detailGroup = new QGroupBox("Detail Mesh");
    auto* detailForm = new QFormLayout(detailGroup);

    detailSampleDistSpin_ = new QDoubleSpinBox;
    detailSampleDistSpin_->setRange(0.0, 50.0);
    detailSampleDistSpin_->setSingleStep(1.0);
    detailSampleDistSpin_->setValue(defaults.detail_sample_dist);
    detailForm->addRow("Sample Dist:", detailSampleDistSpin_);

    detailSampleMaxErrorSpin_ = new QDoubleSpinBox;
    detailSampleMaxErrorSpin_->setRange(0.0, 10.0);
    detailSampleMaxErrorSpin_->setSingleStep(0.5);
    detailSampleMaxErrorSpin_->setValue(defaults.detail_sample_max_error);
    detailForm->addRow("Sample Max Error:", detailSampleMaxErrorSpin_);

    layout->addWidget(detailGroup);

    // Generate button
    auto* genBtn = new QPushButton("Generate Navmesh");
    genBtn->setStyleSheet("QPushButton { padding: 8px; font-weight: bold; }");
    connect(genBtn, &QPushButton::clicked, this, &EditorWindow::onGenerate);
    layout->addWidget(genBtn);

    layout->addStretch();
    return panel;
}

navmesh::BuildConfig EditorWindow::currentConfig() const {
    navmesh::BuildConfig cfg;
    cfg.cell_size       = static_cast<float>(cellSizeSpin_->value());
    cfg.cell_height     = static_cast<float>(cellHeightSpin_->value());
    cfg.agent_height    = static_cast<float>(agentHeightSpin_->value());
    cfg.agent_radius    = static_cast<float>(agentRadiusSpin_->value());
    cfg.agent_max_climb = static_cast<float>(agentMaxClimbSpin_->value());
    cfg.agent_max_slope = static_cast<float>(agentMaxSlopeSpin_->value());
    cfg.tile_size       = tileSizeSpin_->value();
    cfg.edge_max_len    = static_cast<float>(edgeMaxLenSpin_->value());
    cfg.edge_max_error  = static_cast<float>(edgeMaxErrorSpin_->value());
    cfg.region_min_size = regionMinSizeSpin_->value();
    cfg.region_merge_size = regionMergeSizeSpin_->value();
    cfg.detail_sample_dist      = static_cast<float>(detailSampleDistSpin_->value());
    cfg.detail_sample_max_error = static_cast<float>(detailSampleMaxErrorSpin_->value());
    cfg.max_verts_per_poly = maxVertsPerPolySpin_->value();
    return cfg;
}

// ---------------------------------------------------------------------------
// Zone list
// ---------------------------------------------------------------------------

void EditorWindow::populateZoneList() {
    namespace fs = std::filesystem;
    zoneCombo_->clear();

    auto all_zones = cdclient_.get_zones();

    // Scan res/maps/ for all .luz files and build a set of world_ids present on disk
    std::unordered_set<uint32_t> on_disk;
    fs::path maps_dir = cdclient_.client_root() / "res" / "maps";
    if (fs::exists(maps_dir)) {
        for (const auto& entry : fs::recursive_directory_iterator(maps_dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (ext != ".luz") continue;

            try {
                std::ifstream f(entry.path(), std::ios::binary | std::ios::ate);
                auto sz = f.tellg(); f.seekg(0);
                std::vector<uint8_t> data(static_cast<size_t>(sz));
                f.read(reinterpret_cast<char*>(data.data()), sz);
                auto luz = lu::assets::luz_parse(data);
                on_disk.insert(luz.world_id);
            } catch (...) {}
        }
    }

    // Only show zones whose LUZ files exist on disk
    zones_.clear();
    for (auto& z : all_zones) {
        if (on_disk.count(z.zone_id)) {
            zones_.push_back(std::move(z));
        }
    }

    for (const auto& z : zones_) {
        QString label = QString("%1 — %2")
            .arg(z.zone_id)
            .arg(z.zone_name.empty()
                ? QString("Zone %1").arg(z.zone_id)
                : QString::fromStdString(z.zone_name));
        zoneCombo_->addItem(label);
    }

    zoneCombo_->setEnabled(!zones_.empty());
    loadZoneBtn_->setEnabled(!zones_.empty());

    if (zones_.empty()) {
        zoneCombo_->setPlaceholderText("No zones found on disk");
    } else {
        zoneCombo_->setPlaceholderText(
            QString("Select zone (%1 available)").arg(zones_.size()));
    }

    statusBar()->showMessage(
        QString("CDClient loaded: %1 zones on disk (of %2 in database)")
            .arg(zones_.size()).arg(all_zones.size()), 5000);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool EditorWindow::setClientRoot(const std::string& path) {
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        cdclient_.open(std::filesystem::path(path));
        QApplication::restoreOverrideCursor();

        // Update UI to reflect the loaded client root
        QString rootName = QString::fromStdString(
            cdclient_.client_root().filename().string());
        clientRootBtn_->setText(rootName.isEmpty() ? "Change Client Root..." : rootName);
        clientRootBtn_->setToolTip(QString::fromStdString(
            cdclient_.client_root().string()));

        populateZoneList();
        setWindowTitle(QString("Navmesh Editor — %1")
            .arg(QString::fromStdString(
                cdclient_.client_root().string())));
        saveSettings();
        return true;
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, "CDClient Error",
            QString("Failed to load CDClient from:\n%1\n\n%2")
                .arg(QString::fromStdString(path))
                .arg(QString::fromStdString(e.what())));
        return false;
    }
}

bool EditorWindow::loadZone(uint32_t zone_id) {
    if (!cdclient_.is_open()) {
        QMessageBox::warning(this, "No CDClient",
            "Set the client root directory first.");
        return false;
    }

    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        geometry_ = navmesh::load_zone_geometry(cdclient_, zone_id);
        QApplication::restoreOverrideCursor();

        glWidget_->loadSourceGeometry(geometry_);
        currentZoneId_ = zone_id;
        populateObjectList();

        // Sync terrain toggle with auto-hide state
        if (terrainToggle_ && !geometry_.objects.empty() &&
            geometry_.objects[0].source == "terrain") {
            bool terrainVis = glWidget_->isObjectVisible(0);
            terrainToggle_->setChecked(terrainVis);
            if (!terrainVis) log("Terrain auto-hidden (far from objects)");
        }

        log(QString("Zone %1 loaded: %2 objects, %3 terrain + %4 HKX + %5 NIF + %6 primitive tris")
            .arg(zone_id).arg(geometry_.objects.size())
            .arg(geometry_.terrain_tris).arg(geometry_.hkx_tris)
            .arg(geometry_.nif_tris).arg(geometry_.prim_tris));

        setWindowTitle(QString("Navmesh Editor — Zone %1").arg(zone_id));
        return true;
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, "Load Error",
            QString("Failed to load zone %1:\n%2")
                .arg(zone_id)
                .arg(QString::fromStdString(e.what())));
        return false;
    }
}

bool EditorWindow::loadMset(const std::string& path) {
    dtNavMesh* mesh = navmesh::read_mset(std::filesystem::path(path));
    if (!mesh) {
        QMessageBox::warning(this, "Load Error",
            QString("Failed to load MSET file:\n%1")
                .arg(QString::fromStdString(path)));
        return false;
    }

    if (navmesh_) dtFreeNavMesh(navmesh_);
    navmesh_ = mesh;

    glWidget_->loadNavmesh(navmesh_);

    setWindowTitle(QString("Navmesh Editor — %1")
        .arg(QString::fromStdString(
            std::filesystem::path(path).filename().string())));
    return true;
}

void EditorWindow::onSetClientRoot() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select LEGO Universe Client Root Directory",
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (!dir.isEmpty()) {
        setClientRoot(dir.toStdString());
    }
}

void EditorWindow::onZoneSelected(int index) {
    if (index < 0 || index >= static_cast<int>(zones_.size())) return;
    loadZone(zones_[index].zone_id);
}

void EditorWindow::onLoadMset() {
    QString path = qt_common::FileBrowserDialog::getOpenFileName(
        this, "Open MSET Navmesh File", QString(),
        "MSET Files (*.bin);;All Files (*)");
    if (!path.isEmpty()) {
        loadMset(path.toStdString());
    }
}

void EditorWindow::onSaveMset() {
    if (!navmesh_) {
        QMessageBox::warning(this, "No Navmesh",
            "Generate or load a navmesh first.");
        return;
    }

    QString defaultName;
    if (currentZoneId_ > 0) {
        defaultName = QString("%1.bin").arg(currentZoneId_);
    }

    QString path = qt_common::FileBrowserDialog::getSaveFileName(
        this, "Save MSET Navmesh File", defaultName,
        "MSET Files (*.bin);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        navmesh::write_mset(navmesh_, std::filesystem::path(path.toStdString()));
        statusBar()->showMessage(
            QString("Saved: %1").arg(path), 5000);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Save Error",
            QString("Failed to save MSET:\n%1")
                .arg(QString::fromStdString(e.what())));
    }
}

void EditorWindow::onGenerate() {
    if (geometry_.objects.empty()) {
        QMessageBox::warning(this, "No Geometry",
            "Load a zone first (select zone and click Load Zone).");
        return;
    }

    auto cfg = currentConfig();

    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        dtNavMesh* mesh = navmesh::build_navmesh(geometry_, cfg);
        QApplication::restoreOverrideCursor();

        if (navmesh_) dtFreeNavMesh(navmesh_);
        navmesh_ = mesh;

        glWidget_->loadNavmesh(navmesh_);

        auto nav_stats = navmesh::get_navmesh_stats(navmesh_);
        statusBar()->showMessage(
            QString("Generated: %1 tiles, %2 polys, %3 verts")
                .arg(nav_stats.total_tiles)
                .arg(nav_stats.total_polys)
                .arg(nav_stats.total_verts),
            10000);
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, "Build Error",
            QString("Navmesh generation failed:\n%1")
                .arg(QString::fromStdString(e.what())));
    }
}

void EditorWindow::onStatsChanged() {
    auto& s = glWidget_->stats();
    QString text;

    if (s.source_objects > 0) {
        text = QString("Source: %1 objects, %2 tris (%3t + %4h + %5n + %6p)")
            .arg(s.source_objects)
            .arg(s.source_triangles)
            .arg(s.terrain_tris)
            .arg(s.hkx_tris)
            .arg(s.nif_tris)
            .arg(s.prim_tris);
    }

    if (s.nav_tiles > 0) {
        if (!text.isEmpty()) text += "  |  ";
        text += QString("Navmesh: %1 tiles, %2 polys, %3 verts")
            .arg(s.nav_tiles)
            .arg(s.nav_polys)
            .arg(s.nav_verts);
    }

    if (text.isEmpty()) {
        text = cdclient_.is_open()
            ? "CDClient loaded — select a zone"
            : "Set client root to begin (File > Set Client Root)";
    }

    statusLabel_->setText(text);
}

// ---------------------------------------------------------------------------
// Object list / selection / delete
// ---------------------------------------------------------------------------

void EditorWindow::populateObjectList() {
    objectList_->clear();
    for (size_t i = 0; i < geometry_.objects.size(); ++i) {
        const auto& obj = geometry_.objects[i];
        QString label = QString("[%1] %2 — %3 tris @ (%4, %5, %6)")
            .arg(i)
            .arg(obj.label.empty()
                ? QString::fromStdString(obj.source + " " + obj.asset_path)
                : QString::fromStdString(obj.label))
            .arg(obj.triangles.size())
            .arg(static_cast<double>(obj.pos_x), 0, 'f', 1)
            .arg(static_cast<double>(obj.pos_y), 0, 'f', 1)
            .arg(static_cast<double>(obj.pos_z), 0, 'f', 1);
        objectList_->addItem(label);
    }

    // Log all objects to console
    log(QString("--- %1 objects loaded ---").arg(geometry_.objects.size()));
    for (size_t i = 0; i < geometry_.objects.size(); ++i) {
        const auto& obj = geometry_.objects[i];
        log(QString("  [%1] LOT %2 | %3 | %4 | %5 tris | pos (%6, %7, %8) | scale %9")
            .arg(i).arg(obj.lot)
            .arg(QString::fromStdString(obj.source))
            .arg(QString::fromStdString(obj.asset_path))
            .arg(obj.triangles.size())
            .arg(static_cast<double>(obj.pos_x), 0, 'f', 1)
            .arg(static_cast<double>(obj.pos_y), 0, 'f', 1)
            .arg(static_cast<double>(obj.pos_z), 0, 'f', 1)
            .arg(static_cast<double>(obj.scale), 0, 'f', 2));
    }
}

void EditorWindow::onObjectSelected(int index) {
    // Sync GL widget selection → object list
    if (index >= 0 && index < objectList_->count()) {
        objectList_->setCurrentRow(index);
        const auto& obj = geometry_.objects[index];
        log(QString("Selected [%1] LOT %2 — %3 — %4 tris")
            .arg(index).arg(obj.lot)
            .arg(QString::fromStdString(obj.label.empty() ? obj.source : obj.label))
            .arg(obj.triangles.size()));
    } else {
        objectList_->clearSelection();
    }
}

void EditorWindow::onObjectListClicked(int row) {
    // Sync object list click → GL widget selection
    glWidget_->setSelectedIndex(row);
}

void EditorWindow::onDeleteSelected() {
    int idx = glWidget_->selectedIndex();
    if (idx < 0 || idx >= static_cast<int>(geometry_.objects.size())) {
        log("No object selected to delete.");
        return;
    }

    const auto& obj = geometry_.objects[idx];
    log(QString("Deleted [%1] LOT %2 — %3")
        .arg(idx).arg(obj.lot)
        .arg(QString::fromStdString(obj.label.empty() ? obj.source : obj.label)));

    // Remove from geometry and GL widget
    geometry_.objects.erase(geometry_.objects.begin() + idx);
    glWidget_->deleteSelected();
    populateObjectList();
}

void EditorWindow::log(const QString& msg) {
    if (console_) {
        console_->append(msg);
    }
}

void EditorWindow::onContextMenu(int objectIndex, QPoint globalPos) {
    QMenu menu;

    if (objectIndex >= 0 && objectIndex < static_cast<int>(geometry_.objects.size())) {
        const auto& obj = geometry_.objects[objectIndex];
        menu.addAction(QString("LOT %1 — %2")
            .arg(obj.lot).arg(QString::fromStdString(obj.label)))->setEnabled(false);
        menu.addSeparator();

        menu.addAction("Fit to Object", [this, objectIndex]() {
            glWidget_->fitToMesh(objectIndex);
        });

        bool vis = glWidget_->isObjectVisible(objectIndex);
        menu.addAction(vis ? "Hide Object" : "Show Object", [this, objectIndex, vis]() {
            glWidget_->setObjectVisible(objectIndex, !vis);
        });

        menu.addAction("Delete Object", [this, objectIndex]() {
            glWidget_->setSelectedIndex(objectIndex);
            onDeleteSelected();
        });
    } else {
        menu.addAction("(no object under cursor)")->setEnabled(false);
    }

    menu.addSeparator();
    menu.addAction("Fit to Visible", [this]() { glWidget_->fitToVisible(); });

    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------

void EditorWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void EditorWindow::saveSettings() {
    QSettings s;

    // Window state
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());

    // Client root
    if (cdclient_.is_open()) {
        s.setValue("client_root", QString::fromStdString(
            cdclient_.client_root().string()));
    }

    // Last selected zone
    if (currentZoneId_ > 0) {
        s.setValue("last_zone_id", currentZoneId_);
    }
    s.setValue("zone_combo_index", zoneCombo_->currentIndex());

    // Recast parameters
    s.beginGroup("recast");
    s.setValue("cell_size", cellSizeSpin_->value());
    s.setValue("cell_height", cellHeightSpin_->value());
    s.setValue("agent_height", agentHeightSpin_->value());
    s.setValue("agent_radius", agentRadiusSpin_->value());
    s.setValue("agent_max_climb", agentMaxClimbSpin_->value());
    s.setValue("agent_max_slope", agentMaxSlopeSpin_->value());
    s.setValue("tile_size", tileSizeSpin_->value());
    s.setValue("edge_max_len", edgeMaxLenSpin_->value());
    s.setValue("edge_max_error", edgeMaxErrorSpin_->value());
    s.setValue("region_min_size", regionMinSizeSpin_->value());
    s.setValue("region_merge_size", regionMergeSizeSpin_->value());
    s.setValue("detail_sample_dist", detailSampleDistSpin_->value());
    s.setValue("detail_sample_max_error", detailSampleMaxErrorSpin_->value());
    s.setValue("max_verts_per_poly", maxVertsPerPolySpin_->value());
    s.endGroup();
}

void EditorWindow::restoreSettings() {
    QSettings s;

    // Window state
    if (s.contains("window/geometry")) {
        restoreGeometry(s.value("window/geometry").toByteArray());
    }
    if (s.contains("window/state")) {
        restoreState(s.value("window/state").toByteArray());
    }

    // Recast parameters — restore before loading zones so they're ready
    if (s.childGroups().contains("recast")) {
        s.beginGroup("recast");
        if (s.contains("cell_size"))       cellSizeSpin_->setValue(s.value("cell_size").toDouble());
        if (s.contains("cell_height"))     cellHeightSpin_->setValue(s.value("cell_height").toDouble());
        if (s.contains("agent_height"))    agentHeightSpin_->setValue(s.value("agent_height").toDouble());
        if (s.contains("agent_radius"))    agentRadiusSpin_->setValue(s.value("agent_radius").toDouble());
        if (s.contains("agent_max_climb")) agentMaxClimbSpin_->setValue(s.value("agent_max_climb").toDouble());
        if (s.contains("agent_max_slope")) agentMaxSlopeSpin_->setValue(s.value("agent_max_slope").toDouble());
        if (s.contains("tile_size"))       tileSizeSpin_->setValue(s.value("tile_size").toInt());
        if (s.contains("edge_max_len"))    edgeMaxLenSpin_->setValue(s.value("edge_max_len").toDouble());
        if (s.contains("edge_max_error"))  edgeMaxErrorSpin_->setValue(s.value("edge_max_error").toDouble());
        if (s.contains("region_min_size")) regionMinSizeSpin_->setValue(s.value("region_min_size").toInt());
        if (s.contains("region_merge_size")) regionMergeSizeSpin_->setValue(s.value("region_merge_size").toInt());
        if (s.contains("detail_sample_dist")) detailSampleDistSpin_->setValue(s.value("detail_sample_dist").toDouble());
        if (s.contains("detail_sample_max_error")) detailSampleMaxErrorSpin_->setValue(s.value("detail_sample_max_error").toDouble());
        if (s.contains("max_verts_per_poly")) maxVertsPerPolySpin_->setValue(s.value("max_verts_per_poly").toInt());
        s.endGroup();
    }

    // Client root — reopen CDClient and repopulate zone list
    QString clientRoot = s.value("client_root").toString();
    if (!clientRoot.isEmpty()) {
        if (setClientRoot(clientRoot.toStdString())) {
            // Restore zone combo selection
            int comboIdx = s.value("zone_combo_index", -1).toInt();
            if (comboIdx >= 0 && comboIdx < zoneCombo_->count()) {
                zoneCombo_->setCurrentIndex(comboIdx);
            }
        }
    }
}

} // namespace navmesh_editor
