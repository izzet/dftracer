#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/fork_state.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/common/utils.h>
#include <dftracer/core/writer/mofka_writer.h>
#include <sys/prctl.h>

#include <chrono>
#include <cstring>
#include <diaspora/BatchParams.hpp>
#include <diaspora/DataView.hpp>
#include <diaspora/Metadata.hpp>
#include <diaspora/Ordering.hpp>
#include <diaspora/ThreadPool.hpp>
#include <mofka/MofkaDriver.hpp>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace dftracer {
namespace {

bool parse_ordering(const char* value, diaspora::Ordering* ordering) {
  if (!value || !ordering) return false;
  if (std::strcmp(value, "strict") == 0 || std::strcmp(value, "STRICT") == 0) {
    *ordering = diaspora::Ordering::Strict;
    return true;
  }
  if (std::strcmp(value, "loose") == 0 || std::strcmp(value, "LOOSE") == 0) {
    *ordering = diaspora::Ordering::Loose;
    return true;
  }
  return false;
}

void parse_control_event_names(const char* value,
                               std::vector<std::string>* names) {
  if (!names) return;
  names->clear();
  if (!value) return;
  std::stringstream ss(value);
  std::string token;
  while (std::getline(ss, token, ',')) {
    auto trimmed = trim_copy(token);
    if (!trimmed.empty()) names->push_back(std::move(trimmed));
  }
}

bool is_control_trigger_event(
    ConstEventNameType event_name,
    const std::vector<std::string>& control_trigger_event_names) {
  if (!event_name || event_name[0] == '\0') return false;
  for (const auto& trigger_name : control_trigger_event_names) {
    if (std::strcmp(event_name, trigger_name.c_str()) == 0) return true;
  }
  return false;
}

void ensure_topic_exists(diaspora::Driver* driver,
                         const std::string& topic_name) {
  if (!driver) return;
  if (driver->topicExists(topic_name)) return;
  diaspora::Validator validator;
  diaspora::Serializer serializer;
  diaspora::PartitionSelector selector;
  driver->createTopic(topic_name, diaspora::Metadata{}, validator, selector,
                      serializer);
  driver->as<mofka::MofkaDriver>().addMemoryPartition(topic_name, 0);
}

}  // namespace

template <>
std::shared_ptr<MofkaWriter> Singleton<MofkaWriter>::instance = nullptr;
template <>
bool Singleton<MofkaWriter>::stop_creating_instances = false;

MofkaWriter::MofkaWriter() {}

MofkaWriter::~MofkaWriter() {
  dftracer::emit_fork_breadcrumb("mofka_writer.destructor.enter");
  if (forked_child_ || dftracer::g_forked_child_process) {
    (void)control_producer_.release();
    (void)producer_.release();
    (void)control_topic_.release();
    (void)topic_.release();
    (void)driver_.release();
    dftracer::emit_fork_breadcrumb("mofka_writer.destructor.release_forked_child");
    return;
  }
  finalize(0);
  dftracer::emit_fork_breadcrumb("mofka_writer.destructor.exit");
}

