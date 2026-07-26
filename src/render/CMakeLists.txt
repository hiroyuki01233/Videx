add_library(videx_render_qt STATIC)
add_library(Videx::RenderQt ALIAS videx_render_qt)

target_sources(
    videx_render_qt
    PRIVATE
        src/qt_monitor_widget.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/videx/render/qt_monitor_widget.hpp
)

target_compile_features(videx_render_qt PUBLIC cxx_std_20)
target_link_libraries(videx_render_qt PUBLIC Qt6::Widgets PRIVATE Qt6::GuiPrivate)
target_include_directories(
    videx_render_qt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

videx_set_project_warnings(videx_render_qt)
videx_enable_sanitizers(videx_render_qt)

