add_library(videx_media STATIC)
add_library(Videx::Media ALIAS videx_media)

target_sources(
    videx_media
    PRIVATE
        src/media_probe.cpp
        src/audio_buffer.cpp
        src/video_frame.cpp
        src/codec_support.cpp
        src/timeline_export.cpp
        src/waveform.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/videx/media/media_probe.hpp
            include/videx/media/audio_buffer.hpp
            include/videx/media/video_frame.hpp
            include/videx/media/codec_support.hpp
            include/videx/media/timeline_export.hpp
            include/videx/media/waveform.hpp
)

target_compile_features(videx_media PUBLIC cxx_std_20)
target_include_directories(
    videx_media
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(videx_media SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(videx_media PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(videx_media PRIVATE ${FFMPEG_LIBRARIES})

videx_set_project_warnings(videx_media)
videx_enable_sanitizers(videx_media)
