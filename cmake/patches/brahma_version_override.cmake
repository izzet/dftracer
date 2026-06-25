# brahma_version_override.cmake
# Patches brahma's CMakeLists.txt so that externally-provided BRAHMA_MPI_VERSION /
# BRAHMA_MPI_IMPL and BRAHMA_HDF5_VERSION are used as-is, with detection only
# running when the values are NOT pre-supplied.
#
# Invoked by ExternalProject PATCH_COMMAND:
#   cmake -DSOURCE_DIR=<SOURCE_DIR> -P brahma_version_override.cmake
cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR must be defined via -DSOURCE_DIR=...")
endif()

set(_cmake_file "${SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${_cmake_file}")
  message(FATAL_ERROR "brahma CMakeLists.txt not found at: ${_cmake_file}")
endif()

file(READ "${_cmake_file}" _content)

# ---- Guard MPI version recomputation -----------------------------------------
# Brahma unconditionally overwrites BRAHMA_MPI_VERSION from MPI_PACKAGE_VERSION
# (or the MPI standard version fallback). Add a pre-check: if BRAHMA_MPI_VERSION
# is already > 0 (passed in by dftracer), skip the whole recomputation block.
set(_mpi_old [=[    if(MPI_PACKAGE_VERSION)
      convert_version_to_number("${MPI_PACKAGE_VERSION}" BRAHMA_MPI_VERSION)
      message(STATUS "[${PROJECT_NAME}] MPI version: ${MPI_PACKAGE_VERSION} (${BRAHMA_MPI_VERSION})")
    else()
      convert_version_to_number("${MPI_C_VERSION}" BRAHMA_MPI_VERSION)
      message(STATUS "[${PROJECT_NAME}] MPI standard version (fallback): ${MPI_C_VERSION} (${BRAHMA_MPI_VERSION})")
    endif()]=])

set(_mpi_new [=[    if(BRAHMA_MPI_VERSION GREATER 0)
      message(STATUS "[${PROJECT_NAME}] MPI version pre-set (skipping detection): impl=${BRAHMA_MPI_IMPL} version=${BRAHMA_MPI_VERSION}")
    elseif(MPI_PACKAGE_VERSION)
      convert_version_to_number("${MPI_PACKAGE_VERSION}" BRAHMA_MPI_VERSION)
      message(STATUS "[${PROJECT_NAME}] MPI version: ${MPI_PACKAGE_VERSION} (${BRAHMA_MPI_VERSION})")
    else()
      convert_version_to_number("${MPI_C_VERSION}" BRAHMA_MPI_VERSION)
      message(STATUS "[${PROJECT_NAME}] MPI standard version (fallback): ${MPI_C_VERSION} (${BRAHMA_MPI_VERSION})")
    endif()]=])

string(REPLACE "${_mpi_old}" "${_mpi_new}" _patched "${_content}")
if(_patched STREQUAL _content)
  message(WARNING "[dftracer patch] MPI version guard not applied — brahma CMakeLists.txt may have changed")
else()
  set(_content "${_patched}")
  message(STATUS "[dftracer patch] Applied MPI version guard to brahma CMakeLists.txt")
endif()

# ---- Guard HDF5 version recomputation ----------------------------------------
# Brahma calls convert_version_to_number unconditionally to compute
# BRAHMA_HDF5_VERSION from find_package(HDF5). Add a pre-check so that when
# dftracer already forwarded the correct value, it is not overwritten.
set(_hdf5_old [=[        convert_version_to_number("${HDF5_VERSION}" BRAHMA_HDF5_VERSION)]=])

set(_hdf5_new [=[        if(NOT BRAHMA_HDF5_VERSION GREATER 0)
          convert_version_to_number("${HDF5_VERSION}" BRAHMA_HDF5_VERSION)
        endif()]=])

string(REPLACE "${_hdf5_old}" "${_hdf5_new}" _patched "${_content}")
if(_patched STREQUAL _content)
  message(WARNING "[dftracer patch] HDF5 version guard not applied — brahma CMakeLists.txt may have changed")
else()
  set(_content "${_patched}")
  message(STATUS "[dftracer patch] Applied HDF5 version guard to brahma CMakeLists.txt")
endif()

file(WRITE "${_cmake_file}" "${_content}")
message(STATUS "[dftracer patch] brahma version override patch complete")
