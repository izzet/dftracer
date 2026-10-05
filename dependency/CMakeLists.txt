include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules/dftracer-utils.cmake)

# cpp-logger and brahma used to be cloned UNCONDITIONALLY, unlike gotcha/yaml-cpp/pybind11 below,
# which have always been guarded by find_package. That asymmetry was harmless while the upstream
# repos were reachable and merely wasteful on rebuilds. It stopped being harmless when
# github.com/hariharan-devarajan/cpp-logger started returning 401 (repo gone or private): every
# configure re-cloned a dependency that was already installed in ${CMAKE_INSTALL_PREFIX}, so the
# build could no longer be reproduced at all. brahma comes from the same account and is one
# outage away from the same fate. Guarding both makes an already-installed dependency satisfy the
# build, which is what the other three dependencies have always done.
#
# The prebuilt artefacts are found via CMAKE_PREFIX_PATH; setup.py already puts the install prefix
# first on that path, so an incremental rebuild resolves them without any network access.
set(DFTRACER_EXTERNAL_DEPS "")

find_package(cpp-logger 1.0.0 QUIET)
if(NOT cpp-logger_FOUND)
  dftracer_install_external_project(cpp-logger 1.0.0 "CPP_LOGGER" https://github.com/rayandrew/cpp-logger.git 6e451bb89f6d93ce7a90b04a3286096653c722ca ${CMAKE_INSTALL_PREFIX} "")
  list(APPEND DFTRACER_EXTERNAL_DEPS cpp-logger)
else()
  message(STATUS "-- [DFTRACER] using preinstalled cpp-logger (${cpp-logger_DIR}); skipping clone")
endif()

find_package(gotcha QUIET)
if(NOT ${gotcha_FOUND})
  dftracer_install_external_project(gotcha 2.3.2 "gotcha" https://github.com/LLNL/GOTCHA.git 1.0.7 ${CMAKE_INSTALL_PREFIX} "-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
  list(APPEND DFTRACER_EXTERNAL_DEPS gotcha)
endif()

find_package(brahma 3.0.0 QUIET)
if(NOT brahma_FOUND)
  dftracer_install_external_project(brahma 3.0.0 "BRAHMA" https://github.com/hariharan-devarajan/brahma.git v1.0.1 ${CMAKE_INSTALL_PREFIX} "")
  # Only order against dependencies we are actually building here -- add_dependencies on a
  # find_package target that was never declared as an ExternalProject is a hard configure error.
  foreach(dep IN LISTS DFTRACER_EXTERNAL_DEPS)
    add_dependencies(brahma ${dep})
  endforeach()
  list(APPEND DFTRACER_EXTERNAL_DEPS brahma)
else()
  message(STATUS "-- [DFTRACER] using preinstalled brahma (${brahma_DIR}); skipping clone")
endif()

find_package(yaml-cpp QUIET)
if(NOT ${yaml-cpp_FOUND})
  dftracer_install_external_project(yaml-cpp "" "YAML_CPP" https://github.com/jbeder/yaml-cpp.git yaml-cpp-0.6.3 ${CMAKE_INSTALL_PREFIX} "-DYAML_CPP_BUILD_TESTS=OFF;-DYAML_BUILD_SHARED_LIBS=ON;-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
  foreach(dep IN LISTS DFTRACER_EXTERNAL_DEPS)
    add_dependencies(yaml-cpp ${dep})
  endforeach()
endif()

find_package(pybind11 QUIET)
if(NOT ${pybind11_FOUND})
  dftracer_install_external_project(pybind11 2.12.0 "pybind11" https://github.com/pybind/pybind11.git v2.12.0 ${CMAKE_INSTALL_PREFIX} "-DPYBIND11_TEST=OFF")
endif()
