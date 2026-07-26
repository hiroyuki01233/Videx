add_executable(videx_media_worker main.cpp)
target_link_libraries(videx_media_worker PRIVATE Videx::Core Videx::Media)
target_compile_features(videx_media_worker PRIVATE cxx_std_20)

set_target_properties(videx_media_worker PROPERTIES OUTPUT_NAME "videx-media-worker")

videx_set_project_warnings(videx_media_worker)
videx_enable_sanitizers(videx_media_worker)

install(TARGETS videx_media_worker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    add_test(NAME videx.media_worker.version COMMAND videx_media_worker --version)
endif()

