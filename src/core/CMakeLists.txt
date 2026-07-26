add_library(videx_core STATIC)
add_library(Videx::Core ALIAS videx_core)

target_sources(
    videx_core
    PRIVATE
        src/application_info.cpp
        src/edit_session.cpp
        src/timeline.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/videx/core/application_info.hpp
            include/videx/core/edit_session.hpp
            include/videx/core/timeline.hpp
)

target_compile_features(videx_core PUBLIC cxx_std_20)
target_include_directories(
    videx_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

videx_set_project_warnings(videx_core)
videx_enable_sanitizers(videx_core)

install(
    TARGETS videx_core
    EXPORT VidexTargets
    FILE_SET HEADERS
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
