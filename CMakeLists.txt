cmake_minimum_required(VERSION 3.16)
project(OpenDepot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN)
endif()

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

if(WIN32)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico" ICON_PATH)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    set(APP_RC "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
endif()

add_executable(opendepot-cli
    src/main_cli.cpp
    src/adk.cpp
    src/flashcode_db.cpp
    ${APP_RC}
)
target_include_directories(opendepot-cli PRIVATE src)
target_link_libraries(opendepot-cli PRIVATE ole32 advapi32 user32)

add_executable(OpenDepot WIN32
    src/main_gui.cpp
    src/adk.cpp
    src/flashcode_db.cpp
    ${APP_RC}
)
target_include_directories(OpenDepot PRIVATE src)
target_compile_definitions(OpenDepot PRIVATE UNICODE _UNICODE)
target_link_libraries(OpenDepot PRIVATE
    imgui
    d3d11 dxgi dwmapi ole32 advapi32 comdlg32 user32 gdi32 shell32
)

set_target_properties(opendepot-cli PROPERTIES OUTPUT_NAME "opendepot-cli")
