#include <dftracer/core/buffer/buffer.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <dftracer/core/dftracer_config.hpp>
#include <string>

// Lightweight overhead counters (thread-safe, no locking).
static std::atomic<uint64_t> g_log_event_count{0};
static std::atomic<uint64_t> g_log_event_ns{0};
static std::atomic<uint64_t> g_lock_wait_ns{0};
static std::atomic<uint64_t> g_serialize_ns{0};
static std::atomic<uint64_t> g_flush_count{0};
static std::atomic<uint64_t> g_flush_ns{0};
static std::atomic<uint64_t> g_trigger_count{0};
static std::atomic<uint64_t> g_trigger_ns{0};
static std::atomic<uint64_t> g_trigger_flush_data_ns{0};
static std::atomic<uint64_t> g_trigger_push_ctrl_ns{0};
static std::atomic<uint64_t> g_trigger_flush_ctrl_ns{0};
static const uint64_t REPORT_INTERVAL = 50000;  // print every N events

#if DFTRACER_WRITER_TYPE_MOFKA
#include <dftracer/core/writer/mofka_writer.h>
#elif DFTRACER_WRITER_TYPE_SHM
#include <dftracer/core/writer/shm_ring_writer.h>
#elif DFTRACER_WRITER_TYPE_ZMQ
#include <dftracer/core/writer/zmq_writer.h>
#else
#include <dftracer/core/writer/stdio_writer.h>
#endif

template <>
std::shared_ptr<dftracer::BufferManager>
    dftracer::Singleton<dftracer::BufferManager>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::BufferManager>::stop_creating_instances =
    false;
