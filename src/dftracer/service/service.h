#ifndef DFTRACER_SERVER
#define DFTRACER_SERVER

#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/df_logger.h>
#include <dftracer/core/utils/configuration_manager.h>
#include <dftracer/service/common/datastructure.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dftracer {

// Main service class for DFTracer server
class DFTracerService {
 public:
  // Constructor: initializes configuration, logger, and buffer manager
  DFTracerService() : index{0}, interval(1000), running(false) {
    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    conf->metadata = true;
    conf->enable = true;
    conf->compression = false;
    conf->write_buffer_size = 16 * 1024 * 1024;
    interval = conf->trace_interval_ms;
    if (conf->log_file.empty()) {
      throw std::runtime_error(
          "Configuration error: Please set the log_file prefix in the "
          "configuration.");
    } else {
      // Get hostname and append to log_file for uniqueness
      char hostname[256];
      if (gethostname(hostname, sizeof(hostname)) == 0) {
        conf->log_file += std::string("_") + hostname;
      } else {
        throw std::runtime_error("Failed to get hostname for log_file.");
      }
      // Add file extension based on compression setting
      if (conf->compression) {
        conf->log_file += ".pfw.gz";
      } else {
        conf->log_file += ".pfw";
      }
      // Delete the file if it exists
      if (FILE* f = fopen(conf->log_file.c_str(), "r")) {
        fclose(f);
        if (remove(conf->log_file.c_str()) != 0) {
          throw std::runtime_error("Failed to delete existing log file: " +
                                   conf->log_file);
        }
      }
      logger = DFT_LOGGER_INIT();
      buffer_manager =
          dftracer::Singleton<dftracer::BufferManager>::get_instance();
      auto hostname_hash = logger->get_hash(hostname);
      this->buffer_manager->initialize(conf->log_file.c_str(), hostname_hash);
    }
  }

  // Destructor: ensures the service is stopped and resources are cleaned up
  ~DFTracerService() { stop(); }

  // Starts the background worker thread for periodic metric collection
  void start() {
    running = true;
    worker = std::thread([this]() { this->progressEngine(); });
  }

  // Stops the worker thread and finalizes the buffer manager
  void stop() {
    running = false;
    if (worker.joinable()) worker.join();
    this->buffer_manager->finalize(index.load(std::memory_order_relaxed), true);
#if DFTRACER_WRITER_TYPE_STDIO
    if (auto conf =
            dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
        conf) {
      std::string cmd = "gzip -f " + conf->log_file;
      int ret = system(cmd.c_str());
      if (ret != 0) {
        fprintf(stderr, "Warning: gzip compression failed for %s\n",
                conf->log_file.c_str());
      }
    }
#endif
  }

 private:
  std::shared_ptr<DFTLogger> logger;  // Logger instance
  std::atomic<int> index;             // Event index counter
  unsigned int interval;      // Interval between metric collections (ms)
  std::atomic<bool> running;  // Flag to control worker thread
  std::thread worker;         // Worker thread for metric collection
  std::mutex data_mutex;      // Mutex for thread safety (not used here)
  std::shared_ptr<dftracer::BufferManager> buffer_manager;  // Buffer manager

  // Main loop for periodic metric collection
  void progressEngine() {
    int step = 1;
    while (running) {
      TimeResolution time = logger->get_time();
      printf("Capturing step: %d, timestep: %lld, interval: %u ms\r", step,
             static_cast<long long>(time), interval);
      fflush(stdout);
      getCpuMetrics(time);  // Collect CPU metrics
      getMemMetrics(time);  // Collect memory metrics
      buffer_manager->flush();  // Push buffered events to writer (Mofka/STDIO)
      std::this_thread::sleep_for(std::chrono::milliseconds(interval));
      ++step;
    }
  }

