set_property(GLOBAL PROPERTY COVERAGE_FILES "")
include_directories(${DFTRACER_PROJECT_DIR}/include)

set(MOFKA_GROUP_FILE "${CMAKE_CURRENT_BINARY_DIR}/mofka.group.json")
set(MOFKA_SERVER_FIXTURE "MOFKA_SERVER")

find_program(BASH_EXE bash)

if(DFTRACER_WRITER_TYPE_MOFKA)
    find_program(BEDROCK_EXE bedrock REQUIRED)
    add_test(mofka_server_start ${CMAKE_CURRENT_SOURCE_DIR}/mofka_server_start.sh ${BEDROCK_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/mofka.config.json ${CMAKE_CURRENT_BINARY_DIR}/mofka_server.log)
    add_test(mofka_server_stop ${CMAKE_CURRENT_SOURCE_DIR}/mofka_server_stop.sh)
    set_tests_properties(mofka_server_start PROPERTIES FIXTURES_SETUP ${MOFKA_SERVER_FIXTURE})
    set_tests_properties(mofka_server_stop PROPERTIES FIXTURES_CLEANUP ${MOFKA_SERVER_FIXTURE})
endif()

function(df_add_test)
    add_test(${ARGV})
    set_tests_properties(${ARGV0} PROPERTIES
        ENVIRONMENT "LLVM_PROFILE_FILE=${ARGV0}.profraw")
    get_property(OLD_CF GLOBAL PROPERTY COVERAGE_FILES)

    # Use test_fixture_name for constructing the profraw path
    set(NEW_CF ${OLD_CF} ${CMAKE_CURRENT_BINARY_DIR}/${ARGV0}.profraw)
    set_property(GLOBAL PROPERTY COVERAGE_FILES ${NEW_CF})
endfunction()

function(df_check_test test_name expected_count)
    if(DFTRACER_WRITER_TYPE_MOFKA)
        df_add_test(check_${test_name}
            ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/check_mofka_topic.py ${test_name} ${expected_count})
        set_tests_properties(check_${test_name} PROPERTIES FIXTURES_REQUIRED ${MOFKA_SERVER_FIXTURE})
        set_property(TEST check_${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_MOFKA_GROUP_FILE=${MOFKA_GROUP_FILE})
        set_property(TEST check_${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_MOFKA_TOPIC_NAME=${test_name})
    else()
        if(${expected_count} EQUAL 0)
            df_add_test(check_${test_name}
                ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_not.sh
                ${CMAKE_CURRENT_BINARY_DIR}/${test_name}*
            )
        else()
            df_add_test(check_${test_name}
                ${BASH_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/check_file_at_least.sh
                ${CMAKE_CURRENT_BINARY_DIR}/${test_name}* ${expected_count}
            )
        endif()
    endif()

    set_tests_properties(check_${test_name} PROPERTIES DEPENDS ${test_name})
endfunction()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_cpp cpp/test.cpp)
target_link_libraries(test_cpp ${PROJECT_NAME}_core_dbg)
add_dependencies(test_cpp ${PROJECT_NAME}_core_dbg)
add_dependencies(test_cpp ${PROJECT_NAME}_preload_dbg)

add_library(testlib SHARED c/testlib.c)
target_compile_options(testlib PRIVATE ${DFTRACER_FUNCTION_FLAGS})

add_executable(test_c c/test.c)
target_compile_options(test_c PRIVATE ${DFTRACER_FUNCTION_FLAGS})
target_link_libraries(test_c ${PROJECT_NAME}_core_dbg testlib)
add_dependencies(test_c ${PROJECT_NAME}_core_dbg)
add_dependencies(test_c ${PROJECT_NAME}_preload_dbg)

function(set_common_properties test_name)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_BIND_SIGNALS=0)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/data)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=OFF)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_LEVEL=DEBUG)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_TRACE_COMPRESSION=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_WRITE_BUFFER_SIZE=4096)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_LIBRARY_PATH=$ENV{LD_LIBRARY_PATH}:${CMAKE_SOURCE_DIR}/.spack-env/view/lib:${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}:${DFTRACER_TEST_LD_LIBRARY_PATH})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT PYTHONPATH=$ENV{PYTHONPATH}:${CMAKE_SOURCE_DIR}/.spack-env/bin/python)

    if(DFTRACER_WRITER_TYPE_MOFKA)
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_MOFKA_GROUP_FILE=${MOFKA_GROUP_FILE})
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_MOFKA_TOPIC_NAME=${test_name})
        set_tests_properties(${test_name} PROPERTIES FIXTURES_REQUIRED ${MOFKA_SERVER_FIXTURE})
    endif()
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/data)

