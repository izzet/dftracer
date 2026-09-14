#include <dftracer/core/writer/shm_ring_writer.h>

#include <dftracer/core/common/logging.h>
#include <dftracer/core/common/singleton.h>
#include <dftracer/core/common/datastructure.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <unistd.h>

namespace dftracer {

namespace {

std::string trim_copy(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\n\r");
  if (b == std::string::npos) return "";
  const auto e = s.find_last_not_of(" \t\n\r");
  return s.substr(b, e - b + 1);
}

void parse_names(const char* value, std::vector<std::string>* names) {
  if (!names) return;
  names->clear();
  if (!value) return;
  std::stringstream ss(value);
  std::string token;
  while (std::getline(ss, token, ',')) {
    auto t = trim_copy(token);
    if (!t.empty()) names->push_back(std::move(t));
  }
}

std::string env_or(const char* name, const char* fallback) {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : std::string(fallback);
}

size_t env_size(const char* name, size_t fallback) {
  const char* v = std::getenv(name);
  if (!v || !*v) return fallback;
  char* end = nullptr;
  unsigned long long parsed = std::strtoull(v, &end, 10);
  if (end == v || parsed == 0) return fallback;
  return static_cast<size_t>(parsed);
}

// JSON escaping for the small control-metadata strings. Deliberately minimal: the only values
// that reach it are event names, categories and phases, which are identifiers in practice.
void append_escaped(std::string* out, const char* s) {
  if (!s) return;
  for (const char* p = s; *p; ++p) {
    switch (*p) {
      case '"':  out->append("\\\""); break;
      case '\\': out->append("\\\\"); break;
      case '\n': out->append("\\n");  break;
      case '\t': out->append("\\t");  break;
      default:   out->push_back(*p);  break;
    }
  }
}

bool metadata_integer(const Metadata* metadata, const char* key, long long* value) {
  if (!metadata || !key || !value) return false;
  const auto it = metadata->find(key);
  if (it == metadata->end()) return false;
  const std::any& raw = it->second.second;
  if (raw.type() == typeid(int)) { *value = std::any_cast<int>(raw); return true; }
  if (raw.type() == typeid(long)) { *value = std::any_cast<long>(raw); return true; }
  if (raw.type() == typeid(long long)) { *value = std::any_cast<long long>(raw); return true; }
  if (raw.type() == typeid(unsigned int)) { *value = std::any_cast<unsigned int>(raw); return true; }
  if (raw.type() == typeid(unsigned long)) { *value = std::any_cast<unsigned long>(raw); return true; }
  if (raw.type() == typeid(unsigned long long)) { *value = std::any_cast<unsigned long long>(raw); return true; }
  return false;
}

}  // namespace

// Explicit singleton statics, exactly as every other writer does (mofka_writer.cpp:74-77).
// BufferManager reaches this writer through Singleton<ShmRingWriter>::get_instance().
template <>
std::shared_ptr<ShmRingWriter> Singleton<ShmRingWriter>::instance = nullptr;
template <>
bool Singleton<ShmRingWriter>::stop_creating_instances = false;

ShmRingWriter::ShmRingWriter() = default;

ShmRingWriter::~ShmRingWriter() = default;

void ShmRingWriter::initialize(const char* filename) {
  (void)filename;  // the ring is addressed by segment name, not by path

  // NOTE ON FORK: there is deliberately no pid check and no re-initialisation path here.
  // A child inherits the mmap, so the mapping it wakes up with is already valid. ShmRing::write
  // notices the new pid and claims a fresh lane on the first write, which keeps each lane
  // single-producer without any work at fork time.
  const std::string prefix = env_or("DFTRACER_SHM_PREFIX", "liveflow");
  trace_seg_ = "/" + prefix + "." + env_or("DFTRACER_SHM_TRACE_TOPIC", "trace");
  control_seg_ = "/" + prefix + "." + env_or("DFTRACER_SHM_CONTROL_TOPIC", "control");

  const size_t lanes = env_size("DFTRACER_SHM_LANES", 40);
  // Strictly MORE than 2x (write_buffer_size + slack). BufferManager allocates
  // `write_buffer_size + 16*1024` and can flush all of it, so the 16 MiB default yields records
  // just OVER 16 MiB; a 32 MiB lane refuses them at the lane_bytes/2 guard by a hair. Measured
  // on a live DLIO run: 3 drops, average size exactly 16.00 MiB. 64 MiB is the safe default.
  const size_t trace_lane = env_size("DFTRACER_SHM_LANE_BYTES", 64ull * 1024 * 1024);
  const size_t ctl_lane = env_size("DFTRACER_SHM_SMALL_LANE_BYTES", 64ull * 1024);

  // Attach if the agent already created the segment, otherwise create it. Either order works,
  // which matters because the application and the node agent start independently.
  if (!trace_ring_.attach(trace_seg_.c_str())) {
    if (!trace_ring_.create(trace_seg_.c_str(), static_cast<uint32_t>(lanes), trace_lane)) {
      DFTRACER_LOG_ERROR("ShmRingWriter: cannot open trace segment %s", trace_seg_.c_str());
      return;
    }
  }

  const char* names_env = std::getenv("DFTRACER_SHM_CONTROL_EVENT_NAMES");
  if (names_env) {
    parse_names(names_env, &control_trigger_event_names_);
  } else {
    // Same defaults MofkaWriter uses, so switching transports does not silently change which
    // events close a control window.
    control_trigger_event_names_.push_back("epoch.start");
    control_trigger_event_names_.push_back("epoch.block");
  }

  if (!control_trigger_event_names_.empty()) {
    if (!control_ring_.attach(control_seg_.c_str())) {
      if (!control_ring_.create(control_seg_.c_str(), static_cast<uint32_t>(lanes), ctl_lane)) {
        DFTRACER_LOG_WARN("ShmRingWriter: cannot open control segment %s; control hooks off",
                          control_seg_.c_str());
        return;
      }
    }
    control_hooks_enabled_ = true;
  }

  DFTRACER_LOG_INFO("ShmRingWriter ready: trace=%s control=%s lanes=%zu",
                    trace_seg_.c_str(), control_seg_.c_str(), lanes);
}

bool ShmRingWriter::is_control_trigger(ConstEventNameType event_name) const {
  if (!event_name || event_name[0] == '\0') return false;
  for (const auto& n : control_trigger_event_names_) {
    if (std::strcmp(event_name, n.c_str()) == 0) return true;
  }
  return false;
}

// Framing: [u32 meta_len][meta_json][payload]. Must match liveflow_transport/shm_io.py's
// pack()/unpack(), which is the reader on the other side.
bool ShmRingWriter::push(liveflow::ShmRing& ring, const std::string& metadata_json,
                         const char* data, size_t len) {
  if (!ring.valid()) return false;
  const uint32_t meta_len = static_cast<uint32_t>(metadata_json.size());
  scratch_.clear();
  scratch_.reserve(sizeof(uint32_t) + meta_len + len);
  scratch_.append(reinterpret_cast<const char*>(&meta_len), sizeof(meta_len));
  scratch_.append(metadata_json);
  if (data && len) scratch_.append(data, len);
  return ring.write(scratch_.data(), scratch_.size());
}

size_t ShmRingWriter::write(const char* data, size_t len, bool force) {
  (void)force;
  if (finalized_ || !data || len == 0) return 0;

  // WAIT-FREE. This is the whole point (LIVEFLOW-ARCHITECTURE.md sec 4): the caller here is a
  // training thread or a DataLoader worker thread, and it must never be possible for the tracing
  // plane to block it. A full ring is a COUNTED drop, visible to the agent through the lane's
  // `dropped` counter, never a wait.
  const bool ok = push(trace_ring_, "{\"type\":\"batch\"}", data, len);
  if (ok) {
    // COUNT EVENTS, NOT RECORDS. `events_written` is published on the control boundary and the
    // analyzer uses it as a per-pid DRAIN TARGET: it pulls trace records until it has seen that
    // many EVENTS, then closes the window (analyzer.py target_counts).
    //
    // Counting write() calls instead -- one per buffer flush -- makes the target absurdly small,
    // so the analyzer stops after a handful of events and reports a window of 1-3 events out of
    // several thousand. That is exactly what the first live run did. MofkaWriter counts newlines
    // (mofka_writer.cpp:473) and this must match it.
    for (size_t i = 0; i < len; ++i) {
      if (data[i] == '\n') ++trace_events_written_;
    }
  }
  return ok ? len : 0;
}

void ShmRingWriter::after_write(const EventContext& event) {
  if (!control_hooks_enabled_ || !control_ring_.valid()) return;

  // No flush-and-wait before the boundary, unlike MofkaWriter::after_write which calls
  // producer_->flush().wait(100ms) to make sure the data producer has drained first. A ring needs
  // no such barrier: write() published the payload with a release store before returning, so any
  // trace record for this window is already visible to the reader by the time the boundary lands.
  // That removes a 100 ms worst-case stall from the application's control path.
  const auto trace_ts_us = static_cast<unsigned long long>(event.start_time);

  std::string meta;
  meta.reserve(320);
  meta.append("{\"type\":\"boundary_event\"");
  meta.append(",\"trace_topic\":\"").append(trace_seg_).append("\"");
  meta.append(",\"control_topic\":\"").append(control_seg_).append("\"");
  meta.append(",\"events_written\":").append(std::to_string(trace_events_written_));
  meta.append(",\"trigger_event_name\":\"");
  append_escaped(&meta, event.event_name);
  meta.append("\"");
  meta.append(",\"trigger_category\":\"");
  append_escaped(&meta, event.category ? event.category : "");
  meta.append("\"");
  meta.append(",\"trigger_phase\":\"");
  append_escaped(&meta, event.phase ? event.phase : "");
  meta.append("\"");
  meta.append(",\"pid\":").append(std::to_string(static_cast<long long>(event.process_id)));
  meta.append(",\"tid\":").append(std::to_string(static_cast<unsigned long long>(event.thread_id)));
  meta.append(",\"trace_ts_us\":").append(std::to_string(trace_ts_us));
  meta.append(",\"ts_unix_ns\":").append(std::to_string(trace_ts_us * 1000ULL));
  // These flags describe boundary eligibility, not a diagnosis.  The analyzer must consume an
  // explicitly discarded boundary to preserve rank/window ordering, while excluding it from
  // control evidence.  Keep the control plane's contract minimal and authoritative.
  long long boundary_flag = 0;
  if (metadata_integer(event.metadata, "discard", &boundary_flag) && boundary_flag) {
    meta.append(",\"discard\":1");
  }
  if (metadata_integer(event.metadata, "flush", &boundary_flag) && boundary_flag) {
    meta.append(",\"flush\":1");
  }
  meta.append(",\"duration_us\":").append(std::to_string(
      static_cast<unsigned long long>(event.duration)));
  if (event.has_step) meta.append(",\"step\":").append(std::to_string(event.step));
  if (event.has_epoch) meta.append(",\"epoch\":").append(std::to_string(event.epoch));
  meta.append("}");

  push(control_ring_, meta, nullptr, 0);
}

void ShmRingWriter::finalize(int index) {
  (void)index;
  finalized_ = true;
  // Nothing to tear down. There is no connection to close, no thread pool to join, and no
  // outstanding future to wait on -- which is also why this writer cannot hang at exit, a failure
  // mode the Mofka path had to be defended against.
}

}  // namespace dftracer