  // Collects CPU metrics from /proc/stat and logs them
  void getCpuMetrics(TimeResolution time) {
    FILE* file = fopen("/proc/stat", "r");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
      std::string str(line);
      if (str.find("cpu ") == 0) {
        // Aggregate metrics for all CPUs
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

        if (total_jiffies == 0) total_jiffies = 1;  // Avoid division by zero

        auto* metadata = new dftracer::Metadata();
        // Store each metric as a percentage of total jiffies
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
        buffer_manager->log_counter_event(current_index, "cpu", "sys", time, 0,
                                          0, metadata);
      } else if (str.find("cpu") == 0 && isdigit(str[3])) {
        // Per-CPU metrics (e.g., cpu0, cpu1, ...)
        CpuMetrics cpu;
        int cpu_index = -1;
        // Parse per-CPU metrics
        sscanf(str.c_str(),
               "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
               &cpu_index, &cpu.user, &cpu.nice, &cpu.system, &cpu.idle,
               &cpu.iowait, &cpu.irq, &cpu.softirq, &cpu.steal, &cpu.guest,
               &cpu.guest_nice);

        // Calculate total jiffies for this CPU
        unsigned long long total_jiffies =
            cpu.user + cpu.nice + cpu.system + cpu.idle + cpu.iowait + cpu.irq +
            cpu.softirq + cpu.steal + cpu.guest + cpu.guest_nice;

        // Avoid division by zero
        if (total_jiffies == 0) total_jiffies = 1;

        // Convert each metric to percentage
        double user_pct = 100.0 * cpu.user / total_jiffies;
        double system_pct = 100.0 * cpu.system / total_jiffies;
        double idle_pct = 100.0 * cpu.idle / total_jiffies;
        double iowait_pct = 100.0 * cpu.iowait / total_jiffies;
        double irq_pct = 100.0 * cpu.irq / total_jiffies;
        double softirq_pct = 100.0 * cpu.softirq / total_jiffies;
        double steal_pct = 100.0 * cpu.steal / total_jiffies;

        auto* metadata = new dftracer::Metadata();
        metadata->insert_or_assign("user_pct", user_pct);
        metadata->insert_or_assign("system_pct", system_pct);
        metadata->insert_or_assign("idle_pct", idle_pct);
        metadata->insert_or_assign("iowait_pct", iowait_pct);
        metadata->insert_or_assign("irq_pct", irq_pct);
        metadata->insert_or_assign("softirq_pct", softirq_pct);
        metadata->insert_or_assign("steal_pct", steal_pct);

        std::string cpu_name = "cpu-" + std::to_string(cpu_index);
        int current_index = index.fetch_add(1, std::memory_order_relaxed);
        buffer_manager->log_counter_event(current_index, cpu_name.c_str(),
                                          "sys", time, 0, 0, metadata);
      }
    }
    fclose(file);
  }

  // Collects memory metrics from /proc/meminfo and logs them
  void getMemMetrics(TimeResolution time) {
    auto current_index = this->index.fetch_add(1, std::memory_order_relaxed);
    FILE* file = fopen("/proc/meminfo", "r");
    if (!file) return;
    char line[256];
    auto* metadata = new dftracer::Metadata();
    while (fgets(line, sizeof(line), file)) {
      char key[64];
      unsigned long long value = 0;
      if (sscanf(line, "%63[^:]: %llu", key, &value) == 2) {
        std::string k(key);
        static unsigned long long mem_available = 0;
        if (k == "MemAvailable") {
          mem_available = value;
          metadata->insert_or_assign("MemAvailable", mem_available);
        } else {
          // Convert to percentage of MemAvailable if MemAvailable is known and
          // nonzero
          if (mem_available > 0) {
            metadata->insert_or_assign(
                k, 100.0 * static_cast<double>(value) / mem_available);
          } else {
            metadata->insert_or_assign(k, 0.0);
          }
        }
      }
    }
    if (!metadata->empty()) {
      buffer_manager->log_counter_event(current_index, "memory", "sys", time, 0,
                                        0, metadata);
    } else {
      delete metadata;
    }
    fclose(file);
    return;
  }
};
}  // namespace dftracer

#endif  // DFTRACER_SERVER
