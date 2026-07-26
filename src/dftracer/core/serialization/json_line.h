#ifndef DFTRACER_SERIALIZATION_JSON_LINE_H
#define DFTRACER_SERIALIZATION_JSON_LINE_H

#include <dftracer/core/aggregator/aggregator.h>
#include <dftracer/core/common/cpp_typedefs.h>
#include <dftracer/core/common/datastructure.h>
#include <dftracer/core/common/enumeration.h>
#include <dftracer/core/common/typedef.h>
#include <dftracer/core/utils/configuration_manager.h>

#include <any>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unordered_map>

namespace dftracer {
class JsonLines {
  bool include_metadata;
  HashType hostname_hash;
  bool convert_metadata(Metadata* metadata, std::stringstream& meta_stream);
  // Shared layout for the timestamped, duration-less records: counters and
  // aggregated events differ only in their phase.
  size_t series(char* buffer, int index, ConstEventNameType event_name,
                ConstEventNameType category, TraceEventType type,
                TracePhaseType phase, TimeResolution start_time,
                ProcessID process_id, ThreadID thread_id,
                dftracer::Metadata* metadata);

 public:
  JsonLines();
  size_t initialize(char* buffer, HashType hostname_hash);
  size_t data(char* buffer, int index, ConstEventNameType event_name,
              ConstEventNameType category, TraceEventType type,
              TimeResolution start_time, TimeResolution duration,
              dftracer::Metadata* metadata, ProcessID process_id, ThreadID tid);
  // record_name is the metadata record kind (PR, FH, HH, SH, CM), not a phase.
  size_t metadata(char* buffer, ConstEventNameType name,
                  ConstEventNameType value, ConstEventNameType record_name,
                  TraceEventType type, ProcessID process_id, ThreadID thread_id,
                  bool is_string = true);
  size_t counter(char* buffer, int index, ConstEventNameType name,
                 ConstEventNameType category, TraceEventType type,
                 TimeResolution start_time, ProcessID process_id,
                 ThreadID thread_id, dftracer::Metadata* metadata);
  size_t aggregated(char* buffer, int index, ProcessID process_id,
                    dftracer::AggregatedDataType& data);
  size_t finalize(char* buffer, bool end_sym = false) {
    if (end_sym) {
      buffer[0] = ']';
      return 1;
    }
    return 0;
  }
};
}  // namespace dftracer

#endif  // DFTRACER_SERIALIZATION_JSON_LINE_H