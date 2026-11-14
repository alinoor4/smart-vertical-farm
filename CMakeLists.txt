cmake_minimum_required(VERSION 3.24)
project(SmartVerticalFarm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FARM_BUILD_APP "Build the Dear ImGui desktop app (needs OpenGL)" ON)

# Downloads live beside the sources, so deleting build/ does not delete them.
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/.deps CACHE PATH "")
include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

# Warnings on our own targets only.
function(farm_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# Static linking so the .exe runs on a machine without MinGW.
function(farm_static target)
  if(MINGW)
    target_link_options(${target} PRIVATE -static)
  endif()
endfunction()

add_library(farm_core STATIC src/core/version.cpp)
target_include_directories(farm_core PUBLIC src/core)
target_link_libraries(farm_core PUBLIC nlohmann_json::nlohmann_json)
farm_warnings(farm_core)

add_executable(farm_cli src/cli/main.cpp)
target_link_libraries(farm_cli PRIVATE farm_core)
farm_warnings(farm_cli)
farm_static(farm_cli)

add_executable(farm_tests tests/test_main.cpp)
target_link_libraries(farm_tests PRIVATE farm_core)
farm_warnings(farm_tests)
farm_static(farm_tests)

enable_testing()
add_test(NAME farm_tests COMMAND farm_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# The two farm_cli tests (run, bench) are added in M7 and M9, when those commands exist.

if(FARM_BUILD_APP)
  FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/c1aaac65e896a45f81f47a4d68f9959e0f86c859.tar.gz)
  FetchContent_Declare(implot URL https://github.com/epezent/implot/archive/819f200639455a58b6b3d4156b70e4271aeb2d00.tar.gz)
  FetchContent_Declare(glfw URL https://github.com/glfw/glfw/archive/d9d6f0f1f967807ffade6598ea9a631ebaf37a56.tar.gz)
  FetchContent_Declare(stb
    URL https://raw.githubusercontent.com/nothings/stb/a570af1f275acc0efac14ead09bcd6fbc961c98c/stb_image_write.h
    DOWNLOAD_NO_EXTRACT TRUE)

  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(imgui implot glfw stb)   # imgui, implot and stb have no CMake file: this only downloads them

  find_package(OpenGL REQUIRED)

  add_library(imgui_all STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp)
  # SYSTEM: third-party headers must not trigger our warning flags.
  target_include_directories(imgui_all SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR})
  # 32-bit draw indices so a large drawing cannot overflow 16-bit ones.
  target_compile_definitions(imgui_all PUBLIC "ImDrawIdx=unsigned int")
  target_link_libraries(imgui_all PUBLIC glfw OpenGL::GL)
  if(WIN32)
    target_link_libraries(imgui_all PUBLIC imm32)
  endif()

  add_executable(farm_app src/app/main.cpp)
  target_link_libraries(farm_app PRIVATE farm_core imgui_all)
  target_include_directories(farm_app SYSTEM PRIVATE ${stb_SOURCE_DIR})
  farm_warnings(farm_app)
  farm_static(farm_app)
  if(MINGW)
    target_link_options(farm_app PRIVATE -mwindows)   # GUI subsystem: no console window beside the app
  endif()
endif()
