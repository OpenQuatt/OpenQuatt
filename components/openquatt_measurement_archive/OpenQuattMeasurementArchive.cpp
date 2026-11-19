#include "OpenQuattMeasurementArchive.h"
#include "esphome/core/log.h"
#include "esp_random.h"
#include "OpenQuattFlashLayout.h"
#include "includes/sources/oq_resolved_learning_source.h"
#include <cstdio>
#include "nvs.h"

namespace esphome::openquatt_measurement_archive {
using namespace oq_measurement_archive;
using namespace oq_power_house::learning;

static_assert(kOffset == openquatt_common::OpenQuattFlashLayout::MEASUREMENT_ARCHIVE_OFFSET &&
                  kEndOffset == openquatt_common::OpenQuattFlashLayout::MEASUREMENT_ARCHIVE_END_OFFSET,
              "The archive codec and firmware flash reservation must agree");
static const char* const TAG = "oq.meetarchief";
static constexpr char kPreferences[] = "oq_arch";
static constexpr char kEnabledKey[] = "enabled";

static esp_err_t read_enabled(uint8_t& value) {
  nvs_handle_t nvs;
  auto result = nvs_open(kPreferences, NVS_READONLY, &nvs);
  if (result == ESP_OK) {
    result = nvs_get_u8(nvs, kEnabledKey, &value);
    nvs_close(nvs);
  }
  return result;
}

static bool save_enabled(bool enabled) {
  nvs_handle_t nvs;
  if (nvs_open(kPreferences, NVS_READWRITE, &nvs) != ESP_OK) return false;
  const bool saved = nvs_set_u8(nvs, kEnabledKey, enabled ? 1 : 0) == ESP_OK && nvs_commit(nvs) == ESP_OK;
  nvs_close(nvs);
  uint8_t verified = 0xFF;
  return saved && read_enabled(verified) == ESP_OK && verified == (enabled ? 1 : 0);
}

void ArchiveEnabledSwitch::write_state(bool enabled) {
  if (archive_) archive_->set_enabled(enabled);
}

void MeasurementArchive::setup() {
  // Attach to the learning endpoint before its normal handler is registered.
  setup_endpoint();
  partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "openquatt_data");
  if (!partition_ || partition_->size < kEndOffset || !state_.allocate()) {
    enabled_switch_->publish_state(false);
    publish_("storage_unavailable");
    mark_failed();
    return;
  }
  auto& state = state_[0];
  const uint32_t boot = esp_random();
  state.contexts.open(*this, state.scratch);
  state.healthy = state.measurements.open(*this, state.measurement_index, state.scratch, boot) &&
                  state.weather.open(*this, state.weather_index, state.scratch, boot);
  state.last_weather_epoch = state.weather.last_epoch();
  uint8_t stored = 0;
  const auto loaded = read_enabled(stored);
  preference_error_ = (loaded != ESP_OK && loaded != ESP_ERR_NVS_NOT_FOUND) || stored > 1;
  const bool has_saved_data = state.measurements.last_epoch() || state.weather.last_epoch();
  // Factory reset clears NVS, but leaves the data partition in place.
  // Do not resume or disclose a previous installation's measurements.
  if (has_saved_data && (loaded != ESP_OK || stored > 1)) {
    orphaned_ = loaded == ESP_ERR_NVS_NOT_FOUND;
    enabled_switch_->publish_state(false);
    publish_(orphaned_ ? "archive_unlinked" : "storage_error");
    return;
  }
  index_ready_ = state.healthy;
  enabled_ = stored_enabled_ = index_ready_ && loaded == ESP_OK && stored == 1;
  enabled_switch_->publish_state(enabled_);
  publish_(!state.healthy ? "storage_error" : enabled_ ? "waiting" : "off");
}
void MeasurementArchive::set_enabled(bool enabled) {
  if (!index_ready_ || (enabled && !healthy()) || (enabled == stored_enabled_ && !preference_error_)) return;
  // Apply off in RAM even when the flash preference cannot be written.
  enabled_ = false;
  if (state_) {
    state_[0].collector.reset();
    state_[0].new_session = true;
  }
  preference_error_ = !save_enabled(enabled);
  if (!preference_error_) stored_enabled_ = enabled;
  enabled_ = enabled && !preference_error_;
  publish_(healthy() ? (enabled_ ? "waiting" : "off") : "storage_error");
  // Keep the confirmed switch position on failure so the user can retry.
  enabled_switch_->publish_state(stored_enabled_);
}

