// Real serializer/control regression: JsonLines consumes Metadata before after_write.
#include <dftracer/core/buffer/buffer.h>
#include <liveflow/shm_ring.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <any>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// Read the documented public SHM layout, also consumed by python/shm_reader.py.
// The test owns only its unique rings and always unlinks them on exit.
class OwnedRing {
 public:
  explicit OwnedRing(std::string name) : name_(std::move(name)) {
    liveflow::ShmRing creator;
    require(creator.create(name_.c_str(), 16, 65536), "create test ring");
    const int fd = shm_open(name_.c_str(), O_RDWR, 0600);
    if (fd < 0) { shm_unlink(name_.c_str()); throw std::runtime_error("open test ring"); }
    struct stat st {};
    const bool stat_ok = fstat(fd, &st) == 0;
    bytes_ = stat_ok ? static_cast<size_t>(st.st_size) : 0;
    base_ = stat_ok ? mmap(nullptr, bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    close(fd);
    if (base_ == MAP_FAILED) {
      shm_unlink(name_.c_str());
      throw std::runtime_error("map test ring");
    }
  }
  ~OwnedRing() { munmap(base_, bytes_); shm_unlink(name_.c_str()); }
  OwnedRing(const OwnedRing&) = delete;
  OwnedRing& operator=(const OwnedRing&) = delete;

  std::vector<std::string> drain() {
    auto* h = static_cast<liveflow::SegmentHeader*>(base_);
    require(h->unclaimed_dropped.load() == 0, "unclaimed SHM drop");
    std::vector<std::string> records;
    for (uint32_t i = 0; i < h->lane_count; ++i) {
      auto* lane = reinterpret_cast<liveflow::Lane*>(static_cast<char*>(base_) + sizeof(*h)) + i;
      require(lane->dropped.load() == 0, "SHM lane drop");
      auto tail = lane->tail.load(std::memory_order_relaxed);
      const auto head = lane->head.load(std::memory_order_acquire);
      const char* data = static_cast<char*>(base_) + liveflow::lane_data_offset(h->lane_count) + i*h->lane_bytes;
      auto copy = [&](uint64_t pos, void* dest, size_t n) {
        const size_t offset = pos & (h->lane_bytes-1);
        const size_t first = std::min(n, static_cast<size_t>(h->lane_bytes-offset));
        std::memcpy(dest, data+offset, first);
        if (first<n) std::memcpy(static_cast<char*>(dest)+first, data, n-first);
      };
      while (tail < head) {
        liveflow::RecordHeader rh {};
        require(head-tail >= sizeof(rh), "complete record header");
        copy(tail, &rh, sizeof(rh));
        require(rh.len <= h->lane_bytes/2 && head-tail >= sizeof(rh)+rh.len, "complete record payload");
        std::string record(rh.len, '\0');
        copy(tail+sizeof(rh), &record[0], rh.len);
        records.push_back(std::move(record));
        tail += sizeof(rh)+rh.len;
      }
      lane->tail.store(tail, std::memory_order_release);
    }
    return records;
  }
 private:
  std::string name_;
  void* base_ = MAP_FAILED;
  size_t bytes_ = 0;
};

struct Frame { YAML::Node metadata; std::string data; };
Frame unpack(const std::string& record) {
  uint32_t len = 0;
  require(record.size() >= sizeof(len), "framing header");
  std::memcpy(&len, record.data(), sizeof(len));
  require(len <= record.size()-sizeof(len), "framing metadata length");
  return {YAML::Load(record.substr(sizeof(len),len)), record.substr(sizeof(len)+len)};
}
struct FlagCase { const char* label; std::any value; bool present; bool expected; };
}

int main() {
  try {
    const std::string prefix = "dftracer.control-unit."+std::to_string(getpid());
    OwnedRing trace("/"+prefix+".trace"), control("/"+prefix+".control");
    setenv("DFTRACER_SHM_PREFIX",prefix.c_str(),1);
    setenv("DFTRACER_SHM_CONTROL_EVENT_NAMES","window.stop",1);
    setenv("DFTRACER_ENABLE","1",1);
    setenv("DFTRACER_INC_METADATA","1",1);
    setenv("DFTRACER_ENABLE_AGGREGATION","OFF",1);
    setenv("DFTRACER_TRACE_COMPRESSION","0",1);
    setenv("DFTRACER_WRITE_BUFFER_SIZE","4096",1);
    const std::vector<FlagCase> cases = {
      {"absent",0,false,false}, {"zero",0,true,false},
      {"int",-1,true,true}, {"long",2L,true,true},
      {"longlong",3LL,true,true}, {"uint",4U,true,true},
      {"ulong",5UL,true,true}, {"ulonglong",6ULL,true,true},
      {"string",std::string("1"),true,false}, {"float",1.0,true,false},
      {"bool",true,true,false}
    };
    dftracer::BufferManager buffer;
    char hostname[] = "host";
    buffer.initialize("unit",hostname);
    int index = 0;
    for (const auto& c : cases) {
      for (int phase = 0; phase < 2; ++phase, ++index) {
        auto* metadata = new dftracer::Metadata();
        metadata->insert_or_assign("step",std::string(std::to_string(900+index)));
        metadata->insert_or_assign("epoch",3L);
        metadata->insert_or_assign("proof",std::string(c.label));
        if (c.present) {
          metadata->insert_or_assign("discard",c.value);
          metadata->insert_or_assign("flush",c.value);
        }
        if (phase==0) buffer.log_data_event(index,"window.stop","pipeline",1000+index,10,metadata,getpid(),getpid());
        else buffer.log_counter_event(index,"window.stop","pipeline",1000+index,getpid(),getpid(),metadata);
        // Each trigger forces the trace out before publishing its boundary.
        const auto boundaries = control.drain(), traces = trace.drain();
        require(boundaries.size()==1 && traces.size()==1,"one trace and boundary per event");
        const auto b = unpack(boundaries[0]);
        const auto t = unpack(traces[0]);
        require(b.data.empty(),"boundary carries metadata only");
        require(b.metadata["step"].as<int>()==900+index && b.metadata["epoch"].as<int>()==3,"saved step/epoch");
        require(b.metadata["trigger_phase"].as<std::string>()==(phase==0?"X":"C"),"saved event phase");
        for (const char* flag : {"discard","flush"}) {
          require(static_cast<bool>(b.metadata[flag])==c.expected,"numeric-only flag eligibility");
          if (c.expected) require(b.metadata[flag].as<int>()==1,"normalized nonzero flag");
        }
        const auto event = YAML::Load(t.data);
        require(event["args"]["step"].as<int>()==900+index,"trace retains step");
        require(event["args"]["epoch"].as<int>()==3,"trace retains epoch");
        require(event["args"]["proof"].as<std::string>()==c.label,"trace metadata serialized");
        for (const char* flag : {"discard","flush"}) {
          const bool serializable = c.present && std::strcmp(c.label,"bool")!=0 && std::strcmp(c.label,"longlong")!=0;
          require(static_cast<bool>(event["args"][flag])==serializable,"trace preserves supported flag presence");
          if (serializable && c.expected) require(event["args"][flag].as<long long>()!=0,"trace preserves nonzero flag");
          if (c.present && std::strcmp(c.label,"string")==0) require(event["args"][flag].as<std::string>()=="1","trace preserves string flag");
        }
      }
    }
    buffer.finalize(index,getpid());
    require(control.drain().empty(),"no spurious final boundary");
    std::cout << "PASS " << index << " serialized X/C events, saved control fields, zero drops\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