void MofkaWriter::initialize(const char* filename) {
  const char* group_file_env = std::getenv("DFTRACER_MOFKA_GROUP_FILE");
  if (!group_file_env) {
    DFTRACER_LOG_ERROR("DFTRACER_MOFKA_GROUP_FILE not set", "");
    throw std::runtime_error("DFTRACER_MOFKA_GROUP_FILE not set");
  }
  group_file_ = group_file_env;
  const char* topic_name_env = std::getenv("DFTRACER_MOFKA_TOPIC_NAME");
  topic_name_ = topic_name_env ? topic_name_env : "dftracer_events";
  auto current_pid = getpid();

  // Allow Mofka/Mercury to access this process's memory for shared memory
  // transport
  // prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);

  if (driver_) {
    if (init_pid_ == current_pid) {
      DFTRACER_LOG_INFO("MofkaWriter already initialized", "");
      return;
    }
    forked_child_ = true;
    DFTRACER_LOG_INFO(
        "Detected forked child; rebuilding MofkaWriter state (parent pid=%d, "
        "child pid=%d)",
        init_pid_, current_pid);
    (void)control_producer_.release();
    (void)producer_.release();
    (void)control_topic_.release();
    (void)topic_.release();
    (void)driver_.release();
  }

  trace_events_written_ = 0;
  control_hooks_enabled_ = false;
  control_trigger_event_names_.clear();
  control_topic_name_.clear();
  finalized_ = false;

  try {
    diaspora::Metadata options;
    options.json()["group_file"] = group_file_;
    options.json()["margo"] = nlohmann::json::object();
    options.json()["margo"]["use_progress_thread"] = true;

    driver_ = std::make_unique<diaspora::Driver>(
        diaspora::Driver::New("mofka", options));
    DFTRACER_LOG_INFO("Mofka driver initialized", "");

    if (driver_->topicExists(topic_name_)) {
      DFTRACER_LOG_INFO("Mofka trace topic exists", "");
    } else {
      ensure_topic_exists(driver_.get(), topic_name_);
      DFTRACER_LOG_INFO("Mofka trace topic created", "");
    }

    topic_ = std::make_unique<diaspora::TopicHandle>(
        driver_->openTopic(topic_name_));
    DFTRACER_LOG_INFO("Mofka topic opened", "");

    // Buffer manager handles batching — each write() call from the buffer
    // manager contains a batch of newline-delimited JSON events. Use
    // batch_size=1 so each push is sent immediately (no Mofka-level batching).
    diaspora::BatchSize batchSize{1};
    const char* batch_size_env_name = "DFTRACER_MOFKA_PRODUCER_BATCH_SIZE";
    const char* batch_size_env = std::getenv(batch_size_env_name);
    size_t parsed_batch_size = 0;
    if (batch_size_env) {
      if (parse_positive_size_t(batch_size_env, &parsed_batch_size)) {
        batchSize = diaspora::BatchSize{parsed_batch_size};
        DFTRACER_LOG_INFO("Mofka producer batch size set from %s=%zu",
                          batch_size_env_name, parsed_batch_size);
      } else {
        DFTRACER_LOG_WARN("Invalid %s value '%s'; using batch_size=1",
                          batch_size_env_name, batch_size_env);
      }
    }

    diaspora::MaxNumBatches max_num_batches = diaspora::MaxNumBatches{16};
    const char* max_num_batches_env_name =
        "DFTRACER_MOFKA_PRODUCER_MAX_NUM_BATCHES";
    const char* max_num_batches_env = std::getenv(max_num_batches_env_name);
    size_t parsed_max_num_batches = 0;
    if (max_num_batches_env) {
      if (parse_positive_size_t(max_num_batches_env,
                                &parsed_max_num_batches)) {
        max_num_batches = diaspora::MaxNumBatches{
            static_cast<size_t>(parsed_max_num_batches)};
        DFTRACER_LOG_INFO("Mofka producer max num batches set from %s=%zu",
                          max_num_batches_env_name, parsed_max_num_batches);
      } else {
        DFTRACER_LOG_WARN("Invalid %s value '%s'; using default of 16",
                          max_num_batches_env_name, max_num_batches_env);
      }
    }

    diaspora::Ordering ordering = diaspora::Ordering::Strict;
    const char* ordering_env_name = "DFTRACER_MOFKA_PRODUCER_ORDERING";
    const char* ordering_env = std::getenv(ordering_env_name);
    if (ordering_env) {
      if (parse_ordering(ordering_env, &ordering)) {
        DFTRACER_LOG_INFO("Mofka producer ordering set from %s=%s",
                          ordering_env_name, ordering_env);
      } else {
        DFTRACER_LOG_WARN("Invalid %s value '%s'; using strict ordering",
                          ordering_env_name, ordering_env);
      }
    }

    const char* producer_thread_count_env_name =
        "DFTRACER_MOFKA_PRODUCER_THREAD_COUNT";
    const char* producer_thread_count_env =
        std::getenv(producer_thread_count_env_name);
    size_t producer_thread_count = 0;
    diaspora::ThreadPool producer_thread_pool{};
    if (producer_thread_count_env) {
      if (parse_positive_size_t(producer_thread_count_env,
                                &producer_thread_count)) {
        producer_thread_pool = driver_->makeThreadPool(
            diaspora::ThreadCount{producer_thread_count});
        DFTRACER_LOG_INFO("Mofka producer thread count set from %s=%zu",
                          producer_thread_count_env_name,
                          producer_thread_count);
      } else {
        DFTRACER_LOG_WARN("Invalid %s value '%s'; using default thread pool",
                          producer_thread_count_env_name,
                          producer_thread_count_env);
      }
    }

    const char* control_topic_env_name = "DFTRACER_MOFKA_CONTROL_TOPIC_NAME";
    const char* control_topic_env = std::getenv(control_topic_env_name);
    if (control_topic_env && control_topic_env[0] != '\0') {
      control_topic_name_ = control_topic_env;
    } else {
      control_topic_name_ = "control_events";
      DFTRACER_LOG_INFO("%s unset; defaulting to %s", control_topic_env_name,
                        control_topic_name_.c_str());
    }

    const char* control_event_names_env_name =
        "DFTRACER_MOFKA_CONTROL_EVENT_NAMES";
    const char* control_event_names_env =
        std::getenv(control_event_names_env_name);
    if (control_event_names_env) {
      parse_control_event_names(control_event_names_env,
                                &control_trigger_event_names_);
      if (control_trigger_event_names_.empty()) {
        DFTRACER_LOG_WARN(
            "%s is set but empty; control-event producer disabled",
            control_event_names_env_name);
      } else {
        DFTRACER_LOG_INFO("Mofka control trigger names loaded from %s",
                          control_event_names_env_name);
      }
    } else {
      // Default trigger names to avoid requiring extra env setup.
      control_trigger_event_names_.push_back("epoch.start");
      control_trigger_event_names_.push_back("epoch.block");
      DFTRACER_LOG_INFO(
          "%s unset; defaulting control-event triggers to "
          "epoch.start,epoch.block",
          control_event_names_env_name);
    }

    if (producer_thread_pool) {
      producer_ = std::make_unique<diaspora::Producer>(
          topic_->producer("dftracer", batchSize, max_num_batches, ordering,
                           producer_thread_pool));
    } else {
      producer_ = std::make_unique<diaspora::Producer>(
          topic_->producer("dftracer", batchSize, max_num_batches, ordering));
    }
    DFTRACER_LOG_INFO("Mofka producer created", "");

    if (!control_trigger_event_names_.empty()) {
      if (driver_->topicExists(control_topic_name_)) {
        DFTRACER_LOG_INFO("Mofka control topic exists", "");
      } else {
        ensure_topic_exists(driver_.get(), control_topic_name_);
        DFTRACER_LOG_INFO("Mofka control topic created", "");
      }
      control_topic_ = std::make_unique<diaspora::TopicHandle>(
          driver_->openTopic(control_topic_name_));

      const diaspora::BatchSize control_batch_size{1};
      const diaspora::Ordering control_ordering = diaspora::Ordering::Strict;
      if (producer_thread_pool) {
        control_producer_ =
            std::make_unique<diaspora::Producer>(control_topic_->producer(
                "dftracer_control", control_batch_size, max_num_batches,
                control_ordering, producer_thread_pool));
      } else {
        control_producer_ = std::make_unique<diaspora::Producer>(
            control_topic_->producer("dftracer_control", control_batch_size,
                                     max_num_batches, control_ordering));
      }
      DFTRACER_LOG_INFO(
          "Mofka control producer enabled: topic=%s trigger_count=%zu",
          control_topic_name_.c_str(), control_trigger_event_names_.size());
      control_hooks_enabled_ = true;
    }

    init_pid_ = current_pid;
    DFTRACER_LOG_INFO("MofkaWriter initialized with PID %d", init_pid_);
  } catch (const std::exception& e) {
    DFTRACER_LOG_ERROR("Failed to initialize MofkaWriter", e.what());
    throw;
  }
}

