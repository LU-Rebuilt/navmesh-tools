// editor_window.h — QMainWindow for the navmesh editor.
//
// Provides client root selection (auto-finds CDClient), zone picker from
// ZoneTable, navmesh generation with tunable Recast parameters, MSET
// import/export, and a 3D viewport.
#pragma once

#include "navmesh_gl_widget.h"
#include "cdclient_db.h"
#include "recast_builder.h"
#include "zone_geometry.h"

#include <QMainWindow>
#include <QCloseEvent>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QTextEdit>
#include <QAction>
#include <QMenu>

struct dtNavMesh;

namespace navmesh_editor {

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorWindow(QWidget* parent = nullptr);
    ~EditorWindow();

    // Set client root and load CDClient database.
    bool setClientRoot(const std::string& path);

    // Load a specific zone (client root must be set first).
    bool loadZone(uint32_t zone_id);

    // Load an existing MSET navmesh file.
    bool loadMset(const std::string& path);

private slots:
    void onSetClientRoot();
    void onZoneSelected(int index);
    void onLoadMset();
    void onSaveMset();
    void onGenerate();
    void onStatsChanged();
    void onObjectSelected(int index);
    void onObjectListClicked(int row);
    void onDeleteSelected();
    void onContextMenu(int objectIndex, QPoint globalPos);

private:
    void closeEvent(QCloseEvent* event) override;

    // Read current Recast config from the UI spinboxes.
    navmesh::BuildConfig currentConfig() const;

    // Create the parameter panel dock widget.
    QWidget* createParamPanel();

    // Populate zone combo box from CDClient ZoneTable.
    void populateZoneList();

    // Populate the object list widget from current geometry.
    void populateObjectList();

    // Log a message to the console panel.
    void log(const QString& msg);

    // Persist/restore settings via QSettings.
    void saveSettings();
    void restoreSettings();

    NavmeshGLWidget* glWidget_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    // Client + CDClient state
    navmesh::CdClientDb cdclient_;
    std::vector<navmesh::ZoneInfo> zones_;

    // Current loaded state
    navmesh::ZoneGeometry geometry_;
    dtNavMesh* navmesh_ = nullptr;
    uint32_t currentZoneId_ = 0;

    // Zone selector widgets
    QPushButton* clientRootBtn_ = nullptr;
    QComboBox* zoneCombo_ = nullptr;
    QPushButton* loadZoneBtn_ = nullptr;

    // Object list and console
    QListWidget* objectList_ = nullptr;
    QTextEdit* console_ = nullptr;
    QAction* terrainToggle_ = nullptr;

    // Recast parameter spinboxes
    QDoubleSpinBox* cellSizeSpin_ = nullptr;
    QDoubleSpinBox* cellHeightSpin_ = nullptr;
    QDoubleSpinBox* agentHeightSpin_ = nullptr;
    QDoubleSpinBox* agentRadiusSpin_ = nullptr;
    QDoubleSpinBox* agentMaxClimbSpin_ = nullptr;
    QDoubleSpinBox* agentMaxSlopeSpin_ = nullptr;
    QSpinBox* tileSizeSpin_ = nullptr;
    QDoubleSpinBox* edgeMaxLenSpin_ = nullptr;
    QDoubleSpinBox* edgeMaxErrorSpin_ = nullptr;
    QSpinBox* regionMinSizeSpin_ = nullptr;
    QSpinBox* regionMergeSizeSpin_ = nullptr;
    QDoubleSpinBox* detailSampleDistSpin_ = nullptr;
    QDoubleSpinBox* detailSampleMaxErrorSpin_ = nullptr;
    QSpinBox* maxVertsPerPolySpin_ = nullptr;
};

} // namespace navmesh_editor
