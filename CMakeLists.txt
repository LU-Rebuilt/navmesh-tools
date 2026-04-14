cmake_minimum_required(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(navmesh-tools VERSION 0.1.0 LANGUAGES CXX DESCRIPTION "LEGO Universe navmesh generator and editor")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(lu_assets
    GIT_REPOSITORY https://github.com/LU-Rebuilt/lu-assets.git
    GIT_TAG main
)
FetchContent_MakeAvailable(lu_assets)

FetchContent_Declare(tool_common
    GIT_REPOSITORY https://github.com/LU-Rebuilt/tool-common.git
    GIT_TAG main
)
FetchContent_MakeAvailable(tool_common)

if(NOT TARGET SQLite::SQLite3)
    find_package(SQLite3 REQUIRED)
endif()

FetchContent_Declare(recastnavigation
    GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
    GIT_TAG v1.6.0
)
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(recastnavigation)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(OpenGL REQUIRED)
set(CMAKE_AUTOMOC ON)

add_executable(navmesh_editor
    src/navmesh_editor.cpp
    src/editor_window.cpp src/editor_window.h
    src/navmesh_gl_widget.cpp src/navmesh_gl_widget.h
    src/zone_geometry.cpp src/zone_geometry.h
    src/recast_builder.cpp src/recast_builder.h
    src/mset_writer.cpp src/mset_writer.h
    src/cdclient_db.cpp src/cdclient_db.h
)
target_link_libraries(navmesh_editor PRIVATE
    gl_viewport lu_assets qt_common SQLite::SQLite3
    RecastNavigation::Recast RecastNavigation::Detour
)
target_include_directories(navmesh_editor PRIVATE src)
