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

string(JSON _obs_studio_version GET ${buildspec} dependencies obs-studio version)
set(_obs_studio_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-${_obs_studio_version}")

list(APPEND CMAKE_MODULE_PATH "${_obs_studio_dir}/cmake/finders")
find_package(FFmpeg REQUIRED avcodec avformat avutil swscale swresample)

# media-playback is an internal, unversioned OBS library (INTERFACE target,
# no separate release/install package) - pull its sources in directly from
# the pinned obs-studio checkout fetched by buildspec.json.
add_subdirectory("${_obs_studio_dir}/shared/media-playback" "${CMAKE_CURRENT_BINARY_DIR}/media-playback")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/program-standby-source.c src/obs-ffmpeg-compat.h
                                              src/obs-ffmpeg-formats.h src/scene-membership.h src/scene-membership.c
                                              src/scene-membership-obs.h src/scene-membership-obs.c
                                              src/standby-state.h src/standby-state.c
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::media-playback FFmpeg::swscale FFmpeg::swresample)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
