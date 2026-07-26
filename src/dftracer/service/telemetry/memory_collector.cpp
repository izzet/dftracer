#include <dftracer/service/telemetry/memory_collector.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace dftracer {

void MemoryTelemetryCollector::initialize() {
  // No specific initialization needed for memory collector
}

void MemoryTelemetryCollector::capture(
    std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
    TimeResolution timestamp) {
  parseMemMetrics(timestamp, buffer_manager, logger, index);
}

void MemoryTelemetryCollector::finalize() {
  // No cleanup needed
}

void MemoryTelemetryCollector::parseMemMetrics(
    TimeResolution time, std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index) {
  int current_index = index.fetch_add(1, std::memory_order_relaxed);
  FILE* file = fopen("/proc/meminfo", "r");
  if (!file) return;

  char line[256];
  unsigned long long mem_available_current = 0;
  std::vector<std::pair<std::string, unsigned long long>> raw_values;
  auto metadata = new dftracer::Metadata();
  while (fgets(line, sizeof(line), file)) {
    char key[64];
    unsigned long long value = 0;
    if (sscanf(line, "%63[^:]: %llu", key, &value) == 2) {
      std::string k(key);
      if (k == "MemAvailable") {
        mem_available_current = value;
      }
      raw_values.emplace_back(std::move(k), value);
    }
  }

  mem_available = mem_available_current;
  metadata->insert_or_assign("MemAvailable", mem_available_current);

  for (const auto& kv : raw_values) {
    if (kv.first == "MemAvailable") {
      continue;
    }
    if (mem_available_current > 0) {
      metadata->insert_or_assign(
          kv.first,
          100.0 * static_cast<double>(kv.second) / mem_available_current);
    } else {
      metadata->insert_or_assign(kv.first, 0.0);
    }
  }

  if (!metadata->empty()) {
    buffer_manager->log_counter_event(current_index, "memory", "sys",
                                      TraceEventType::TRACE_TYPE_PSUTIL, time,
                                      0, 0, metadata);
  }
  fclose(file);
}

}  // namespace dftracer
