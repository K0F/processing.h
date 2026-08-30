cmake_minimum_required(VERSION 3.16)
project(pdeide C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The Export button links vendored static raylib + freetype archives that it
# expects at <repo>/build/static/... ; the Makefile builds those with
# `make static`, this CMake build does the same thing via ExternalProject so
# the artefacts land in exactly the same locations (source-tree build/static).
option(PDEIDE_STATIC_DEPS "Build static raylib+freetype into build/static via ExternalProject" ON)

set(PDEIDE_STATIC_DIR "${CMAKE_SOURCE_DIR}/build/static")

# ----------------------------------------------------------------------
# vendored submodules (third_party/raylib, third_party/freetype) are not
# checked out in a fresh clone; bootstrap them at configure time when the
# static dependency bundles are requested.
# ----------------------------------------------------------------------
if(PDEIDE_STATIC_DEPS)
  set(_PDEIDE_MISSING_SUBMODS "")
  foreach(_sub third_party/raylib third_party/freetype)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${_sub}/CMakeLists.txt" AND
       NOT EXISTS "${CMAKE_SOURCE_DIR}/${_sub}/src/raylib.h")
      list(APPEND _PDEIDE_MISSING_SUBMODS "${_sub}")
    endif()
  endforeach()
  if(_PDEIDE_MISSING_SUBMODS)
    find_package(Git QUIET)
    if(Git_FOUND)
      message(STATUS "initializing git submodules: ${_PDEIDE_MISSING_SUBMODS}")
      execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE _pdeide_submod_rc)
      if(NOT _pdeide_submod_rc EQUAL 0)
        message(FATAL_ERROR "Failed to initialize git submodules "
          "(${_PDEIDE_MISSING_SUBMODS}). Run manually:\n"
          "  git submodule update --init --recursive")
      endif()
    else()
      message(FATAL_ERROR "Vendored dependencies ${_PDEIDE_MISSING_SUBMODS} are "
        "missing and git was not found. Run:\n  git submodule update --init --recursive")
    endif()
  endif()
endif()

# ----------------------------------------------------------------------
# static dependency bundles (used by both the IDE link and the Export step)
# ----------------------------------------------------------------------
if(PDEIDE_STATIC_DEPS)
  include(ExternalProject)

  ExternalProject_Add(ep_raylib_static
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/raylib"
    BINARY_DIR "${PDEIDE_STATIC_DIR}/raylib"
    CMAKE_ARGS
      -DBUILD_SHARED_LIBS=OFF
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_EXAMPLES=OFF
      -DGLFW_BUILD_WAYLAND=OFF
      -DCMAKE_C_FLAGS=-fPIC
    INSTALL_COMMAND ""
  )

  ExternalProject_Add(ep_freetype_static
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/freetype"
    BINARY_DIR "${PDEIDE_STATIC_DIR}/freetype"
    CMAKE_ARGS
      -DBUILD_SHARED_LIBS=OFF
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_C_FLAGS=-fPIC
      -DFT_DISABLE_HARFBUZZ=TRUE
      -DFT_DISABLE_BROTLI=TRUE
      -DFT_DISABLE_BZIP2=TRUE
      -DFT_DISABLE_PNG=TRUE
      -DFT_DISABLE_ZLIB=TRUE
    INSTALL_COMMAND ""
  )

  add_custom_target(static_archives ALL DEPENDS ep_raylib_static ep_freetype_static)

  set(RAYLIB_STATIC "${PDEIDE_STATIC_DIR}/raylib/raylib/libraylib.a")
  set(FREETYPE_STATIC "${PDEIDE_STATIC_DIR}/freetype/libfreetype.a")
  set(RAYLIB_SRC "${CMAKE_SOURCE_DIR}/third_party/raylib/src")
  set(FREETYPE_SRC "${CMAKE_SOURCE_DIR}/third_party/freetype/include")
  set(FREETYPE_BUILD "${PDEIDE_STATIC_DIR}/freetype/include")
else()
  find_package(PkgConfig QUIET)
  pkg_check_modules(RAYLIB raylib REQUIRED)
  pkg_check_modules(FREETYPE freetype2 REQUIRED)
  set(RAYLIB_SRC "" )
  set(FREETYPE_SRC "")
  set(FREETYPE_BUILD "")
endif()

# ----------------------------------------------------------------------
# the IDE
# ----------------------------------------------------------------------
add_executable(pdeide
  pdeide.c
  platform.c
  editor.c
  tinyfiledialogs.c
)

target_include_directories(pdeide PRIVATE
  "${CMAKE_SOURCE_DIR}"
  "${RAYLIB_SRC}"
  "${FREETYPE_SRC}"
  "${FREETYPE_BUILD}"
)

