#include <dftracer/service/telemetry/cpu_collector.h>

#include <cstdio>

namespace dftracer {

void CPUTelemetryCollector::initialize() {
  // No specific initialization needed for CPU collector
}

void CPUTelemetryCollector::capture(
    std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
    TimeResolution timestamp) {
  parseCpuMetrics(timestamp, buffer_manager, logger, index);
}

void CPUTelemetryCollector::finalize() {
  // No cleanup needed
}

void CPUTelemetryCollector::parseCpuMetrics(
    TimeResolution time, std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index) {
  FILE* file = fopen("/proc/stat", "r");
  if (!file) return;

  char line[256];
  while (fgets(line, sizeof(line), file)) {
    std::string str(line);

    // Aggregate metrics for all CPUs
    if (str.find("cpu ") == 0) {
      CpuMetrics metrics;
      sscanf(str.c_str(),
             "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
             &metrics.user, &metrics.nice, &metrics.system, &metrics.idle,
             &metrics.iowait, &metrics.irq, &metrics.softirq, &metrics.steal,
             &metrics.guest, &metrics.guest_nice);

      // Calculate total jiffies for all CPUs
      unsigned long long total_jiffies =
          metrics.user + metrics.nice + metrics.system + metrics.idle +
          metrics.iowait + metrics.irq + metrics.softirq + metrics.steal +
          metrics.guest + metrics.guest_nice;

      if (total_jiffies == 0) total_jiffies = 1;

      auto metadata = new dftracer::Metadata();
      metadata->insert_or_assign("user_pct",
                                 100.0 * metrics.user / total_jiffies);
      metadata->insert_or_assign("nice_pct",
                                 100.0 * metrics.nice / total_jiffies);
      metadata->insert_or_assign("system_pct",
                                 100.0 * metrics.system / total_jiffies);
      metadata->insert_or_assign("idle_pct",
                                 100.0 * metrics.idle / total_jiffies);
      metadata->insert_or_assign("iowait_pct",
                                 100.0 * metrics.iowait / total_jiffies);
      metadata->insert_or_assign("irq_pct",
                                 100.0 * metrics.irq / total_jiffies);
      metadata->insert_or_assign("softirq_pct",
                                 100.0 * metrics.softirq / total_jiffies);
      metadata->insert_or_assign("steal_pct",
                                 100.0 * metrics.steal / total_jiffies);
      metadata->insert_or_assign("guest_pct",
                                 100.0 * metrics.guest / total_jiffies);
      metadata->insert_or_assign("guest_nice_pct",
                                 100.0 * metrics.guest_nice / total_jiffies);

      int current_index = index.fetch_add(1, std::memory_order_relaxed);
      buffer_manager->log_counter_event(current_index, "cpu", "sys",
                                        TraceEventType::TRACE_TYPE_PSUTIL, time,
                                        0, 0, metadata);
    }
    // Per-CPU metrics (e.g., cpu0, cpu1, ...)
    else if (str.find("cpu") == 0 && isdigit(str[3])) {
      CpuMetrics cpu;
      int cpu_index = -1;
      sscanf(str.c_str(),
             "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
             &cpu_index, &cpu.user, &cpu.nice, &cpu.system, &cpu.idle,
             &cpu.iowait, &cpu.irq, &cpu.softirq, &cpu.steal, &cpu.guest,
             &cpu.guest_nice);

      unsigned long long total_jiffies =
          cpu.user + cpu.nice + cpu.system + cpu.idle + cpu.iowait + cpu.irq +
          cpu.softirq + cpu.steal + cpu.guest + cpu.guest_nice;

      if (total_jiffies == 0) total_jiffies = 1;

      auto metadata = new dftracer::Metadata();
      metadata->insert_or_assign("user_pct", 100.0 * cpu.user / total_jiffies);
      metadata->insert_or_assign("system_pct",
                                 100.0 * cpu.system / total_jiffies);
      metadata->insert_or_assign("idle_pct", 100.0 * cpu.idle / total_jiffies);
      metadata->insert_or_assign("iowait_pct",
                                 100.0 * cpu.iowait / total_jiffies);
      metadata->insert_or_assign("irq_pct", 100.0 * cpu.irq / total_jiffies);
      metadata->insert_or_assign("softirq_pct",
                                 100.0 * cpu.softirq / total_jiffies);
      metadata->insert_or_assign("steal_pct",
                                 100.0 * cpu.steal / total_jiffies);

      std::string cpu_name = "cpu-" + std::to_string(cpu_index);
      int current_index = index.fetch_add(1, std::memory_order_relaxed);
      buffer_manager->log_counter_event(current_index, cpu_name.c_str(), "sys",
                                        TraceEventType::TRACE_TYPE_PSUTIL, time,
                                        0, 0, metadata);
    }
  }
  fclose(file);
}

}  // namespace dftracer