void MeasurementArchive::publish_(const char* code) {
  if (!status_) return;
  const auto period = index_ready_ ? state_[0].measurements.period() : ArchivePeriod{};
  char value[112];
  std::snprintf(value, sizeof(value), "%s|%u|%u|%zu", preference_error_ ? "preference_error" : code,
                static_cast<unsigned>(period.first_epoch), static_cast<unsigned>(period.last_epoch), period.count);
  if (status_->state != value) status_->publish_state(value);
}
void MeasurementArchive::dump_config() {
  ESP_LOGCONFIG(TAG, "Meetarchief: %u measurements, %u forecasts, healthy=%d, allocated=%u B",
                static_cast<unsigned>(measurements()), static_cast<unsigned>(forecasts()), healthy(),
                static_cast<unsigned>(sizeof(State)));
}
bool MeasurementArchive::read(uint32_t offset, void* data, size_t size) {
  return partition_ && offset >= kOffset && offset <= kEndOffset && size <= kEndOffset - offset &&
         esp_partition_read(partition_, offset, data, size) == ESP_OK;
}
bool MeasurementArchive::write(uint32_t offset, const void* data, size_t size) {
  return partition_ && offset >= kOffset && offset <= kEndOffset && size <= kEndOffset - offset &&
         esp_partition_write(partition_, offset, data, size) == ESP_OK;
}
bool MeasurementArchive::erase(uint32_t offset) {
  return partition_ && offset >= kOffset && offset % kSectorBytes == 0 && offset <= kEndOffset - kSectorBytes &&
         esp_partition_erase_range(partition_, offset, kSectorBytes) == ESP_OK;
}

uint64_t MeasurementArchive::live_contexts_() const {
  const auto& state = state_[0];
  // The measurement collector can have a bound slot before its next row lands.
  return state.measurements.live_contexts() | state.weather.live_contexts() | context_bit(state.context);
}

template <typename T>
static bool fresh(const PhysicalMeasurement<T>& value, uint64_t now) {
  return value.valid && value.source.id && value.source_generation && value.received_monotonic_ms &&
         value.timing.max_age_ms && value.received_monotonic_ms <= now &&
         now - value.received_monotonic_ms <= value.timing.max_age_ms;
}