if(PDEIDE_STATIC_DEPS)
  add_dependencies(pdeide static_archives)
  target_compile_definitions(pdeide PRIVATE $<$<PLATFORM_ID:Linux>:X11>)
  if(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    find_package(X11 REQUIRED)
    target_link_libraries(pdeide
      "${RAYLIB_STATIC}" "${FREETYPE_STATIC}"
      OpenGL::GL X11::X11 m pthread rt ${CMAKE_DL_LIBS})
  elseif(APPLE)
    target_link_libraries(pdeide
      "${RAYLIB_STATIC}" "${FREETYPE_STATIC}"
      "-framework Cocoa" "-framework IOKit" "-framework CoreFoundation"
      "-framework CoreVideo" "-framework OpenGL")
  elseif(WIN32)
    target_link_libraries(pdeide
      "${RAYLIB_STATIC}" "${FREETYPE_STATIC}"
      gdi32 opengl32 winmm user32 shell32 ole32)
  endif()
else()
  target_include_directories(pdeide PRIVATE ${RAYLIB_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
  target_link_libraries(pdeide ${RAYLIB_LIBRARIES} ${FREETYPE_LIBRARIES} m)
  target_link_libraries(pdeide ${RAYLIB_LDFLAGS} ${FREETYPE_LDFLAGS})
endif()

# ----------------------------------------------------------------------
# the transpiler
# ----------------------------------------------------------------------
add_executable(pde2c pde2c.c)
target_include_directories(pde2c PRIVATE "${CMAKE_SOURCE_DIR}")

# ----------------------------------------------------------------------
# tests
# ----------------------------------------------------------------------
enable_testing()
add_executable(test_editor tests/test_editor.c editor.c)
target_include_directories(test_editor PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(test_editor m)
add_test(NAME test_editor COMMAND test_editor)

# ----------------------------------------------------------------------
# Linux self-contained release tarball (staged, not installed system-wide)
# Only meaningful with static deps: the bundles are the point of a
# self-contained release.
# ----------------------------------------------------------------------
if(PDEIDE_STATIC_DEPS)
# The produced directory mirrors the repo root layout, so the IDE's Run and
# Export steps find pde2c, the vendored static archives and the fonts at the
# exact relative paths they expect, no matter where the tarball is unpacked.
set(PDEIDE_RELEASE_DIR "${CMAKE_BINARY_DIR}/dist/pdeide-linux")

add_custom_target(release
  DEPENDS pdeide pde2c static_archives
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${PDEIDE_RELEASE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PDEIDE_RELEASE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PDEIDE_RELEASE_DIR}/build/static/raylib/raylib"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PDEIDE_RELEASE_DIR}/build/static/freetype/include"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PDEIDE_RELEASE_DIR}/third_party/raylib/src"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PDEIDE_RELEASE_DIR}/third_party/freetype/include"
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:pdeide> "${PDEIDE_RELEASE_DIR}/pdeide"
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:pde2c> "${PDEIDE_RELEASE_DIR}/pde2c"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/processing.h" "${CMAKE_SOURCE_DIR}/editor.h" "${CMAKE_SOURCE_DIR}/platform.h" "${CMAKE_SOURCE_DIR}/sketch.pde" "${CMAKE_SOURCE_DIR}/terminus.ttf" "${CMAKE_SOURCE_DIR}/terminus_ttf.h" "${PDEIDE_RELEASE_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy "${RAYLIB_STATIC}" "${PDEIDE_RELEASE_DIR}/build/static/raylib/raylib/libraylib.a"
  COMMAND ${CMAKE_COMMAND} -E copy "${FREETYPE_STATIC}" "${PDEIDE_RELEASE_DIR}/build/static/freetype/libfreetype.a"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${FREETYPE_BUILD}" "${PDEIDE_RELEASE_DIR}/build/static/freetype/include"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${FREETYPE_SRC}" "${PDEIDE_RELEASE_DIR}/third_party/freetype/include"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${RAYLIB_SRC}" "${PDEIDE_RELEASE_DIR}/third_party/raylib/src"
  COMMAND tar -C "${CMAKE_BINARY_DIR}/dist" -czf "${CMAKE_BINARY_DIR}/pdeide-linux.tar.gz" pdeide-linux
  COMMAND ${CMAKE_COMMAND} -E echo "tarball: ${CMAKE_BINARY_DIR}/pdeide-linux.tar.gz"
  USES_TERMINAL
)
endif()

install(TARGETS pdeide pde2c test_editor RUNTIME DESTINATION bin)
install(FILES processing.h editor.h platform.h terminus.ttf terminus_ttf.h sketch.pde DESTINATION share/pdeide)