namespace dftracer {

namespace {

bool extract_metadata_integer(const dftracer::Metadata* metadata,
                              const char* key, long long* value,
                              bool allow_string = true) {
  if (!metadata || !key || !value) return false;
  auto it = metadata->find(key);
  if (it == metadata->end()) return false;
  const std::any& raw_value = it->second.second;
  if (raw_value.type() == typeid(int)) {
    *value = std::any_cast<int>(raw_value);
    return true;
  }
  if (raw_value.type() == typeid(long)) {
    *value = std::any_cast<long>(raw_value);
    return true;
  }
  if (raw_value.type() == typeid(long long)) {
    *value = std::any_cast<long long>(raw_value);
    return true;
  }
  if (raw_value.type() == typeid(unsigned int)) {
    *value = std::any_cast<unsigned int>(raw_value);
    return true;
  }
  if (raw_value.type() == typeid(unsigned long)) {
    *value = std::any_cast<unsigned long>(raw_value);
    return true;
  }
  if (raw_value.type() == typeid(unsigned long long)) {
    *value = std::any_cast<unsigned long long>(raw_value);
    return true;
  }
  if (allow_string && raw_value.type() == typeid(std::string)) {
    try {
      *value = std::stoll(std::any_cast<std::string>(raw_value));
      return true;
    } catch (...) {
      return false;
    }
  }
  return false;
}

}  // namespace

static std::atomic<uint64_t> g_forced_flush_count{0};
static std::atomic<uint64_t> g_forced_flush_ns{0};
static std::atomic<uint64_t> g_forced_flush_bytes{0};
static std::atomic<uint64_t> g_timed_flush_count{0};
static std::atomic<uint64_t> g_timed_flush_ns{0};
static std::atomic<uint64_t> g_timed_flush_bytes{0};

void BufferManager::compress_and_write_if_needed(size_t size, bool force) {
  // A BUFFER THAT FLUSHES ONLY WHEN FULL IS INVISIBLE TO A WINDOWED ANALYZER.
  //
  // There are exactly two ways a buffer left this process before this: it filled
  // `write_buffer_size` (16 MiB by default), or an event named in
  // DFTRACER_SHM_CONTROL_EVENT_NAMES forced it. The second is what makes a training rank's
  // events arrive window by window, because the rank emits window.start/window.stop.
  //
  // A FORKED CHILD EMITS NEITHER. Measured on un_forkgate3_n2, 2 nodes, unet3d, one epoch
  // (T3-0 section 4q.5): 4 pids logged control events and their persisted records span 1.1 s
  // each, while 45 forked DataLoader workers logged none and each one's ENTIRE 473 s of events
  // arrived as a single 16 MiB record at worker exit. Downstream that is 87 of 91 windows
  // reporting `no_read_observed` while the run really was reading, and the four windows that
  // did see bytes reporting up to 99 GB/s, because a whole epoch of a worker's reads was
  // credited to one short window. The flush fix in 1396409 made the child's data ARRIVE; it
  // did not make it arrive in time to be attributed to the window it happened in.
  //
  // So the buffer also flushes on TIME. No thread and no timer is added: the check rides the
  // event path, which already takes four steady_clock readings per event, and therefore adds no
  // new fork-safety surface -- which matters, because a redundant call on a singleton inherited
  // across fork is exactly what 4o.5 spent a day suspecting of a deadlock.
  //
  // The interval is an instrument buffering parameter, not a decision rule: nothing compares it
  // against a domain metric, and its only requirement is to be shorter than the analysis window
  // so that a window's bytes are in the ring when that window closes. Its cost is ring-write
  // frequency: at the measured 151 events per second per worker, one second of buffering is
  // about 35 KiB per record.
  //
  // HONEST LIMIT OF A THREADLESS TIMER: the check rides the event path, so a process that stops
  // producing events entirely keeps whatever is in its buffer until it produces another one or
  // exits. Staleness is bounded only while the process is active, which is the case that matters
  // here (a worker that has stopped reading has nothing the analyzer is waiting for), and the
  // exit flush from 1396409 still catches the rest.
  bool timed = false;
  if (!force && flush_interval_ns > 0 && buffer_pos > 0) {
    auto now = std::chrono::steady_clock::now();
    uint64_t since = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_flush).count();
    if (since >= flush_interval_ns) timed = true;
  }
  if (force || timed || buffer_pos + size > this->config->write_buffer_size) {
    if (this->config->compression) {
      size = this->compressor->compress(buffer, buffer_pos + size);
    } else {
      size = buffer_pos + size;
    }
    if (size > 0) {
      auto t0 = std::chrono::steady_clock::now();
      size = this->writer->write(buffer, size, true);
      auto t1 = std::chrono::steady_clock::now();
      uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
      g_flush_ns.fetch_add(ns, std::memory_order_relaxed);
      g_flush_count.fetch_add(1, std::memory_order_relaxed);
      if (force) {
        g_forced_flush_ns.fetch_add(ns, std::memory_order_relaxed);
        g_forced_flush_count.fetch_add(1, std::memory_order_relaxed);
        g_forced_flush_bytes.fetch_add(size, std::memory_order_relaxed);
      } else if (timed) {
        // Counted apart from the control-trigger flushes so the two are never read as one
        // number: a control flush says a window closed, a timed flush says a buffer aged out.
        g_timed_flush_ns.fetch_add(ns, std::memory_order_relaxed);
        g_timed_flush_count.fetch_add(1, std::memory_order_relaxed);
        g_timed_flush_bytes.fetch_add(size, std::memory_order_relaxed);
      }
    }
    buffer_pos = 0;
    last_flush = std::chrono::steady_clock::now();
  } else {
    buffer_pos += size;
  }
}
int BufferManager::initialize(const char* filename, HashType hostname_hash) {
  DFTRACER_LOG_DEBUG("BufferManager.initialize %s %d", filename, hostname_hash);
  this->config =
      dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
  if (buffer == nullptr) {
    buffer = (char*)malloc(this->config->write_buffer_size + 16 * 1024);
  }
  buffer_pos = 0;
  // Milliseconds, 0 disables. Default 1000: an order of magnitude below the shortest analysis
  // window this project runs (about 5 s on staged cosmoflow), so a window's events are in the
  // ring before the analyzer drains it.
  {
    const char* iv = std::getenv("DFTRACER_FLUSH_INTERVAL_MS");
    long long ms = 1000;
    if (iv && *iv) {
      try {
        ms = std::stoll(iv);
      } catch (...) {
        ms = 1000;
      }
    }
    flush_interval_ns = ms > 0 ? static_cast<uint64_t>(ms) * 1000000ull : 0;
  }
  last_flush = std::chrono::steady_clock::now();
  if (!buffer) {
    DFTRACER_LOG_ERROR("BufferManager.BufferManager Failed to allocate buffer",
                       "");
  }
#if DFTRACER_WRITER_TYPE_MOFKA
  this->writer = std::static_pointer_cast<dftracer::WriterInterface>(
      dftracer::Singleton<dftracer::MofkaWriter>::get_instance());
#elif DFTRACER_WRITER_TYPE_ZMQ
  this->writer = std::static_pointer_cast<dftracer::WriterInterface>(
      dftracer::Singleton<dftracer::ZMQWriter>::get_instance());
#elif DFTRACER_WRITER_TYPE_SHM
  this->writer = std::static_pointer_cast<dftracer::WriterInterface>(
      dftracer::Singleton<dftracer::ShmRingWriter>::get_instance());
#else
  this->writer = std::static_pointer_cast<dftracer::WriterInterface>(
      dftracer::Singleton<dftracer::STDIOWriter>::get_instance());
#endif
  this->writer->initialize(filename);
  this->control_hooks_enabled = this->writer->control_hooks_enabled();
  this->serializer = dftracer::Singleton<dftracer::JsonLines>::get_instance();
  this->aggregator = dftracer::Singleton<dftracer::Aggregator>::get_instance();
  if (this->config->compression) {
    this->compressor =
        dftracer::Singleton<dftracer::ZlibCompression>::get_instance();
    this->compressor->initialize(this->config->write_buffer_size);
  }
  size_t size = this->serializer->initialize(buffer, hostname_hash);
  compress_and_write_if_needed(size);
  return 0;
}

