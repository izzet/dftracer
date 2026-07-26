#include <dftracer/core/aggregator/aggregator.h>

template <>
std::shared_ptr<dftracer::Aggregator>
    dftracer::Singleton<dftracer::Aggregator>::instance = nullptr;
template <>
bool dftracer::Singleton<dftracer::Aggregator>::stop_creating_instances = false;

#define NUM_INSERT(TYPE)                                               \
  insert_number_value<TYPE>(iter2, time_interval, aggregated_key, key, \
                            std::any_cast<TYPE>(value.second));

#define GENERAL_INSERT(TYPE)                                            \
  insert_general_value<TYPE>(iter2, time_interval, aggregated_key, key, \
                             std::any_cast<TYPE>(value.second));
namespace dftracer {
bool Aggregator::aggregate(AggregatedKey& aggregated_key) {
  bool is_first_local = is_first;
  is_first = false;
  std::unique_lock<std::shared_mutex> lock(mtx);
  // Use cached_interval (in the configured time_metric unit) to avoid
  // division per event
  aggregated_key.time_interval =
      (aggregated_key.time_interval / cached_interval) * cached_interval;
  auto time_iter = aggregated_data_.find(aggregated_key.time_interval);
  if (time_iter == aggregated_data_.end()) {
    aggregated_data_.insert_or_assign(
        aggregated_key.time_interval,
        std::unordered_map<AggregatedKey, AggregatedValues*>());
  }
  auto last_interval_ = last_interval;
  if (aggregated_key.time_interval > last_interval) {
    last_interval = aggregated_key.time_interval;
  }

  insert_number_value(aggregated_key.time_interval, aggregated_key, "dur",
                      aggregated_key.duration);

  // Fast path: skip metadata iteration if empty
  if (aggregated_key.additional_keys &&
      !aggregated_key.additional_keys->empty()) {
    for (const auto& [key, value] : *aggregated_key.additional_keys) {
      // Only process MT_VALUE metadata (skip MT_KEY)
      if (std::get<0>(value) == MetadataType::MT_VALUE) {
        DFTRACER_FOR_EACH_NUMERIC_TYPE(
            DFTRACER_ANY_CAST_MACRO, std::get<1>(value), {
              insert_number_value(aggregated_key.time_interval, aggregated_key,
                                  key, res.value());
              continue;
            })
        DFTRACER_FOR_EACH_STRING_TYPE(
            DFTRACER_ANY_CAST_MACRO, std::get<1>(value), {
              insert_general_value(aggregated_key.time_interval, aggregated_key,
                                   key, res.value());
              continue;
            })
      }
    }
  }

  return !is_first_local && last_interval_ != last_interval;
}
int Aggregator::get_previous_aggregations(AggregatedDataType& data, bool all) {
  std::unique_lock<std::shared_mutex> lock(mtx);
  DFTRACER_LOG_INFO("Getting %d timestamps all: %d", aggregated_data_.size(),
                    all);
  int moved = 0;
  for (auto it = aggregated_data_.begin(); it != aggregated_data_.end();) {
    if (all || it->first < last_interval) {
      data[it->first] = std::move(it->second);
      it = aggregated_data_.erase(it);
      ++moved;
    } else {
      ++it;
    }
  }
  return moved;
}
}  // namespace dftracer
