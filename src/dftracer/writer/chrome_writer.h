//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_CHROME_WRITER_H
#define DFTRACER_CHROME_WRITER_H

#include <dftracer/core/constants.h>
#include <dftracer/core/typedef.h>
#include <dftracer/utils/configuration_manager.h>
#include <dftracer/utils/posix_internal.h>
#include <dftracer/utils/utils.h>
#if DISABLE_HWLOC == 1
#include <hwloc.h>
#endif
#include <assert.h>
#include <unistd.h>

#include <any>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
namespace dftracer {
class ChromeWriter {
 private:
  std::unordered_map<char *, std::any> metadata;
  std::shared_mutex mtx;

 protected:
  bool throw_error;
  std::string filename;

 private:
  bool include_metadata, enable_compression;

  bool enable_core_affinity;
#if DISABLE_HWLOC == 1
  hwloc_topology_t topology;
#endif
  FILE *fh;
  char hostname[256];
  static const int MAX_LINE_SIZE = 4096;
  static const int MAX_META_LINE_SIZE = 3000;
  static const int MAX_BUFFER = 33554432;  // 32MB

  size_t current_index;
  char *buffer;
  void convert_json(int index, ConstEventType event_name,
                    ConstEventType category, TimeResolution start_time,
                    TimeResolution duration,
                    std::unordered_map<std::string, std::any> *metadata,
                    ProcessID process_id, ThreadID thread_id);

  bool is_first_write;
  inline size_t write_buffer_op(bool force = false) {
    if (!force && current_index > MAX_BUFFER) return 0;
    DFTRACER_LOG_DEBUG("ChromeWriter.write_buffer_op %s",
                       this->filename.c_str());
    size_t written_elements = 0;
    auto previous_index = current_index;
    {
      std::unique_lock<std::shared_mutex> lock(mtx);
      flockfile(fh);
      written_elements = fwrite(buffer, sizeof(char), current_index, fh);
      funlockfile(fh);
      current_index = 0;
    }

    if (written_elements != previous_index) {  // GCOVR_EXCL_START
      DFTRACER_LOG_ERROR(
          "unable to log write for a+ written only %ld of %ld with error code "
          "%d",
          written_elements, previous_index, errno);
    }  // GCOVR_EXCL_STOP
    return written_elements;
  }
  std::vector<unsigned> core_affinity() {
    DFTRACER_LOG_DEBUG("ChromeWriter.core_affinity", "");
    auto cores = std::vector<unsigned>();
#if DISABLE_HWLOC == 1
    if (enable_core_affinity) {
      hwloc_cpuset_t set = hwloc_bitmap_alloc();
      hwloc_get_cpubind(topology, set, HWLOC_CPUBIND_PROCESS);
      for (unsigned id = hwloc_bitmap_first(set); id != -1;
           id = hwloc_bitmap_next(set, id)) {
        cores.push_back(id);
      }
      hwloc_bitmap_free(set);
    }
#endif
    return cores;
  }

  void get_hostname(char *hostname) {
    DFTRACER_LOG_DEBUG("ChromeWriter.get_hostname", "");
    gethostname(hostname, 256);
  }

 public:
  ChromeWriter()
      : metadata(),
        throw_error(false),
        filename(),
        include_metadata(false),
        enable_compression(false),
        enable_core_affinity(false),
        fh(nullptr),
        current_index(0),
        buffer(nullptr),
        is_first_write(true) {
    DFTRACER_LOG_DEBUG("ChromeWriter.ChromeWriter", "");
    auto conf =
        dftracer::Singleton<dftracer::ConfigurationManager>::get_instance();
    get_hostname(hostname);
    include_metadata = conf->metadata;
    enable_core_affinity = conf->core_affinity;
    enable_compression = conf->compression;
    {
      std::unique_lock<std::shared_mutex> lock(mtx);
      if (!buffer) {
        buffer = (char *)malloc(MAX_BUFFER + 4096);
        current_index = 0;
      }
    }

    if (enable_core_affinity) {
#if DISABLE_HWLOC == 1
      hwloc_topology_init(&topology);  // initialization
      hwloc_topology_load(topology);   // actual detection
#endif
    }
  }
  ~ChromeWriter() { DFTRACER_LOG_DEBUG("Destructing ChromeWriter", ""); }
  void initialize(char *filename, bool throw_error);

  void log(int index, ConstEventType event_name, ConstEventType category,
           TimeResolution &start_time, TimeResolution &duration,
           std::unordered_map<std::string, std::any> *metadata,
           ProcessID process_id, ThreadID tid);

  void finalize(bool has_entry);
};
}  // namespace dftracer

#endif  // DFTRACER_CHROME_WRITER_H
