cmake_minimum_required(VERSION 3.19)
project(CMA_Dashboard LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Charts)

qt_standard_project_setup()

qt_add_executable(CMA_Dashboard
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

target_link_libraries(CMA_Dashboard
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::Sql
        Qt6::Charts
)

install(TARGETS CMA_Dashboard
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET CMA_Dashboard
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