set(test_name test_cpp_basic_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)
df_check_test(${test_name} 29)

set(test_name test_cpp_basic_app_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
df_check_test(${test_name} 29)

set(test_name test_cpp_basic_app_only_yaml)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_CONFIGURATION=${CMAKE_CURRENT_SOURCE_DIR}/yaml/conf.yaml)
set_common_properties(${test_name})

set(test_name test_cpp_basic_app_compress_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_TRACE_COMPRESSION=1)

set(test_name test_cpp_basic_app_aggregate_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
df_check_test(${test_name} 14)

set(test_name test_cpp_basic_app_aggregate_selective_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/yaml/rules.yaml)
df_check_test(${test_name} 14)

set(test_name test_cpp_basic_app_disable_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)
df_check_test(${test_name} 0)

set(test_name test_c_basic_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)
df_check_test(${test_name} 4)

set(test_name test_c_basic_app_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
df_check_test(${test_name} 4)

set(test_name test_c_basic_app_aggregate_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
df_check_test(${test_name} 11)

set(test_name test_c_basic_app_aggregate_selective_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/yaml/rules.yaml)
df_check_test(${test_name} 11)

set(test_name test_c_basic_app_disable_only)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_c ${CMAKE_CURRENT_BINARY_DIR}/data 1)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)
df_check_test(${test_name} 0)

set(test_name test_cpp_basic_meta)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 29)

set(test_name test_cpp_basic_affinity)
df_add_test(${test_name} ${CMAKE_BINARY_DIR}/bin/test_cpp ${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload_dbg.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=PRELOAD)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_SET_CORE_AFFINITY=1)
df_check_test(${test_name} 29)

set(test_name test_py_disable_only)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR})
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)
df_check_test(${test_name} 0)

set(test_name test_py_both)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)

set(test_name test_py_aggregation_both)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)

set(test_name test_py_aggregation_selective_both)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/test.py --format=npz --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE_AGGREGATION=ON)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_TYPE=SELECTIVE)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_AGGREGATION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/yaml/rules.yaml)

# ########################################################################
# AI Logging Test
# ########################################################################

# # Normal Run
set(test_name test_py_ai_logging_normal)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 74)

# # Normal Run + Epoch as Metadata
set(test_name test_py_ai_logging_epoch_as_metadata)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --epoch-as-metadata)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 74)

# # Disable DFTracer
set(test_name test_py_ai_logging_disable_only)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)
df_check_test(${test_name} 0)

# # Disable Category

# ## ALL Category
set(test_name test_py_ai_logging_disable_cat_all)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat all)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 7)

# ## DataLoader category
set(test_name test_py_ai_logging_disable_cat_dataloader)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat dataloader)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 59)

# ## Device category
set(test_name test_py_ai_logging_disable_cat_device)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat device)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 68)

# ## Compute category
set(test_name test_py_ai_logging_disable_cat_compute)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat compute)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 50)

# ## Checkpoint category
set(test_name test_py_ai_logging_disable_cat_ckpt)
df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/ai_logging.py --data_dir=${CMAKE_CURRENT_BINARY_DIR}/data --niter 3 --num_files 2 --disable-ai-cat ckpt)
set_common_properties(${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT LD_PRELOAD=${CMAKE_BINARY_DIR}/${DFTRACER_LIBDIR}/libdftracer_preload.so)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_BINARY_DIR})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_FILE=${CMAKE_CURRENT_BINARY_DIR}/${test_name})
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INIT=HYBRID)
set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
df_check_test(${test_name} 71)

# ########################################################################
# ## END OF AI LOGGING TEST
# ########################################################################
set(TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/util.h)

if(ENABLE_DLIO_BENCHMARK_TESTS)
    set(test_name python_test_py_disable_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz)
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=0)

    set(test_name python_test_py_disable_io_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DISABLE_IO=1)

    set(test_name python_test_py_io_all_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=all)

    set(test_name python_test_py_io_specific_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR})

    set(test_name python_test_py_io_specific_meta_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR})

    set(test_name python_test_py_io_all_meta_only)
    df_add_test(${test_name} ${DFTRACER_PYTHON_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/py/python_test.py --format=npz --log_dir=${CMAKE_CURRENT_BINARY_DIR}/${test_name} --data_dir=${CMAKE_CURRENT_BINARY_DIR})
    set_common_properties(${test_name})
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
    set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_DATA_DIR=all)
    add_subdirectory(dlio_benchmark)
endif()

if(ENABLE_PAPER_TESTS)
    add_subdirectory(paper)
endif()

add_subdirectory(dfanalyzer)