void MeasurementArchive::observe(const LearningSourceInput& input, const CalorimetryResult& heat,
                                 const PassiveContextView& context, uint32_t cic_flow_endpoint, float supply_offset_c) {
  if (!index_ready_ || !healthy()) return;
  auto& state = state_[0];
  const auto emit = [&](Measurement row, uint32_t) {
    const std::lock_guard<std::mutex> guard(index_mutex_);
    const auto context_id =
        state.context ? state.context : state.contexts.bind(state.context_bytes, state.context_size, live_contexts_());
    if (!context_id) return false;
    const bool registered = context_id != kUnregisteredContext;
    if (registered) state.context = context_id;
    if (state.new_session) row.flags |= NEW_SESSION;
    const bool saved = state.measurements.append(row, context_id);
    if (saved) {
      state.new_session = false;
      state.source_unregistered = !registered;
    }
    return saved;
  };
  if (!enabled_ || input.operation.service_or_ota) {
    state.collector.reset();
    state.new_session = true;
    publish_(enabled_ ? "paused" : "off");
    return;
  }
  if (!passive_runtime_detail::valid_context(context)) {
    state.healthy = state.collector.finish_partial(emit);
    state.new_session = true;
    publish_(state.healthy ? "waiting_sources" : "storage_error");
    return;
  }
  if (!input.epoch_s || input.epoch_s <= state.measurements.last_epoch()) {
    state.collector.reset();
    state.new_session = true;
    publish_("waiting_clock");
    return;
  }
  using Route = oq_sources::LearningSourceRoute;
  HeatRoute route;
  const auto& flow = input.system_heat_enabled ? input.circuit_flow_lph : input.flow_lph;
  auto flow_source = static_cast<Route>(flow.source.id);
  // When the outdoor resolver reports stopped pumps, a fresh controller
  // receipt may still prove water flow through the system circuit.
  if (input.system_heat_enabled && flow_source == Route::SYNTHESIZED_ZERO_FLOW && flow.valid &&
      flow.provenance == MeasurementProvenance::PHYSICAL_RECEIPT)
    flow_source = Route::CONTROLLER_FLOW;
  route.flow_source = static_cast<uint8_t>(flow_source);
  route.flow_endpoint = flow_source == Route::CIC_FLOW ? cic_flow_endpoint : 0;
  if (input.system_heat_enabled) {
    route.boundary = HeatBoundary::SYSTEM;
    route.supply_source = static_cast<uint8_t>(input.system_supply_c.source.id);
    route.supply_endpoint = input.system_supply_c.source_generation;
    route.supply_offset_c = supply_offset_c;
  }

  HeatRoute previous;
  if (state.context_size) std::memcpy(&previous, state.context_bytes, sizeof(previous));
  const auto flow_generation = input.flow_lph.source_generation;
  const bool same_generation = state.flow_generation == flow_generation;
  if (state.context_size && same_generation && route.boundary == previous.boundary &&
      (flow_source == Route::NONE || flow_source == Route::SYNTHESIZED_ZERO_FLOW)) {
    // Temporary unavailability does not rename the physical route. A selected
    // synthetic zero is identified separately by the record's quality flag.
    route.flow_source = previous.flow_source;
    route.flow_endpoint = previous.flow_endpoint;
  }
  const size_t size = sizeof(route) + context.size;
  if (size > sizeof(state.context_bytes)) {
    state.healthy = false;
    publish_("storage_error");
    return;
  }
  if (size != state.context_size || !same_generation || context.context_revision != state.source_revision ||
      std::memcmp(&previous, &route, sizeof(route)) != 0 ||
      std::memcmp(state.context_bytes + sizeof(route), context.bytes, context.size) != 0) {
    if (!state.collector.finish_partial(emit)) {
      state.healthy = false;
      publish_("storage_error");
      return;
    }
    std::memcpy(state.context_bytes, &route, sizeof(route));
    std::memcpy(state.context_bytes + sizeof(route), context.bytes, context.size);
    state.context_size = size;
    state.context = 0;  // Bind a flash slot only when this context has a record.
    state.flow_generation = flow_generation;
    state.source_revision = context.context_revision;
    state.new_session = true;
  }
  Sample sample;
  sample.monotonic_ms = input.monotonic_ms;
  sample.epoch = input.epoch_s;
  sample.context = 1;  // The component closes the window before replacing its context.
  sample.room_c = fresh(input.room_c, input.monotonic_ms) ? input.room_c.value : NAN;
  sample.outside_c = fresh(input.outside_c, input.monotonic_ms) ? input.outside_c.value : NAN;
  sample.setpoint_c = fresh(input.setpoint_c, input.monotonic_ms) ? input.setpoint_c.value : NAN;
  sample.heat_w = heat.valid ? heat.heat_to_water_w : NAN;
  if (heat.valid && flow_source == Route::SYNTHESIZED_ZERO_FLOW) sample.flags |= SYNTHESIZED_ZERO_FLOW;
  const auto add_unit_operation = [&](const auto& unit, uint16_t running_flag) {
    const bool compressor_valid = fresh(unit.compressor_active, input.monotonic_ms);
    const bool defrost_valid = fresh(unit.defrost_active, input.monotonic_ms);
    const bool mode_valid = fresh(unit.mode, input.monotonic_ms);
    if (compressor_valid && unit.compressor_active.value) sample.flags |= running_flag;
    if (defrost_valid && unit.defrost_active.value) sample.flags |= DEFROST;
    if (mode_valid && unit.mode.value == HeatPumpMode::COOLING) sample.flags |= COOLING;
    if (!compressor_valid || !defrost_valid || !mode_valid) sample.flags |= UNKNOWN_OPERATION;
  };
  add_unit_operation(input.hp1, HP1_RUNNING);
  if (input.hp2.present) add_unit_operation(input.hp2, HP2_RUNNING);
  const bool boiler_valid = fresh(input.boiler_heat, input.monotonic_ms);
  if (boiler_valid && input.boiler_heat.value == BoilerHeatState::HEAT_ACTIVE) sample.flags |= BOILER;
  if (input.operation.active_limit) sample.flags |= LIMITED;
  if (input.operation.service_or_ota) sample.flags |= SERVICE;
  if (!input.operation.control_mode_valid || !input.operation.active_limit_valid ||
      !input.operation.service_or_ota_valid || !boiler_valid)
    sample.flags |= UNKNOWN_OPERATION;
  state.healthy = state.collector.observe(sample, emit);
  if (!state.healthy) {
    publish_("storage_error");
    return;
  }
  publish_(state.source_unregistered ? "recording_unregistered" : "recording");
}

