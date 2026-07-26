#include <dftracer/core/common/singleton.h>
#include <dftracer/core/serialization/json_line.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "check.h"

using namespace dftracer;

void test_initialize_serialization() {
  std::cout << "=== Test: Initialize Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[1024];
  HashType hostname_hash = const_cast<char*>("test_hash_12345");

  size_t size = serializer->initialize(buffer, hostname_hash);

  assert(size > 0);
  assert(buffer[0] == '[');
  assert(buffer[1] == '\n');

  std::cout << "✓ Initialize serialization test passed\n" << std::endl;
}

void test_data_event_serialization() {
  std::cout << "=== Test: Data Event Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[4096];
  size_t index = 0;

  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(buffer, hostname_hash);

  // Create metadata for data event
  ConstEventNameType category = "posix";
  ConstEventNameType event_name = "read";
  TimeResolution start_time = 2000000;
  TimeResolution duration = 3000;
  ProcessID process_id = 1234;
  ThreadID thread_id = 1;

  Metadata* metadata = new Metadata();
  metadata->insert_or_assign("file_name", std::string("/tmp/test.dat"));
  metadata->insert_or_assign("file_size", static_cast<size_t>(1024));
  metadata->insert_or_assign("file_offset", static_cast<size_t>(0));

  size_t size = serializer->data(buffer, index, event_name, category,
                                 TraceEventType::TRACE_TYPE_LIBC_IO, start_time,
                                 duration, metadata, process_id, thread_id);

  assert(size > 0);
  std::string result(buffer);
  assert(result.find("read") != std::string::npos);
  assert(result.find("posix") != std::string::npos);
  // The layer that produced the event is carried in "type"; "cat" is unchanged.
  DFT_CHECK(result.find("\"cat\":\"posix\",\"type\":3") != std::string::npos);

  std::cout << "✓ Data event serialization test passed\n" << std::endl;
}

void test_aggregated_serialization() {
  std::cout << "=== Test: Aggregated Data Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[8192];
  size_t index = 0;

  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(buffer, hostname_hash);

  // Create aggregated data structures
  Metadata* meta1 = new Metadata();
  meta1->insert_or_assign("test", std::string("value1"));

  Metadata* meta2 = new Metadata();
  meta2->insert_or_assign("test", std::string("value2"));

  AggregatedKey key1("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO,
                     1000000, 5000, 1, meta1, nullptr, nullptr);
  AggregatedKey key2("posix", "write", TraceEventType::TRACE_TYPE_LIBC_IO,
                     1000000, 2000, 1, meta2, nullptr, nullptr);

  // AggregatedDataType is map<TimeResolution, unordered_map<AggregatedKey,
  // AggregatedValues*>>
  AggregatedDataType data1, data2;
  TimeResolution time_interval = 1000000;

  // Create the inner map first
  data1[time_interval][key1] = new AggregatedValues();
  data2[time_interval][key2] = new AggregatedValues();

  // Serialize aggregated data
  char buffer1[4096];
  size_t size1 = serializer->aggregated(buffer1, index, 1234, data1);
  assert(size1 > 0);
  assert(std::string(buffer1).find("read") != std::string::npos);

  char buffer2[4096];
  size_t size2 = serializer->aggregated(buffer2, index, 1234, data2);
  assert(size2 > 0);
  assert(std::string(buffer2).find("write") != std::string::npos);

  // Clean up allocated memory
  delete data1[time_interval][key1];
  delete data2[time_interval][key2];

  std::cout << "✓ Aggregated serialization test passed\n" << std::endl;
}

