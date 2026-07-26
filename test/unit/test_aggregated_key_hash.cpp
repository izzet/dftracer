#include <dftracer/core/common/datastructure.h>

#include <cassert>
#include <iostream>
#include <unordered_map>

#include "check.h"

using namespace dftracer;

void test_hash_equality_basic() {
  std::cout << "=== Test: Hash Equality - Basic Fields ===\n" << std::endl;

  AggregatedKey key1("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 1, nullptr, nullptr, nullptr);
  AggregatedKey key2("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 1, nullptr, nullptr, nullptr);

  std::hash<AggregatedKey> hasher;
  size_t hash1 = hasher(key1);
  size_t hash2 = hasher(key2);

  assert(key1 == key2);
  assert(hash1 == hash2);

  std::cout << "✓ Basic hash equality test passed\n" << std::endl;
}

void test_hash_inequality_basic() {
  std::cout << "=== Test: Hash Inequality - Basic Fields ===\n" << std::endl;

  AggregatedKey key1("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 1, nullptr, nullptr, nullptr);
  AggregatedKey key2("posix", "write", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 1, nullptr, nullptr, nullptr);

  std::hash<AggregatedKey> hasher;
  size_t hash1 = hasher(key1);
  size_t hash2 = hasher(key2);

  assert(!(key1 == key2));
  assert(hash1 != hash2);

  std::cout << "✓ Basic hash inequality test passed\n" << std::endl;
}

void test_hash_equality_with_metadata() {
  std::cout << "=== Test: Hash Equality - With Metadata ===\n" << std::endl;

  Metadata metadata1;
  metadata1.insert_or_assign("rank", static_cast<int>(5));
  metadata1.insert_or_assign("file_size", static_cast<uint64_t>(1024));

  Metadata metadata2;
  metadata2.insert_or_assign("rank", static_cast<int>(5));
  metadata2.insert_or_assign("file_size", static_cast<uint64_t>(1024));

  AggregatedKey key1("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 2, &metadata1, "app1", nullptr);
  AggregatedKey key2("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 2, &metadata2, "app1", nullptr);

  std::hash<AggregatedKey> hasher;
  size_t hash1 = hasher(key1);
  size_t hash2 = hasher(key2);

  assert(key1 == key2);
  assert(hash1 == hash2);

  std::cout << "✓ Hash equality with metadata test passed\n" << std::endl;
}

void test_hash_inequality_with_different_metadata() {
  std::cout << "=== Test: Hash Inequality - Different Metadata ===\n"
            << std::endl;

  Metadata metadata1;
  metadata1.insert_or_assign("rank", static_cast<int>(5));

  Metadata metadata2;
  metadata2.insert_or_assign("rank", static_cast<int>(7));

  AggregatedKey key1("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 2, &metadata1, "app1", nullptr);
  AggregatedKey key2("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 2, &metadata2, "app1", nullptr);

  std::hash<AggregatedKey> hasher;
  size_t hash1 = hasher(key1);
  size_t hash2 = hasher(key2);

  assert(!(key1 == key2));
  // Different metadata should (very likely) produce different hashes
  // Note: hash collisions are possible but extremely unlikely
  assert(hash1 != hash2);

  std::cout << "✓ Hash inequality with different metadata test passed\n"
            << std::endl;
}

void test_hash_in_unordered_map() {
  std::cout << "=== Test: Hash in Unordered Map ===\n" << std::endl;

  Metadata metadata1;
  metadata1.insert_or_assign("operation", std::string("read"));

  Metadata metadata2;
  metadata2.insert_or_assign("operation", std::string("read"));

  AggregatedKey key1("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 1, &metadata1, nullptr, nullptr);
  AggregatedKey key2("posix", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 1, &metadata2, nullptr, nullptr);

  std::unordered_map<AggregatedKey, int> map;
  map[key1] = 10;

  // key2 should find the same entry since key1 == key2 and hash1 == hash2
  assert(map.find(key2) != map.end());
  assert(map[key2] == 10);

  std::cout << "✓ Hash in unordered map test passed\n" << std::endl;
}

void test_hash_contract() {
  std::cout << "=== Test: Hash Contract (Equal Objects Have Equal Hashes) ===\n"
            << std::endl;

  // Generate multiple identical keys with metadata
  for (int i = 0; i < 5; ++i) {
    Metadata metadata;
    metadata.insert_or_assign("batch", static_cast<int>(i));

    AggregatedKey key1("posix", "write", TraceEventType::TRACE_TYPE_LIBC_IO,
                       200, 100, 3, &metadata, "test_app", nullptr);
    AggregatedKey key2("posix", "write", TraceEventType::TRACE_TYPE_LIBC_IO,
                       200, 100, 3, &metadata, "test_app", nullptr);

    std::hash<AggregatedKey> hasher;
    size_t hash1 = hasher(key1);
    size_t hash2 = hasher(key2);

    assert(key1 == key2);
    assert(hash1 == hash2);
  }

  std::cout << "✓ Hash contract test passed\n" << std::endl;
}

void test_type_distinguishes_keys() {
  std::cout << "=== Test: type participates in key identity ===" << std::endl;

  // Two events identical except for the layer that produced them must not
  // collapse into one aggregated bucket, otherwise e.g. a POSIX read and an
  // HDF5 read of the same name would be summed together.
  AggregatedKey key1("read", "read", TraceEventType::TRACE_TYPE_LIBC_IO, 100,
                     50, 1, nullptr, nullptr, nullptr);
  AggregatedKey key2("read", "read", TraceEventType::TRACE_TYPE_HDF5, 100, 50,
                     1, nullptr, nullptr, nullptr);

  DFT_CHECK(!(key1 == key2));

  std::hash<AggregatedKey> hasher;
  DFT_CHECK(hasher(key1) != hasher(key2));

  // And they occupy separate slots in the map the aggregator actually uses.
  std::unordered_map<AggregatedKey, int> map;
  map[key1] = 1;
  map[key2] = 2;
  DFT_CHECK(map.size() == 2);

  std::cout << "✓ type distinguishes aggregation keys" << std::endl;
}

int main() {
  try {
    test_hash_equality_basic();
    test_hash_inequality_basic();
    test_hash_equality_with_metadata();
    test_hash_inequality_with_different_metadata();
    test_hash_in_unordered_map();
    test_hash_contract();
    test_type_distinguishes_keys();

    std::cout << "\n✓✓✓ All AggregatedKey hash tests passed! ✓✓✓\n"
              << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "✗ Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