int BufferManager::finalize(int index, ProcessID process_id, bool end_sym) {
#if DFTRACER_WRITER_TYPE_MOFKA || DFTRACER_WRITER_TYPE_SHM
  // Streaming writers carry no trailing "]" -- the reader consumes records, not a JSON document.
  end_sym = false;
#endif
  std::unique_lock<std::shared_mutex> lock(mtx);
  if (buffer) {
    size_t size = 0;
    if (this->config->aggregation_enable) {
      auto data = dftracer::AggregatedDataType();
      this->aggregator->get_previous_aggregations(data, true);
      size = this->serializer->aggregated(buffer + buffer_pos, index,
                                          process_id, data);
      this->aggregator->finalize();
    }
    auto end_size =
        this->serializer->finalize(buffer + buffer_pos + size, end_sym);
    compress_and_write_if_needed(size + end_size, true);
    if (this->config->compression) this->compressor->finalize();
    this->writer->finalize(index);
    free(buffer);
    buffer = nullptr;
    buffer_pos = 0;
  }
  return 0;
}

void BufferManager::log_data_event(int index, ConstEventNameType event_name,
                                   ConstEventNameType category,
                                   TimeResolution start_time,
                                   TimeResolution duration,
                                   dftracer::Metadata* metadata,
                                   ProcessID process_id, ThreadID tid) {
  auto t_entry = std::chrono::steady_clock::now();
  std::unique_lock<std::shared_mutex> lock(mtx);
  auto t_locked = std::chrono::steady_clock::now();
  g_lock_wait_ns.fetch_add(
      std::chrono::duration_cast<std::chrono::nanoseconds>(t_locked - t_entry).count(),
      std::memory_order_relaxed);
  DFTRACER_LOG_DEBUG("BufferManager.log_data_event %d", index);
  size_t size = 0;
  long long step = 0;
  long long epoch = 0;
  const bool has_step = extract_metadata_integer(metadata, "step", &step);
  const bool has_epoch = extract_metadata_integer(metadata, "epoch", &epoch);
  // JsonLines consumes nonempty metadata. Hooks own only these copied values,
  // never the serializer-owned object, and still run after the trace flush.
  long long discard_value = 0, flush_value = 0;
  const bool discard =
      extract_metadata_integer(metadata, "discard", &discard_value, false) &&
      discard_value != 0;
  const bool flush =
      extract_metadata_integer(metadata, "flush", &flush_value, false) &&
      flush_value != 0;
  bool enable_tracing = true;
  if (this->config->aggregation_enable && strcmp(category, "dftracer") != 0) {
    enable_tracing = false;
    auto aggregated_key =
        AggregatedKey{category, event_name, start_time,     duration,
                      tid,      metadata,   get_app_name(), &rank};
    if (this->config->aggregation_type ==
        AggregationType::AGGREGATION_TYPE_SELECTIVE) {
      enable_tracing = !this->aggregator->should_aggregate(&aggregated_key);
    }
    if (!enable_tracing) {
      bool needs_writing = this->aggregator->aggregate(aggregated_key);
      if (needs_writing) {
        auto data = dftracer::AggregatedDataType();
        this->aggregator->get_previous_aggregations(data);
        size = this->serializer->aggregated(buffer + buffer_pos, index,
                                            process_id, data);
      }
    }
  }
  auto t_ser0 = std::chrono::steady_clock::now();
  if (enable_tracing) {
    size =
        this->serializer->data(buffer + buffer_pos, index, event_name, category,
                               start_time, duration, metadata, process_id, tid);
  }
  auto t_ser1 = std::chrono::steady_clock::now();
  g_serialize_ns.fetch_add(
      std::chrono::duration_cast<std::chrono::nanoseconds>(t_ser1 - t_ser0).count(),
      std::memory_order_relaxed);
  bool is_trigger = this->control_hooks_enabled &&
                     this->writer->is_control_trigger(event_name);
  compress_and_write_if_needed(size, is_trigger);
  if (is_trigger) {
    auto t_aw0 = std::chrono::steady_clock::now();
    this->writer->after_write(dftracer::EventContext{
        event_name, category, "X", process_id, tid, start_time, duration,
        has_step, step, has_epoch, epoch, discard, flush});
    auto t_aw1 = std::chrono::steady_clock::now();
    uint64_t aw_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t_aw1 - t_aw0).count();
    // trigger_ns tracks after_write time only (not the forced flush before it)
    g_trigger_ns.fetch_add(aw_ns, std::memory_order_relaxed);
    g_trigger_count.fetch_add(1, std::memory_order_relaxed);
  }
  auto t_exit = std::chrono::steady_clock::now();
  uint64_t total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t_exit - t_entry).count();
  g_log_event_ns.fetch_add(total_ns, std::memory_order_relaxed);
  uint64_t count = g_log_event_count.fetch_add(1, std::memory_order_relaxed) + 1;
  if (count % REPORT_INTERVAL == 0) {
    uint64_t ev_ns = g_log_event_ns.load(std::memory_order_relaxed);
    uint64_t lk_ns = g_lock_wait_ns.load(std::memory_order_relaxed);
    uint64_t ser_ns = g_serialize_ns.load(std::memory_order_relaxed);
    uint64_t fl_ns = g_flush_ns.load(std::memory_order_relaxed);
    uint64_t fl_cnt = g_flush_count.load(std::memory_order_relaxed);
    uint64_t tr_ns = g_trigger_ns.load(std::memory_order_relaxed);
    uint64_t tr_cnt = g_trigger_count.load(std::memory_order_relaxed);
    uint64_t ff_ns = g_forced_flush_ns.load(std::memory_order_relaxed);
    uint64_t ff_cnt = g_forced_flush_count.load(std::memory_order_relaxed);
    uint64_t ff_bytes = g_forced_flush_bytes.load(std::memory_order_relaxed);
    uint64_t tf_ns = g_timed_flush_ns.load(std::memory_order_relaxed);
    uint64_t tf_cnt = g_timed_flush_count.load(std::memory_order_relaxed);
    uint64_t tf_bytes = g_timed_flush_bytes.load(std::memory_order_relaxed);
    fprintf(stderr,
        "[DFTRACER_PERF] pid=%d events=%lu "
        "total_avg_us=%.1f lock_wait_avg_us=%.1f serialize_avg_us=%.1f "
        "flush_avg_ms=%.2f flush_count=%lu "
        "forced_flush_avg_ms=%.2f forced_count=%lu forced_avg_bytes=%lu "
        "timed_flush_avg_ms=%.2f timed_count=%lu timed_avg_bytes=%lu "
        "trigger_avg_ms=%.1f trigger_count=%lu\n",
        (int)process_id, (unsigned long)count,
        (double)ev_ns / count / 1000.0,
        (double)lk_ns / count / 1000.0,
        (double)ser_ns / count / 1000.0,
        fl_cnt > 0 ? (double)fl_ns / fl_cnt / 1e6 : 0.0,
        (unsigned long)fl_cnt,
        ff_cnt > 0 ? (double)ff_ns / ff_cnt / 1e6 : 0.0,
        (unsigned long)ff_cnt,
        ff_cnt > 0 ? (unsigned long)(ff_bytes / ff_cnt) : 0UL,
        tf_cnt > 0 ? (double)tf_ns / tf_cnt / 1e6 : 0.0,
        (unsigned long)tf_cnt,
        tf_cnt > 0 ? (unsigned long)(tf_bytes / tf_cnt) : 0UL,
        tr_cnt > 0 ? (double)tr_ns / tr_cnt / 1e6 : 0.0,
        (unsigned long)tr_cnt);
  }
}

