include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules/dftracer-utils.cmake)
dftracer_install_external_project(cpp-logger 2.0.0 "CPP_LOGGER" https://github.com/hariharan-devarajan/cpp-logger.git v0.0.8 ${CMAKE_INSTALL_PREFIX} "")

# Build Brahma with MPI and/or HDF5 support based on dftracer options
# Keep BRAHMA's core targets/install rules enabled in the currently pinned branch.
set(BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_DEPENDENCIES=ON")

# Determine effective brahma MPI toggle from availability and detect implementation.
set(_dftracer_enable_brahma_mpi OFF)
set(_dftracer_dep_mpi_impl "")
if(DFTRACER_ENABLE_MPI)
  find_package(MPI QUIET COMPONENTS C)
  if(MPI_FOUND OR TARGET MPI::MPI_C)
    set(_dftracer_enable_brahma_mpi ON)

    # Resolve MPI implementation: honour user override, then run C probe.
    if(DFTRACER_MPI_IMPL)
      set(_dftracer_dep_mpi_impl "${DFTRACER_MPI_IMPL}")
      message(STATUS "[${PROJECT_NAME}] dependency: MPI implementation overridden: ${_dftracer_dep_mpi_impl}")
    else()
      # Auto-detect via compiled C probe (reads preprocessor macros from mpi.h)
      try_run(
        _dep_mpi_run_result
        _dep_mpi_compile_result
        "${CMAKE_BINARY_DIR}/dftracer_dep_mpi_detect"
        "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/tests/mpi_detect.c"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${MPI_C_INCLUDE_DIRS}"
        LINK_LIBRARIES ${MPI_C_LIBRARIES}
        COMPILE_OUTPUT_VARIABLE _dep_mpi_compile_out
        RUN_OUTPUT_VARIABLE _dep_mpi_detect_out
      )
      if(_dep_mpi_compile_result AND
         NOT "${_dep_mpi_run_result}" STREQUAL "FAILED_TO_RUN")
        if(_dep_mpi_detect_out MATCHES "IMPL=([A-Z]+)")
          set(_dftracer_dep_mpi_impl "${CMAKE_MATCH_1}")
        endif()
        message(STATUS "[${PROJECT_NAME}] dependency: MPI C probe: impl=${_dftracer_dep_mpi_impl}")
      else()
        message(WARNING "[${PROJECT_NAME}] dependency: MPI C probe did not run; brahma will auto-detect impl independently")
      endif()
    endif()
  else()
    message(WARNING "[${PROJECT_NAME}] DFTRACER_ENABLE_MPI=ON but MPI was not found. Forcing BRAHMA_BUILD_WITH_MPI=OFF")
  endif()
endif()

if(_dftracer_enable_brahma_mpi)
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()
  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_WITH_MPI=ON")

  # Forward detected/overridden MPI implementation to brahma so it uses
  # the same impl without re-running its own detection independently.
  if(_dftracer_dep_mpi_impl AND NOT "${_dftracer_dep_mpi_impl}" STREQUAL "UNKNOWN")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DBRAHMA_MPI_IMPL=${_dftracer_dep_mpi_impl}")
    message(STATUS "[${PROJECT_NAME}] Forwarding BRAHMA_MPI_IMPL=${_dftracer_dep_mpi_impl} to brahma")
  endif()
else()
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()
  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_WITH_MPI=OFF")
endif()

if(DFTRACER_ENABLE_HDF5)
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()

  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_BUILD_WITH_HDF5=ON")
endif()

if(DFTRACER_GENERATE_INTERFACES)
  if(NOT "${BRAHMA_CONFIGURE_ARGS}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";")
  endif()

  string(APPEND BRAHMA_CONFIGURE_ARGS "-DBRAHMA_GENERATE_INTERFACES=ON")

  # Forward Clang_DIR / LLVM_DIR to brahma so its find_package(Clang) does not
  # accidentally pick up an unrelated LLVM install on the system (e.g., a
  # version whose dev libraries are missing on CI runners).
  if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DClang_DIR=${Clang_DIR}")
  endif()

  if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
    string(APPEND BRAHMA_CONFIGURE_ARGS ";-DLLVM_DIR=${LLVM_DIR}")
  endif()
endif()

# TODO: pin to a tagged release once the next brahma release is published.
dftracer_install_external_project(brahma 4.1.0 "BRAHMA" https://github.com/hariharan-devarajan/brahma.git v1.0.7 ${CMAKE_INSTALL_PREFIX} "${BRAHMA_CONFIGURE_ARGS}")
if(TARGET brahma AND TARGET cpp-logger)
  add_dependencies(brahma cpp-logger)
endif()

find_package(yaml-cpp QUIET)
dftracer_install_external_project(yaml-cpp "" "YAML_CPP" https://github.com/jbeder/yaml-cpp.git yaml-cpp-0.6.3 ${CMAKE_INSTALL_PREFIX} "-DYAML_CPP_BUILD_TESTS=OFF;-DYAML_BUILD_SHARED_LIBS=ON;-DCMAKE_POLICY_VERSION_MINIMUM=3.5")

if(NOT ${yaml-cpp_FOUND})
  if(TARGET yaml-cpp AND TARGET brahma)
    add_dependencies(yaml-cpp brahma)
  endif()

  if(TARGET yaml-cpp AND TARGET cpp-logger)
    add_dependencies(yaml-cpp cpp-logger)
  endif()
endif()

dftracer_install_external_project(pybind11 2.12.0 "pybind11" https://github.com/pybind/pybind11.git v2.12.0 ${CMAKE_INSTALL_PREFIX} "-DPYBIND11_TEST=OFF")

dftracer_install_external_project(libuv 1.52.1 "libuv" https://github.com/libuv/libuv.git v1.52.1 ${CMAKE_INSTALL_PREFIX} "-DLIBUV_BUILD_TESTS=OFF")
