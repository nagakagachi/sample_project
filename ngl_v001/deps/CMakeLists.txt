cmake_minimum_required(VERSION 3.20)
project(ngl_deps LANGUAGES C CXX)

include(ExternalProject)

get_filename_component(NGL_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(NGL_EXTERNAL_ROOT "${NGL_ROOT}/external_build/install")

set(ASSIMP_SOURCE_DIR "${NGL_ROOT}/ngl/external/assimp")
set(DIRECTXTEX_SOURCE_DIR "${NGL_ROOT}/ngl/external/DirectXTex")

function(add_ngl_external name source_dir install_subdir config)
  set(build_dir "${CMAKE_BINARY_DIR}/${name}-${config}")
  set(install_dir "${NGL_EXTERNAL_ROOT}/${install_subdir}/${config}")

  set(common_args
    -DCMAKE_INSTALL_PREFIX=${install_dir}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  )

  if("${name}" STREQUAL "assimp")
    list(APPEND common_args
      -DBUILD_SHARED_LIBS=ON
      -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
      -DASSIMP_BUILD_TESTS=OFF
      -DASSIMP_INSTALL=ON
    )
  elseif("${name}" STREQUAL "directxtex")
    list(APPEND common_args
      -DBUILD_SHARED_LIBS=OFF
    )
  endif()

  ExternalProject_Add(${name}_${config}
    SOURCE_DIR "${source_dir}"
    BINARY_DIR "${build_dir}"
    CMAKE_ARGS ${common_args}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${config}
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${config} --target INSTALL
  )
endfunction()

add_ngl_external(assimp "${ASSIMP_SOURCE_DIR}" "assimp" Debug)
add_ngl_external(assimp "${ASSIMP_SOURCE_DIR}" "assimp" Release)
add_ngl_external(directxtex "${DIRECTXTEX_SOURCE_DIR}" "DirectXTex" Debug)
add_ngl_external(directxtex "${DIRECTXTEX_SOURCE_DIR}" "DirectXTex" Release)

add_custom_target(deps
  DEPENDS
    assimp_Debug
    assimp_Release
    directxtex_Debug
    directxtex_Release
)