void BufferManager::log_counter_event(int index, ConstEventNameType name,
                                      ConstEventNameType category,
                                      TimeResolution start_time,
                                      ProcessID process_id, ThreadID thread_id,
                                      dftracer::Metadata* metadata) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_counter_event %d", index);
  long long step = 0;
  long long epoch = 0;
  const bool has_step = extract_metadata_integer(metadata, "step", &step);
  const bool has_epoch = extract_metadata_integer(metadata, "epoch", &epoch);
  // JsonLines consumes nonempty metadata. Hooks own only these copied values,
  // never the serializer-owned object, and still run after the trace flush.
  long long discard_value = 0, flush_value = 0;
  const bool discard =
      extract_metadata_integer(metadata, "discard", &discard_value, false) &&
      discard_value != 0;
  const bool flush =
      extract_metadata_integer(metadata, "flush", &flush_value, false) &&
      flush_value != 0;
  size_t size =
      this->serializer->counter(buffer + buffer_pos, index, name, category,
                                start_time, process_id, thread_id, metadata);
  bool is_trigger =
      this->control_hooks_enabled && this->writer->is_control_trigger(name);
  compress_and_write_if_needed(size, is_trigger);
  if (is_trigger) {
    this->writer->after_write(dftracer::EventContext{
        name, category, "C", process_id, thread_id, start_time, 0,
        has_step, step, has_epoch, epoch, discard, flush});
  }
}

void BufferManager::flush() {
  std::unique_lock<std::shared_mutex> lock(mtx);
  compress_and_write_if_needed(0, true);
}

void BufferManager::log_metadata_event(ConstEventNameType name,
                                       ConstEventNameType value,
                                       ConstEventNameType ph,
                                       ProcessID process_id, ThreadID tid,
                                       bool is_string) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_metadata_event %s", value);
  size_t size = this->serializer->metadata(buffer + buffer_pos, name, value, ph,
                                           process_id, tid, is_string);
  compress_and_write_if_needed(size);
}
}  // namespace dftracer