bool MofkaWriter::is_control_trigger(ConstEventNameType event_name) const {
  return is_control_trigger_event(event_name, control_trigger_event_names_);
}

void MofkaWriter::after_write(const EventContext& event) {
  if (!control_producer_) return;
  try {
    // Buffer manager already force-flushed via write() before calling us.
    // Flush the Mofka producer to ensure all trace data is delivered before
    // the control boundary event.
    static constexpr int FLUSH_TIMEOUT_MS = 100;
    auto t0 = std::chrono::steady_clock::now();
    if (producer_) {
      producer_->flush().wait(FLUSH_TIMEOUT_MS);
    }
    auto t1 = std::chrono::steady_clock::now();

    const auto trace_ts_us =
        static_cast<unsigned long long>(event.start_time);
    diaspora::Metadata control_metadata;
    control_metadata.json()["type"] = "boundary_event";
    control_metadata.json()["trace_topic"] = topic_name_;
    control_metadata.json()["control_topic"] = control_topic_name_;
    control_metadata.json()["events_written"] = trace_events_written_;
    control_metadata.json()["trigger_event_name"] = event.event_name;
    control_metadata.json()["trigger_category"] =
        event.category ? event.category : "";
    control_metadata.json()["trigger_phase"] = event.phase ? event.phase : "";
    control_metadata.json()["pid"] = event.process_id;
    control_metadata.json()["tid"] =
        static_cast<unsigned long long>(event.thread_id);
    control_metadata.json()["trace_ts_us"] = trace_ts_us;
    control_metadata.json()["ts_unix_ns"] = trace_ts_us * 1000ULL;
    if (event.has_step) control_metadata.json()["step"] = event.step;
    if (event.has_epoch) control_metadata.json()["epoch"] = event.epoch;
    auto t2 = std::chrono::steady_clock::now();
    control_producer_->push(std::move(control_metadata), diaspora::DataView{});
    auto t3 = std::chrono::steady_clock::now();
    control_producer_->flush().wait(FLUSH_TIMEOUT_MS);
    auto t4 = std::chrono::steady_clock::now();

    fprintf(stderr,
        "[DFTRACER_PERF_AFTER_WRITE] pid=%d event=%s "
        "data_flush_ms=%.1f ctrl_build_ms=%.1f ctrl_push_ms=%.1f ctrl_flush_ms=%.1f total_ms=%.1f\n",
        (int)event.process_id,
        event.event_name ? event.event_name : "?",
        std::chrono::duration<double, std::milli>(t1 - t0).count(),
        std::chrono::duration<double, std::milli>(t2 - t1).count(),
        std::chrono::duration<double, std::milli>(t3 - t2).count(),
        std::chrono::duration<double, std::milli>(t4 - t3).count(),
        std::chrono::duration<double, std::milli>(t4 - t0).count());
  } catch (const std::exception& e) {
    DFTRACER_LOG_WARN("Mofka control event push failed: %s", e.what());
  }
}

