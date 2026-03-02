#include <dftracer/core/buffer/buffer.h>

#include <cstddef>
#include <dftracer/core/dftracer_config.hpp>

#if DFTRACER_WRITER_TYPE_MOFKA
#include <dftracer/core/writer/mofka_writer.h>
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

void BufferManager::compress_and_write_if_needed(size_t size, bool force) {
#if DFTRACER_WRITER_TYPE_MOFKA
  force = true;
#endif
  if (force || buffer_pos + size > this->config->write_buffer_size) {
    if (this->config->compression) {
      size = this->compressor->compress(buffer, buffer_pos + size);
    } else {
      size = buffer_pos + size;
    }
    if (size > 0) {
      size = this->writer->write(buffer, size, true);
    }
    buffer_pos = 0;
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
#if DFTRACER_WRITER_TYPE_MOFKA
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
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_data_event %d", index);
  if (this->control_hooks_enabled) {
    this->writer->before_write(
        dftracer::EventContext{event_name, category, "X", process_id, tid});
  }
  size_t size = 0;
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
  if (enable_tracing) {
    size =
        this->serializer->data(buffer + buffer_pos, index, event_name, category,
                               start_time, duration, metadata, process_id, tid);
  }
  compress_and_write_if_needed(size);
}

void BufferManager::log_counter_event(int index, ConstEventNameType name,
                                      ConstEventNameType category,
                                      TimeResolution start_time,
                                      ProcessID process_id, ThreadID thread_id,
                                      dftracer::Metadata* metadata) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_counter_event %d", index);
  if (this->control_hooks_enabled) {
    this->writer->before_write(
        dftracer::EventContext{name, category, "C", process_id, thread_id});
  }
  size_t size =
      this->serializer->counter(buffer + buffer_pos, index, name, category,
                                start_time, process_id, thread_id, metadata);
  compress_and_write_if_needed(size);
}

void BufferManager::log_metadata_event(ConstEventNameType name,
                                       ConstEventNameType value,
                                       ConstEventNameType ph,
                                       ProcessID process_id, ThreadID tid,
                                       bool is_string) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_DEBUG("BufferManager.log_metadata_event %s", value);
  if (this->control_hooks_enabled) {
    this->writer->before_write(
        dftracer::EventContext{ph, "dftracer", "M", process_id, tid});
  }
  size_t size = this->serializer->metadata(buffer + buffer_pos, name, value, ph,
                                           process_id, tid, is_string);
  compress_and_write_if_needed(size);
}
}  // namespace dftracer
