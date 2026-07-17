//
// Created by haridev on 3/28/23.
//

#ifndef DFTRACER_ENUMERATION_H
#define DFTRACER_ENUMERATION_H
#include <cpp-logger/logger.h>
enum WriterType : uint8_t { CHROME = 0 };
enum ProfilerStage : uint8_t {
  PROFILER_INIT = 0,
  PROFILER_FINI = 1,
  PROFILER_OTHER = 2
};
enum ProfileType : uint8_t {
  PROFILER_PRELOAD = 0,
  PROFILER_PY_APP = 1,
  PROFILER_CPP_APP = 2,
  PROFILER_C_APP = 3,
  PROFILER_ANY = 4
};
enum ProfileInitType : uint8_t {
  PROFILER_INIT_NONE = 0,
  PROFILER_INIT_LD_PRELOAD = 1,
  PROFILER_INIT_FUNCTION = 2
};
enum ValueType : uint8_t { VALUE_TYPE_NUMBER = 0, VALUE_TYPE_STRING = 1 };
enum MetadataType : uint8_t { MT_KEY = 0, MT_VALUE = 1, MT_IGNORE = 2 };
enum AggregationType : uint8_t {
  AGGREGATION_TYPE_FULL = 0,
  AGGREGATION_TYPE_SELECTIVE = 1
};
enum TimeMetricType : uint8_t {
  TIME_METRIC_US = 0,
  TIME_METRIC_NS = 1,
  TIME_METRIC_MS = 2,
  TIME_METRIC_SEC = 3
};
enum class RuleOp { AND, OR, NOT, EQ, NEQ, GT, LT, GTE, LTE, IN, LIKE };

inline MetadataType convert(const int& s) {
  if (s == 0) {
    return MetadataType::MT_KEY;
  } else if (s == 1) {
    return MetadataType::MT_VALUE;
  } else if (s == 2) {
    return MetadataType::MT_IGNORE;
  } else {
    return MetadataType::MT_KEY;
  }
}

inline void convert(const int& s, MetadataType& type) {
  if (s == 0) {
    type = MetadataType::MT_KEY;
  } else if (s == 1) {
    type = MetadataType::MT_VALUE;
  } else if (s == 2) {
    type = MetadataType::MT_IGNORE;
  } else {
    type = MetadataType::MT_KEY;
  }
}

inline void convert(const std::string& s, ProfileInitType& type) {
  if (s == "PRELOAD") {
    type = ProfileInitType::PROFILER_INIT_LD_PRELOAD;
  } else if (s == "FUNCTION") {
    type = ProfileInitType::PROFILER_INIT_FUNCTION;
  } else {
    type = ProfileInitType::PROFILER_INIT_NONE;
  }
}
inline void convert(const std::string& s, cpplogger::LoggerType& type) {
  if (s == "DEBUG") {
    type = cpplogger::LoggerType::CPP_LOGGER_DEBUG;
  } else if (s == "INFO") {
    type = cpplogger::LoggerType::CPP_LOGGER_INFO;
  } else if (s == "WARN") {
    type = cpplogger::LoggerType::CPP_LOGGER_WARN;
  } else {
    type = cpplogger::LoggerType::CPP_LOGGER_ERROR;
  }
}
inline void convert(const std::string& s, AggregationType& type) {
  if (s == "FULL") {
    type = AggregationType::AGGREGATION_TYPE_FULL;
  } else if (s == "SELECTIVE") {
    type = AggregationType::AGGREGATION_TYPE_SELECTIVE;
  } else {
    type = AggregationType::AGGREGATION_TYPE_FULL;
  }
}
inline std::string to_string(const AggregationType& type) {
  switch (type) {
    case AggregationType::AGGREGATION_TYPE_FULL:
      return "FULL";
    case AggregationType::AGGREGATION_TYPE_SELECTIVE:
      return "SELECTIVE";
    default:
      return "FULL";
  }
}
inline void convert(const std::string& s, TimeMetricType& type) {
  if (s == "NS") {
    type = TimeMetricType::TIME_METRIC_NS;
  } else if (s == "MS") {
    type = TimeMetricType::TIME_METRIC_MS;
  } else if (s == "SEC") {
    type = TimeMetricType::TIME_METRIC_SEC;
  } else if (s == "US") {
    type = TimeMetricType::TIME_METRIC_US;
  } else {
    type = TimeMetricType::TIME_METRIC_US;
  }
}
inline std::string to_string(const TimeMetricType& type) {
  switch (type) {
    case TimeMetricType::TIME_METRIC_NS:
      return "NS";
    case TimeMetricType::TIME_METRIC_MS:
      return "MS";
    case TimeMetricType::TIME_METRIC_SEC:
      return "SEC";
    case TimeMetricType::TIME_METRIC_US:
      return "US";
    default:
      return "US";
  }
}
// Number of TIME_METRIC units in one second. Used by any consumer that needs
// to convert a wall-clock quantity (e.g. a millisecond interval) into the
// same unit that DFTLogger::get_time() is currently returning.
inline double time_metric_units_per_second(const TimeMetricType& type) {
  switch (type) {
    case TimeMetricType::TIME_METRIC_NS:
      return 1e9;
    case TimeMetricType::TIME_METRIC_MS:
      return 1e3;
    case TimeMetricType::TIME_METRIC_SEC:
      return 1.0;
    case TimeMetricType::TIME_METRIC_US:
    default:
      return 1e6;
  }
}

#define METADATA_NAME_PROCESS "PR"
#define METADATA_NAME_PROCESS_NAME "process_name"
#define METADATA_NAME_THREAD_NAME "thread_name"
#define METADATA_NAME_FILE_HASH "FH"
#define METADATA_NAME_HOSTNAME_HASH "HH"
#define METADATA_NAME_STRING_HASH "SH"
#define CUSTOM_METADATA "CM"
#endif  // DFTRACER_ENUMERATION_H