size_t MofkaWriter::write(const char* data, size_t len, bool force) {
  if (!topic_) {
    DFTRACER_LOG_ERROR("Mofka topic not initialized", "");
    return 0;
  }
  try {
    // Push batched newline-delimited JSON via DataView. Each write() call from
    // buffer_manager contains a batch of events accumulated up to
    // write_buffer_size. Metadata is minimal — the actual event data is in the
    // DataView payload.
    diaspora::Metadata batch_metadata;
    batch_metadata.json()["type"] = "batch";
    batch_metadata.json()["size"] = len;
    diaspora::DataView data_view(const_cast<char*>(data), len);
    producer_->push(std::move(batch_metadata), data_view);
    // Count individual events in the batch (each ends with '\n').
    for (size_t i = 0; i < len; ++i) {
      if (data[i] == '\n') ++trace_events_written_;
    }
    return len;
  } catch (const std::exception& e) {
    DFTRACER_LOG_ERROR("Mofka write failed", e.what());
    return 0;
  }
}

void MofkaWriter::finalize(int index) {
  dftracer::emit_fork_breadcrumb("mofka_writer.finalize.enter");
  if (finalized_) {
    DFTRACER_LOG_INFO("MofkaWriter already finalized", "");
    dftracer::emit_fork_breadcrumb("mofka_writer.finalize.already_finalized");
    return;
  }
  finalized_ = true;
  DFTRACER_LOG_INFO("Mofka finalizing", "");
  if (getpid() != init_pid_) {
    DFTRACER_LOG_INFO(
        "MofkaWriter::finalize called in child process (Init PID: %d, Current "
        "PID: %d). Skipping flush to avoid hang.",
        init_pid_, getpid());
    // In a child process, do not flush or destruct normally.
    // Just release ownership to avoid destructor calls that might wait on
    // futures.
    if (producer_) {
      producer_.release();
    }
    if (control_producer_) {
      control_producer_.release();
    }
    if (topic_) {
      topic_.release();
    }
    if (control_topic_) {
      control_topic_.release();
    }
    if (driver_) {
      driver_.release();
    }
    dftracer::emit_fork_breadcrumb("mofka_writer.finalize.skip_wrong_pid");
    return;
  }
  if (forked_child_) {
    DFTRACER_LOG_INFO(
        "MofkaWriter::finalize in forked child pid=%d: releasing handles "
        "without flushing or destruction",
        getpid());
    if (control_producer_) {
      (void)control_producer_.release();
    }
    if (producer_) {
      (void)producer_.release();
    }
    if (control_topic_) {
      (void)control_topic_.release();
    }
    if (topic_) {
      (void)topic_.release();
    }
    if (driver_) {
      (void)driver_.release();
    }
    dftracer::emit_fork_breadcrumb("mofka_writer.finalize.release_forked_child");
    return;
  }
  if (control_producer_) {
    control_producer_->flush().wait(-1);
    DFTRACER_LOG_INFO("Mofka control producer flushed", "");
    control_producer_.reset();
    DFTRACER_LOG_INFO("Mofka control producer reset", "");
  }
  if (producer_) {
    producer_->flush().wait(-1);
    DFTRACER_LOG_INFO("Mofka producer flushed", "");
    producer_.reset();
    DFTRACER_LOG_INFO("Mofka producer reset", "");
  }
  if (control_topic_) {
    control_topic_.reset();
    DFTRACER_LOG_INFO("Mofka control topic reset", "");
  }
  if (topic_) {
    topic_.reset();
    DFTRACER_LOG_INFO("Mofka topic reset", "");
  }
  if (driver_) {
    driver_.reset();
    DFTRACER_LOG_INFO("Mofka driver reset", "");
  }
  dftracer::emit_fork_breadcrumb("mofka_writer.finalize.exit");
}

}  // namespace dftracer
