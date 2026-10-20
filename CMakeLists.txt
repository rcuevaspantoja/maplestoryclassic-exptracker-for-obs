cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp src/exp-filter.cpp src/exp-filter.hpp)

# HUD reading core: plain C++, no OBS dependency, so the CLI and tests can use it too.
add_library(expmeter-core STATIC)
target_sources(
  expmeter-core
  PRIVATE src/hud/glyph_ocr.cpp src/hud/hud_reader.cpp src/hud/locator.cpp src/hud/parser.cpp src/hud/render.cpp
  PUBLIC
    src/hud/glyph_ocr.hpp
    src/hud/glyphs.hpp
    src/hud/hud_reader.hpp
    src/hud/image.hpp
    src/hud/locator.hpp
    src/hud/parser.hpp
    src/hud/render.hpp
)
target_include_directories(expmeter-core PUBLIC src)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE expmeter-core)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

option(ENABLE_TESTS "Build the HUD reader CLI and unit tests" ON)
if(ENABLE_TESTS)
  enable_testing()

  # Image loading for the CLI and tests (stb_image is third-party code; don't fail on its warnings).
  add_library(expmeter-imagefile STATIC tools/image_file.cpp tools/image_file.hpp)
  target_include_directories(expmeter-imagefile PUBLIC tools PRIVATE third_party/stb)
  target_link_libraries(expmeter-imagefile PUBLIC expmeter-core)
  set_target_properties(expmeter-imagefile PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

  add_executable(expmeter-cli tools/expmeter-cli.cpp)
  target_link_libraries(expmeter-cli PRIVATE expmeter-imagefile)

  add_executable(expmeter-tests tests/test_main.cpp tests/test_parser.cpp tests/test_hud_reader.cpp)
  target_include_directories(expmeter-tests PRIVATE third_party/doctest)
  target_link_libraries(expmeter-tests PRIVATE expmeter-imagefile)
  target_compile_definitions(
    expmeter-tests
    PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS EXPMETER_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/references"
  )
  add_test(NAME expmeter-tests COMMAND expmeter-tests)
endif()
