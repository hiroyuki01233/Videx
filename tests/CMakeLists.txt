function(videx_isolate_test_runtime target_name)
    set_target_properties(
        ${target_name}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-bin/${target_name}"
    )
endfunction()

add_executable(
    videx_core_tests
    core/application_info_test.cpp
    core/timeline_test.cpp
)
target_link_libraries(videx_core_tests PRIVATE Videx::Core)
target_compile_features(videx_core_tests PRIVATE cxx_std_20)

videx_set_project_warnings(videx_core_tests)
videx_enable_sanitizers(videx_core_tests)
videx_isolate_test_runtime(videx_core_tests)

add_test(NAME videx.core.application_info COMMAND videx_core_tests)

if(Qt6_FOUND)
    add_executable(
        videx_project_file_tests
        app/project_file_test.cpp
        ../src/app/project_file.cpp
        ../src/app/project_file.hpp
    )
    target_include_directories(videx_project_file_tests PRIVATE ../src/app)
    target_link_libraries(videx_project_file_tests PRIVATE Videx::Core Qt6::Core)
    target_compile_features(videx_project_file_tests PRIVATE cxx_std_20)

    videx_set_project_warnings(videx_project_file_tests)
    videx_enable_sanitizers(videx_project_file_tests)
    videx_isolate_test_runtime(videx_project_file_tests)

    add_test(NAME videx.project_file.round_trip COMMAND videx_project_file_tests)
    if(WIN32)
        set_tests_properties(
            videx.project_file.round_trip
            PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()

    add_executable(
        videx_title_store_tests
        app/title_store_test.cpp
        ../src/app/title_store.cpp
        ../src/app/title_store.hpp
    )
    target_include_directories(videx_title_store_tests PRIVATE ../src/app)
    target_link_libraries(videx_title_store_tests PRIVATE Qt6::Gui)
    target_compile_features(videx_title_store_tests PRIVATE cxx_std_20)

    videx_set_project_warnings(videx_title_store_tests)
    videx_enable_sanitizers(videx_title_store_tests)
    videx_isolate_test_runtime(videx_title_store_tests)

    add_test(NAME videx.title_store.round_trip COMMAND videx_title_store_tests)
    set_tests_properties(
        videx.title_store.round_trip
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    if(WIN32)
        set_tests_properties(
            videx.title_store.round_trip
            PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()

    add_executable(
        videx_timeline_widget_tests
        app/timeline_widget_test.cpp
        ../src/app/timeline_widget.cpp
        ../src/app/timeline_widget.hpp
    )
    target_include_directories(videx_timeline_widget_tests PRIVATE ../src/app)
    target_link_libraries(videx_timeline_widget_tests PRIVATE Videx::Core Qt6::Widgets)
    target_compile_features(videx_timeline_widget_tests PRIVATE cxx_std_20)
    videx_set_project_warnings(videx_timeline_widget_tests)
    videx_enable_sanitizers(videx_timeline_widget_tests)
    videx_isolate_test_runtime(videx_timeline_widget_tests)
    add_test(NAME videx.timeline.pointer_interactions COMMAND videx_timeline_widget_tests)
    set_tests_properties(videx.timeline.pointer_interactions PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(videx.timeline.pointer_interactions PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    add_executable(videx_monitor_widget_tests render/monitor_widget_test.cpp)
    target_link_libraries(videx_monitor_widget_tests PRIVATE Videx::RenderQt Qt6::Widgets)
    target_compile_features(videx_monitor_widget_tests PRIVATE cxx_std_20)
    videx_set_project_warnings(videx_monitor_widget_tests)
    videx_enable_sanitizers(videx_monitor_widget_tests)
    videx_isolate_test_runtime(videx_monitor_widget_tests)
    add_test(NAME videx.monitor.pointer_interactions COMMAND videx_monitor_widget_tests)
    set_tests_properties(videx.monitor.pointer_interactions PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(videx.monitor.pointer_interactions PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    add_executable(
        videx_context_rail_tests
        app/context_rail_test.cpp
        ../src/app/context_rail.cpp
        ../src/app/context_rail.hpp
        ../src/app/effect_card.cpp
        ../src/app/effect_card.hpp
        ../src/app/property_section.cpp
        ../src/app/property_section.hpp
        ../src/app/property_row.cpp
        ../src/app/property_row.hpp
        ../src/app/selection_model.cpp
        ../src/app/selection_model.hpp
    )
    target_include_directories(videx_context_rail_tests PRIVATE ../src/app)
    target_link_libraries(videx_context_rail_tests PRIVATE Videx::Core Qt6::Widgets)
    target_compile_features(videx_context_rail_tests PRIVATE cxx_std_20)
    set_target_properties(videx_context_rail_tests PROPERTIES AUTOMOC ON)
    videx_set_project_warnings(videx_context_rail_tests)
    videx_enable_sanitizers(videx_context_rail_tests)
    videx_isolate_test_runtime(videx_context_rail_tests)
    add_test(NAME videx.ui.context_rail COMMAND videx_context_rail_tests)
    set_tests_properties(videx.ui.context_rail PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(videx.ui.context_rail PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()

    if(TARGET videx_media_worker)
        add_executable(videx_media_worker_protocol_tests app/media_worker_protocol_test.cpp)
        target_link_libraries(videx_media_worker_protocol_tests PRIVATE Qt6::Core)
        target_compile_features(videx_media_worker_protocol_tests PRIVATE cxx_std_20)
        videx_set_project_warnings(videx_media_worker_protocol_tests)
        videx_enable_sanitizers(videx_media_worker_protocol_tests)
        videx_isolate_test_runtime(videx_media_worker_protocol_tests)

        add_test(
            NAME videx.media_worker.frame_protocol
            COMMAND
                videx_media_worker_protocol_tests
                $<TARGET_FILE:videx_media_worker>
                ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/frame.ppm
        )
        if(WIN32)
            set_tests_properties(
                videx.media_worker.frame_protocol
                PROPERTIES
                    ENVIRONMENT_MODIFICATION
                        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
            )
        endif()
    endif()
endif()

if(TARGET Videx::Media)
    add_executable(videx_media_tests media/media_probe_test.cpp)
    target_link_libraries(videx_media_tests PRIVATE Videx::Media)
    target_compile_features(videx_media_tests PRIVATE cxx_std_20)

    videx_set_project_warnings(videx_media_tests)
    videx_enable_sanitizers(videx_media_tests)
    videx_isolate_test_runtime(videx_media_tests)

    add_test(NAME videx.media.probe_wav COMMAND videx_media_tests)

    add_executable(videx_timeline_export_tests media/timeline_export_test.cpp)
    target_link_libraries(videx_timeline_export_tests PRIVATE Videx::Media Videx::Core)
    target_compile_features(videx_timeline_export_tests PRIVATE cxx_std_20)
    videx_set_project_warnings(videx_timeline_export_tests)
    videx_enable_sanitizers(videx_timeline_export_tests)
    videx_isolate_test_runtime(videx_timeline_export_tests)
    add_test(NAME videx.media.export_h264_aac COMMAND videx_timeline_export_tests)
endif()
