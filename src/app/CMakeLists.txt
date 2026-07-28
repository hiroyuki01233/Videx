qt_standard_project_setup()

qt_add_executable(
    videx
    WIN32
    MACOSX_BUNDLE
    main.cpp
    main_window.cpp
    main_window.hpp
    context_rail.cpp
    context_rail.hpp
    effect_card.cpp
    effect_card.hpp
    property_section.cpp
    property_section.hpp
    property_row.cpp
    property_row.hpp
    project_file.cpp
    project_file.hpp
    selection_model.cpp
    selection_model.hpp
    theme.cpp
    theme.hpp
    theme_tokens.hpp
    timeline_widget.cpp
    timeline_widget.hpp
    title_store.cpp
    title_store.hpp
)

qt_add_resources(
    videx
    "videx_theme"
    PREFIX
        "/"
    FILES
        resources/theme/videx-dark.qss
)

target_link_libraries(videx PRIVATE Videx::Core Videx::RenderQt Qt6::Multimedia Qt6::Widgets)
target_compile_features(videx PRIVATE cxx_std_20)

if(TARGET videx_media_worker)
    add_dependencies(videx videx_media_worker)
    target_compile_definitions(videx PRIVATE VIDEX_HAS_MEDIA_WORKER=1)
    if(APPLE)
        add_custom_command(
            TARGET videx
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:videx_media_worker>"
                "$<TARGET_BUNDLE_DIR:videx>/Contents/MacOS/videx-media-worker"
            COMMENT "Bundling the Videx media worker"
            VERBATIM
        )
        install(
            PROGRAMS "$<TARGET_FILE:videx_media_worker>"
            DESTINATION "Videx.app/Contents/MacOS"
        )
    endif()
endif()

set_target_properties(
    videx
    PROPERTIES
        OUTPUT_NAME "Videx"
        MACOSX_BUNDLE_BUNDLE_NAME "Videx"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.videx.editor"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

videx_set_project_warnings(videx)
videx_enable_sanitizers(videx)

if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(
        TARGET videx
        POST_BUILD
        COMMAND
            "$<TARGET_FILE:Qt6::windeployqt>"
            "--$<IF:$<CONFIG:Debug>,debug,release>"
            --no-translations
            "$<TARGET_FILE:videx>"
        COMMENT "Deploying the Qt runtime next to Videx"
        VERBATIM
    )
endif()

if(APPLE AND TARGET Qt6::macdeployqt AND TARGET videx_media_worker)
    add_custom_command(
        TARGET videx
        POST_BUILD
        COMMAND
            "$<TARGET_FILE:Qt6::macdeployqt>"
            "$<TARGET_BUNDLE_DIR:videx>"
            "-executable=$<TARGET_BUNDLE_DIR:videx>/Contents/MacOS/videx-media-worker"
            -always-overwrite
        COMMENT "Deploying the Qt runtime into the Videx bundle"
        VERBATIM
    )
endif()

install(
    TARGETS videx
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(APPLE)
    qt_generate_deploy_app_script(
        TARGET videx
        OUTPUT_SCRIPT videx_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${videx_deploy_script})
endif()
