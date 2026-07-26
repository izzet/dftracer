#include <dftracer/service/telemetry/io_collector.h>

#include <cstdio>
#include <string>

namespace dftracer {

void IOTelemetryCollector::initialize() {
  // Initialize with baseline I/O metrics
  FILE* file = fopen("/proc/diskstats", "r");
  if (!file) return;

  char line[256];
  while (fgets(line, sizeof(line), file)) {
    unsigned int major, minor;
    char dev_name[32];
    IOMetrics metrics;
    unsigned long long reads_merged = 0;
    unsigned long long read_sectors = 0;
    unsigned long long writes_merged = 0;
    unsigned long long write_sectors = 0;
    unsigned long long io_time_ms = 0;
    unsigned long long weighted_io_time_ms = 0;

    if (sscanf(line,
               "%u %u %31s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu "
               "%llu",
               &major, &minor, dev_name, &metrics.read_count, &reads_merged,
               &read_sectors, &metrics.read_time_ms, &metrics.write_count,
               &writes_merged, &write_sectors, &metrics.write_time_ms,
               &metrics.io_in_progress, &io_time_ms,
               &weighted_io_time_ms) == 14) {
      // Linux /proc/diskstats reports sectors; convert to bytes.
      metrics.read_bytes = read_sectors * 512ULL;
      metrics.write_bytes = write_sectors * 512ULL;
      previous_io_metrics[dev_name] = metrics;
    }
  }
  fclose(file);
}

void IOTelemetryCollector::capture(
    std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
    TimeResolution timestamp) {
  parseIOMetrics(timestamp, buffer_manager, logger, index);
}

void IOTelemetryCollector::finalize() { previous_io_metrics.clear(); }

void IOTelemetryCollector::parseIOMetrics(
    TimeResolution time, std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index) {
  FILE* file = fopen("/proc/diskstats", "r");
  if (!file) return;

  char line[256];
  while (fgets(line, sizeof(line), file)) {
    unsigned int major, minor;
    char dev_name[32];
    IOMetrics metrics;
    unsigned long long reads_merged = 0;
    unsigned long long read_sectors = 0;
    unsigned long long writes_merged = 0;
    unsigned long long write_sectors = 0;
    unsigned long long io_time_ms = 0;
    unsigned long long weighted_io_time_ms = 0;

    // Parse Linux /proc/diskstats columns 1-14.
    if (sscanf(line,
               "%u %u %31s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu "
               "%llu",
               &major, &minor, dev_name, &metrics.read_count, &reads_merged,
               &read_sectors, &metrics.read_time_ms, &metrics.write_count,
               &writes_merged, &write_sectors, &metrics.write_time_ms,
               &metrics.io_in_progress, &io_time_ms,
               &weighted_io_time_ms) != 14) {
      continue;
    }

    // Linux /proc/diskstats reports sectors; convert to bytes.
    metrics.read_bytes = read_sectors * 512ULL;
    metrics.write_bytes = write_sectors * 512ULL;

    // Skip loop and ram devices
    if (std::string(dev_name).find("loop") != std::string::npos ||
        std::string(dev_name).find("ram") != std::string::npos) {
      continue;
    }

    auto metadata = new dftracer::Metadata();
    auto it = previous_io_metrics.find(dev_name);
    if (it != previous_io_metrics.end()) {
      // Calculate deltas
      metadata->insert_or_assign(
          "reads_completed",
          static_cast<double>(metrics.read_count - it->second.read_count));
      metadata->insert_or_assign(
          "bytes_read",
          static_cast<double>(metrics.read_bytes - it->second.read_bytes));
      metadata->insert_or_assign(
          "writes_completed",
          static_cast<double>(metrics.write_count - it->second.write_count));
      metadata->insert_or_assign(
          "bytes_written",
          static_cast<double>(metrics.write_bytes - it->second.write_bytes));
    }

    metadata->insert_or_assign("ios_in_progress",
                               static_cast<double>(metrics.io_in_progress));

    previous_io_metrics[dev_name] = metrics;

    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(current_index, dev_name, "io",
                                      TraceEventType::TRACE_TYPE_PSUTIL, time,
                                      0, 0, metadata);
  }
  fclose(file);
}

}  // namespace dftracer
