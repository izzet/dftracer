#include <dftracer/service/telemetry/network_collector.h>

#include <cstdio>
#include <string>

namespace dftracer {

void NetworkTelemetryCollector::initialize() {
  // Initialize with baseline network metrics
  FILE* file = fopen("/proc/net/dev", "r");
  if (!file) return;

  char line[256];
  // Skip first two header lines; a file too short to have them has no data.
  if (fgets(line, sizeof(line), file) == nullptr ||
      fgets(line, sizeof(line), file) == nullptr) {
    fclose(file);
    return;
  }

  while (fgets(line, sizeof(line), file)) {
    char iface[32];
    NetworkMetrics metrics;
    unsigned long long rx_fifo = 0;
    unsigned long long rx_frame = 0;
    unsigned long long rx_compressed = 0;
    unsigned long long rx_multicast = 0;
    unsigned long long tx_fifo = 0;
    unsigned long long tx_carrier = 0;
    unsigned long long tx_compressed = 0;

    if (sscanf(line,
               "%31[^:]: %llu %llu %llu %llu %llu %llu %llu %llu "
               "%llu %llu %llu %llu %llu %llu %llu %llu",
               iface, &metrics.rx_bytes, &metrics.rx_packets,
               &metrics.rx_errors, &metrics.rx_dropped, &rx_fifo, &rx_frame,
               &rx_compressed, &rx_multicast, &metrics.tx_bytes,
               &metrics.tx_packets, &metrics.tx_errors, &metrics.tx_dropped,
               &tx_fifo, &metrics.collisions, &tx_carrier,
               &tx_compressed) >= 2) {
      // Trim whitespace from interface name
      std::string if_name(iface);
      if_name.erase(if_name.find_last_not_of(" \n\r\t") + 1);
      previous_net_metrics[if_name] = metrics;
    }
  }
  fclose(file);
}

void NetworkTelemetryCollector::capture(
    std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index,
    TimeResolution timestamp) {
  parseNetworkMetrics(timestamp, buffer_manager, logger, index);
}

void NetworkTelemetryCollector::finalize() { previous_net_metrics.clear(); }

void NetworkTelemetryCollector::parseNetworkMetrics(
    TimeResolution time, std::shared_ptr<BufferManager> buffer_manager,
    std::shared_ptr<DFTLogger> logger, std::atomic<int>& index) {
  FILE* file = fopen("/proc/net/dev", "r");
  if (!file) return;

  char line[256];
  // Skip header lines; a file too short to have them has no data.
  if (fgets(line, sizeof(line), file) == nullptr ||
      fgets(line, sizeof(line), file) == nullptr) {
    fclose(file);
    return;
  }

  while (fgets(line, sizeof(line), file)) {
    char iface[32];
    NetworkMetrics metrics;
    unsigned long long rx_fifo = 0;
    unsigned long long rx_frame = 0;
    unsigned long long rx_compressed = 0;
    unsigned long long rx_multicast = 0;
    unsigned long long tx_fifo = 0;
    unsigned long long tx_carrier = 0;
    unsigned long long tx_compressed = 0;

    if (sscanf(line,
               "%31[^:]: %llu %llu %llu %llu %llu %llu %llu %llu "
               "%llu %llu %llu %llu %llu %llu %llu %llu",
               iface, &metrics.rx_bytes, &metrics.rx_packets,
               &metrics.rx_errors, &metrics.rx_dropped, &rx_fifo, &rx_frame,
               &rx_compressed, &rx_multicast, &metrics.tx_bytes,
               &metrics.tx_packets, &metrics.tx_errors, &metrics.tx_dropped,
               &tx_fifo, &metrics.collisions, &tx_carrier,
               &tx_compressed) < 2) {
      continue;
    }

    // Trim whitespace from interface name
    std::string if_name(iface);
    if_name.erase(if_name.find_last_not_of(" \n\r\t") + 1);

    // Skip loopback for per-interface metrics
    if (if_name == "lo") {
      continue;
    }

    auto metadata = new dftracer::Metadata();
    auto it = previous_net_metrics.find(if_name);
    if (it != previous_net_metrics.end()) {
      // Calculate deltas
      metadata->insert_or_assign(
          "bytes_recv",
          static_cast<double>(metrics.rx_bytes - it->second.rx_bytes));
      metadata->insert_or_assign(
          "packets_recv",
          static_cast<double>(metrics.rx_packets - it->second.rx_packets));
      metadata->insert_or_assign(
          "rx_errors",
          static_cast<double>(metrics.rx_errors - it->second.rx_errors));
      metadata->insert_or_assign(
          "rx_dropped",
          static_cast<double>(metrics.rx_dropped - it->second.rx_dropped));
      metadata->insert_or_assign(
          "bytes_sent",
          static_cast<double>(metrics.tx_bytes - it->second.tx_bytes));
      metadata->insert_or_assign(
          "packets_sent",
          static_cast<double>(metrics.tx_packets - it->second.tx_packets));
      metadata->insert_or_assign(
          "tx_errors",
          static_cast<double>(metrics.tx_errors - it->second.tx_errors));
      metadata->insert_or_assign(
          "tx_dropped",
          static_cast<double>(metrics.tx_dropped - it->second.tx_dropped));
      metadata->insert_or_assign(
          "collisions",
          static_cast<double>(metrics.collisions - it->second.collisions));
    } else {
      metadata->insert_or_assign("bytes_recv",
                                 static_cast<double>(metrics.rx_bytes));
      metadata->insert_or_assign("packets_recv",
                                 static_cast<double>(metrics.rx_packets));
      metadata->insert_or_assign("rx_errors",
                                 static_cast<double>(metrics.rx_errors));
      metadata->insert_or_assign("rx_dropped",
                                 static_cast<double>(metrics.rx_dropped));
      metadata->insert_or_assign("bytes_sent",
                                 static_cast<double>(metrics.tx_bytes));
      metadata->insert_or_assign("packets_sent",
                                 static_cast<double>(metrics.tx_packets));
      metadata->insert_or_assign("tx_errors",
                                 static_cast<double>(metrics.tx_errors));
      metadata->insert_or_assign("tx_dropped",
                                 static_cast<double>(metrics.tx_dropped));
      metadata->insert_or_assign("collisions",
                                 static_cast<double>(metrics.collisions));
    }

    previous_net_metrics[if_name] = metrics;

    std::string net_name = "net-" + if_name;
    int current_index = index.fetch_add(1, std::memory_order_relaxed);
    buffer_manager->log_counter_event(current_index, net_name.c_str(), "net",
                                      TraceEventType::TRACE_TYPE_PSUTIL, time,
                                      0, 0, metadata);
  }
  fclose(file);
}

}  // namespace dftracer