void MeasurementArchive::weather(const oq_weather::Series& series) {
  if (!enabled_ || !healthy() || !series.fetched_epoch || !series.generation) return;
  auto& state = state_[0];
  if (series.fetched_epoch <= state.last_weather_epoch) return;
  struct __attribute__((packed)) WeatherContext {
    float latitude, longitude;
    char model[48];
  } context{};
  context.latitude = series.latitude;
  context.longitude = series.longitude;
  std::snprintf(context.model, sizeof(context.model), "%s", oq_weather::kModel);
  Weather row{};
  row.epoch = series.fetched_epoch;
  size_t first = 0;
  while (first + 1 < oq_weather::kHours && series.time[first + 1] <= row.epoch) ++first;
  if (first + 4 >= oq_weather::kHours) return;
  row.first_hour_epoch = series.time[first];
  row.current_epoch = series.current_wind.time_epoch;
  row.current_speed_centi = series.current_wind.valid ? std::lround(series.current_wind.speed_mps * 100) : kMissingWind;
  row.current_direction_centi = series.current_wind.valid && series.current_wind.direction_valid
                                    ? std::lround(series.current_wind.direction_deg * 100)
                                    : kMissingWind;
  for (size_t i = 0; i < 5; ++i) {
    row.outside_centi[i] = temperature(series.temperature[first + i]);
    row.speed_centi[i] = std::lround(series.wind_mps[first + i] * 100);
    const bool direction_valid = series.wind_direction_valid_mask & (uint64_t{1} << (first + i));
    row.direction_two_degrees[i] =
        direction_valid ? std::lround(series.wind_direction_deg[first + i] / 2) % 180 : UINT8_MAX;
    if (direction_valid) row.valid_mask |= 1U << i;
  }
  {
    const std::lock_guard<std::mutex> guard(index_mutex_);
    const auto context_id = state.contexts.bind(&context, sizeof(context), live_contexts_());
    state.healthy = context_id && state.weather.append(row, context_id);
  }
  if (state.healthy)
    state.last_weather_epoch = row.epoch;
  else
    publish_("storage_error");
}

}  // namespace esphome::openquatt_measurement_archive
