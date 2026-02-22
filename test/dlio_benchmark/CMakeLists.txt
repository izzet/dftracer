set(DLIO_BENCHMARK_EXE ${MAIN_PYTHON_SITE_PACKAGES}/dlio_benchmark/main.py)
message("-- Found dlio_benchmark at location " ${DLIO_BENCHMARK_EXE})
find_program(PYTHON_EXE python)
set(COMMON_ARGS workload=unet3d_v100 ++workload.train.computation_time=0.05
        ++workload.evaluation.eval_time=0.01 ++workload.train.epochs=2 ++workload.workflow.train=True
        ++workload.workflow.generate_data=True ++workload.dataset.num_files_train=16
        ++workload.dataset.num_files_eval=16 ++workload.reader.read_threads=2
        ++workload.dataset.record_length=4096 ++workload.dataset.record_length_stdev=0
        ++workload.framework=tensorflow ++workload.reader.data_loader=tensorflow)
set(formats npz hdf5 tfrecord png jpeg)
set(lines 2055 1159 715 935 903)
list(LENGTH formats formats_length)
math(EXPR formats_length "${formats_length} - 1")
message(STATUS "Number of formats: ${formats_length}")

foreach(index RANGE ${formats_length})
        list(GET formats ${index} format)
        list(GET lines ${index} line)
        set(test_name dlio_${format})
        df_add_test(${test_name} ${PYTHON_EXE} ${DLIO_BENCHMARK_EXE} ${COMMON_ARGS} ++workload.dataset.format=${format}
                ++workload.output.folder=${CMAKE_CURRENT_BINARY_DIR}/output_${format})
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ENABLE=1)
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_INC_METADATA=1)
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_LOG_LEVEL=INFO)
        set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_TRACE_COMPRESSION=0)

        # Add fixture requirements for streaming writers
        if(DFTRACER_WRITER_TYPE_MOFKA)
                set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_MOFKA_GROUP_FILE=${MOFKA_GROUP_FILE})
                set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_MOFKA_TOPIC_NAME=${test_name})
                set_tests_properties(${test_name} PROPERTIES FIXTURES_REQUIRED ${MOFKA_SERVER_FIXTURE})
        endif()

        if(DFTRACER_WRITER_TYPE_ZMQ)
                set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT DFTRACER_ZMQ_ENDPOINT=tcp://localhost:${ZMQ_EVENT_PORT})
                set_tests_properties(${test_name} PROPERTIES FIXTURES_REQUIRED ${ZMQ_SINK_FIXTURE})
        endif()

        # Use df_check_test for all writer types (handles STDIO, Mofka, ZMQ)
        df_check_test(${test_name} ${line})
endforeach()