void test_type_column_serialization() {
  std::cout << "=== Test: Type Column Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();
  char buffer[4096];
  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(buffer, hostname_hash);

  // A data event carries the type it was logged with, not a fixed value.
  serializer->data(buffer, 0, "read", "POSIX", TraceEventType::TRACE_TYPE_HDF5,
                   1000, 10, nullptr, 1234, 1);
  DFT_CHECK(std::string(buffer).find("\"type\":5") != std::string::npos);
  DFT_CHECK(std::string(buffer).find("\"ph\":1") != std::string::npos);

  // Counter events carry it too.
  serializer->counter(buffer, 0, "memory", "sys",
                      TraceEventType::TRACE_TYPE_PSUTIL, 1000, 1234, 1,
                      nullptr);
  {
    std::string result(buffer);
    DFT_CHECK(result.find("\"type\":7") != std::string::npos);
    DFT_CHECK(result.find("\"ph\":2") != std::string::npos);
  }

  // Metadata events are attributed to their caller rather than always being
  // tagged as internal, so metadata logged from Python reads as PYTHON.
  serializer->metadata(buffer, "epoch", "1", "CM",
                       TraceEventType::TRACE_TYPE_PYTHON, 1234, 1, false);
  {
    std::string result(buffer);
    DFT_CHECK(result.find("\"type\":6") != std::string::npos);
    DFT_CHECK(result.find("\"ph\":4") != std::string::npos);
  }

  // Internal bookkeeping stays DFTRACER.
  serializer->metadata(buffer, "corona211", "51242", "HH",
                       TraceEventType::TRACE_TYPE_DFTRACER, 1234, 1, true);
  DFT_CHECK(std::string(buffer).find("\"type\":1") != std::string::npos);

  std::cout << "\u2713 Type column serialization test passed\n" << std::endl;
}

void test_aggregated_preserves_type() {
  std::cout << "=== Test: Aggregated Preserves Type ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();
  char buffer[8192];
  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  serializer->initialize(buffer, hostname_hash);

  Metadata* meta = new Metadata();
  meta->insert_or_assign("test", std::string("value"));
  AggregatedKey key("MPIIO", "MPI_File_read", TraceEventType::TRACE_TYPE_MPI,
                    1000000, 5000, 1, meta, nullptr, nullptr);

  AggregatedDataType data;
  TimeResolution interval = 1000000;
  data[interval][key] = new AggregatedValues();

  char out[4096];
  size_t size = serializer->aggregated(out, 0, 1234, data);
  DFT_CHECK(size > 0);
  // The aggregated record keeps the type of the layer it came from, and is
  // marked AGGREGATED rather than COUNTER so the two can be told apart.
  DFT_CHECK(std::string(out).find("\"type\":10") != std::string::npos);
  DFT_CHECK(std::string(out).find("\"ph\":3") != std::string::npos);
  DFT_CHECK(std::string(out).find("\"ph\":2") == std::string::npos);

  delete data[interval][key];

  std::cout << "\u2713 Aggregated type test passed\n" << std::endl;
}

void test_finalize_serialization() {
  std::cout << "=== Test: Finalize Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[1024];

  // Test without end symbol
  size_t size1 = serializer->finalize(buffer, false);
  assert(size1 == 0);

  // Test with end symbol
  size_t size2 = serializer->finalize(buffer, true);
  assert(size2 == 1);
  assert(buffer[0] == ']');

  std::cout << "✓ Finalize serialization test passed\n" << std::endl;
}

void test_multiple_events() {
  std::cout << "=== Test: Multiple Events Serialization ===\n" << std::endl;

  auto serializer = Singleton<JsonLines>::get_instance();

  char buffer[8192];
  HashType hostname_hash = const_cast<char*>("test_hash_12345");
  size_t total_size = serializer->initialize(buffer, hostname_hash);

  // Serialize multiple data events
  for (size_t i = 0; i < 3; ++i) {
    Metadata* event_meta = new Metadata();
    event_meta->insert_or_assign("iteration", static_cast<size_t>(i));

    total_size +=
        serializer->data(buffer + total_size, i, "open", "posix",
                         TraceEventType::TRACE_TYPE_LIBC_IO,
                         1000000 + i * 10000, 5000, event_meta, 1234, 1);
  }

  // Verify buffer contains multiple events
  std::string result(buffer);
  assert(result.find("open") != std::string::npos);
  assert(total_size > 0);

  // Finalize
  total_size += serializer->finalize(buffer + total_size, true);
  assert(buffer[total_size - 1] == ']');

  std::cout << "✓ Multiple events serialization test passed\n" << std::endl;
}

int main() {
  std::cout << "\n=== Running Serialization Unit Tests ===\n" << std::endl;

  try {
    test_initialize_serialization();
    test_data_event_serialization();
    test_aggregated_serialization();
    test_type_column_serialization();
    test_aggregated_preserves_type();
    test_finalize_serialization();
    test_multiple_events();

    std::cout << "\n=== All Serialization Tests Passed ===\n" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
