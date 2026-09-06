#pragma once

#include "../learning/oq_ph_learning_platform.h"

#if defined(ESP_PLATFORM) && OQ_PH_LEARNING_CORE_AVAILABLE

#include <cstring>
#include <new>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "OpenQuattFlashLayout.h"
#include "PsramBuffer.h"
#include "../boiler/oq_otb_telemetry.h"
#include "../learning/oq_ph_passive_runtime_logic.h"
#include "../learning/oq_ph_learning_journal.h"
#include "../learning/oq_ph_learning_live_logic.h"
#include "../learning/oq_ph_learning_persistence_logic.h"
#include "oq_ph_learning_json.h"
#include "oq_sensor_source_runtime.h"

namespace oq_ph_learning {

using namespace oq_power_house::learning;
using esphome::openquatt_common::OpenQuattFlashLayout;

static_assert(OpenQuattFlashLayout::HOUSE_LEARNING_SLOT_COUNT == 2U,
              "The journal selector and caller-owned slot buffers require exactly two slots");
static_assert(kLearningJournalMaxBytes <= OpenQuattFlashLayout::HOUSE_LEARNING_SLOT_SIZE,
              "The encoded learning journal must fit one flash slot");
static_assert(kMaxSegmentRecords == kMaxExportRecordRows,
              "The export budget must cover every retained passive-learning record");

// One main-loop owner. HTTP handlers receive only copied JSON caches, never these
// records or fit state. The large object and all scratch space are strict PSRAM.
struct DiagnosticRow {
  uint32_t epoch = 0;
  uint32_t invalid = 0;
  uint32_t source_generation = 0;
  uint32_t control_generation = 0;
  float room = NAN, setpoint = NAN, outside = NAN, heat = NAN, base = NAN, request = NAN;
  uint32_t coverage_s = 0;
  int control_mode = 0;
  bool heat_valid = false, training_qualified = false;
  bool hp1_active = false, hp2_active = false, hp1_active_known = false, hp2_active_known = false;
  bool active_limit = false, active_limit_known = false;
  bool boiler_active = false, boiler_known = false;
  bool protection_active = false, protection_known = false;
  bool request_known = false;
};

struct RuntimeStorage {
  PassiveRuntimeStorage learner;
  LearningSourceInput input;
  PassiveTickInput tick;
  PassiveRuntimeSummary summary;
  PassiveRuntimeConfig config;
  CalorimetryReconfirmationState calorimetry_reconfirmation;
  oq_sources::ResolvedLearningSource sources[4];
  uint32_t source_revisions[4]{};
  uint8_t context[kMaxPassiveContextBytes]{};
  size_t context_size = 0;
  LearningJournalStore journal;
  char json[kExportJsonBufferSize]{};
  DiagnosticRow rows[kMaxExportDiagnosticRows];
  DiagnosticCaptureGate diagnostic_capture;
  size_t row_count = 0, row_next = 0;
  uint64_t recovery_until_ms = 0;
  float last_setpoint = NAN;
  uint32_t context_revision = 1;
  bool changed = false, reset_requested = false;
  const esp_partition_t* partition = nullptr;
  SnapshotDiagnostics source_diagnostics;
  uint64_t max_tick_us = 0;
};

class Runtime {
 public:
  void pause() {
    if (!storage_) return;
    auto& state = storage_[0];
    pause_diagnostic_capture(state.diagnostic_capture);
    if (state.learner.initialized) pause_passive_runtime(state.learner, oq_sources::monotonic_ms());
    publish_paused_status_(state);
  }

  void request_reset() {
    id(oq_ph_learning_enabled).turn_off();
    if (!storage_) {
      reset_without_storage_();
      return;
    }
    auto& state = storage_[0];
    state.reset_requested = true;
    state.journal.request_reset();
    pause();
    publish_reset_pending_export_();
  }

  void configuration_changed() {
    if (!storage_) return;
    auto& state = storage_[0];
    state.changed = true;
    // Invalidate source caches on every setting event, including A->B->A before
    // the next periodic selection. This does not republish or change controls.
    oq_sensor_source::runtime().source_configuration_changed();
    pause();
  }

  void tick() {
    const uint64_t started_us = static_cast<uint64_t>(esp_timer_get_time());
    if (!storage_ && !setup_()) return;
    auto& state = storage_[0];
    const uint64_t now_ms = oq_sources::monotonic_ms();
    const auto clock = id(oq_time).now();
    const uint32_t epoch = clock.is_valid() ? clock.timestamp : 0;
    capture_(state, now_ms, epoch);
    build_context_(state);
    const bool enabled = id(oq_ph_learning_enabled).state;
    auto& input = state.input;
    // Wait for selected sources before binding a boot context. A temporary
    // missing receipt pauses collection. A resolver route/provenance change
    // starts a new dataset, including changes observed between learner ticks.
    bool context_valid = state.context_size > 0;
    for (const auto& source : state.sources) context_valid = context_valid && source.valid;
    const bool sources_changed = context_valid && observe_source_revisions(state.source_revisions, state.sources);
    const bool context_changed =
        state.learner.initialized &&
        (state.changed || sources_changed ||
         (context_valid && (state.context_size != state.learner.context_size ||
                            memcmp(state.context, state.learner.context_bytes, state.context_size) != 0)));
    if (context_changed && ++state.context_revision == 0) state.context_revision = 1;
    input.source_cohort_generation = state.context_revision;
    input.physical_context_generation = state.context_revision;
    input.control_generation = state.context_revision;
    input.operation.captured_control_generation = state.context_revision;
    PassiveContextView context{state.context, state.context_size, state.context_revision, state.context_revision,
                               state.context_revision};
    const bool reset_processed = state.reset_requested;
    if (state.changed || context_changed || reset_processed) {
      reset_passive_runtime(state.learner);
      state.journal.reset(
          [&state]() { return erase_slots_(state.partition); },
          [&state](size_t slot, uint8_t* data, size_t size) { return read_slot_(state.partition, slot, data, size); });
      state.changed = false;
    }
    if (reset_processed) {
      state.reset_requested = false;
      state.row_count = state.row_next = 0;
      reset_diagnostic_capture(state.diagnostic_capture);
    }
    if (context_valid && !state.learner.initialized)
      initialize_passive_runtime(state.learner, context, state.config, enabled);
    if (state.learner.initialized && context_valid && epoch != 0 && !state.journal.loaded) {
      LearningJournalRecords records;
      if (state.journal.load(
              context, state.config.quality, epoch,
              [&state](size_t slot, uint8_t* data, size_t size) {
                return read_slot_(state.partition, slot, data, size);
              },
              records))
        restore_passive_records(state.learner, records);
    }

    const auto batch = build_learning_snapshot(input, state.config.quality, SnapshotPurpose::STRUCTURAL_BATCH);
    const auto dynamic = build_learning_snapshot(input, state.config.quality, SnapshotPurpose::THERMAL_DYNAMIC);
    state.source_diagnostics = combined_snapshot_diagnostics(batch, dynamic);
    state.tick = PassiveTickInput{};
    auto& tick = state.tick;
    tick.context = context;
    tick.now_monotonic_ms = now_ms;
    tick.now_epoch_s = epoch;
    tick.opted_in = enabled;
    tick.context_valid = context_valid;
    tick.active_line = active_line_();
    tick.active_line_valid = oq_power_house::valid_house_line(tick.active_line);
    // The nominal room target is deliberately the stable setpoint. Using the
    // measured room value here would restart the bounded multi-tick fit on
    // every small sensor update.
    tick.reference_room_c = input.setpoint_c.value;
    tick.reference_setpoint_c = input.setpoint_c.value;
    tick.reference_context_valid = input.setpoint_c.valid && isfinite(input.setpoint_c.value);
    tick.batch_snapshot_available = batch.has_snapshot;
    tick.batch_snapshot = batch.snapshot;
    tick.dynamic_snapshot_available = dynamic.has_snapshot;
    tick.dynamic_snapshot = dynamic.snapshot;
    if (state.learner.initialized && context_valid) {
      tick_passive_runtime(state.learner, tick);
    } else if (state.learner.initialized) {
      pause_passive_runtime(state.learner, now_ms);
    }
    const auto diagnostic_capture = diagnostic_capture_decision(state.diagnostic_capture, enabled && !reset_processed);
    if (diagnostic_capture.capture) capture_diagnostics_(state, dynamic, epoch, diagnostic_capture.use_previous);
    const bool persistence_window_safe = input.operation.service_or_ota_valid && !input.operation.service_or_ota;
    if (state.learner.initialized && context_valid && epoch != 0 && enabled && persistence_window_safe)
      state.journal.save(
          passive_runtime_dataset(state.learner), state.config.quality, epoch, now_ms,
          state.learner.diagnostics.accepted_batch_records,
          [&state](size_t slot) { return erase_slot_(state.partition, slot); },
          [&state](size_t slot, const uint8_t* data, size_t size) {
            return esp_partition_write(state.partition, slot_offset_(slot), data, size) == ESP_OK;
          },
          [&state](size_t slot, uint8_t* data, size_t size) { return read_slot_(state.partition, slot, data, size); });
    state.summary = passive_runtime_summary(state.learner, now_ms);
    const uint64_t elapsed = static_cast<uint64_t>(esp_timer_get_time()) - started_us;
    if (elapsed > state.max_tick_us) state.max_tick_us = elapsed;
    publish_(state, enabled, epoch);
  }

 private:
  esphome::openquatt_common::PsramObjectArray<RuntimeStorage, 1> storage_;
  bool allocation_attempted_ = false;

  template <typename T>
  void watch_number_(T& entity) {
    entity.add_on_state_callback([this](float) { this->configuration_changed(); });
  }
  template <typename T>
  void watch_select_(T& entity) {
    entity.add_on_state_callback([this](size_t) { this->configuration_changed(); });
  }

  bool setup_() {
    if (allocation_attempted_) return false;
    allocation_attempted_ = true;
    if (!storage_.allocate()) {
      const char* error =
          "{\"schema\":1,\"mode\":\"passive\",\"storage_ready\":false,\"status\":\"psram_allocation_failed\",\"auto_"
          "apply_allowed\":false}";
      id(oq_ph_learning_status_endpoint).publish_status_json(error, strlen(error));
      return false;
    }
    auto& state = storage_[0];
    state.partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "openquatt_data");
    state.journal.setup(state.partition != nullptr &&
                        OpenQuattFlashLayout::HOUSE_LEARNING_END_OFFSET <= state.partition->size);
    watch_select_(id(room_temp_source));
    watch_select_(id(room_setpoint_source));
    watch_select_(id(outside_temp_source));
    watch_select_(id(flow_source));
    watch_select_(id(oq_duo_outdoor_flow_mode));
    watch_select_(id(hp_generation));
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    watch_select_(id(oq_q_flow_source));
#endif
    watch_select_(id(oq_heat_control_mode));
    watch_select_(id(oq_cm_override));
    watch_select_(id(oq_boiler_connection));
    watch_number_(id(oq_ph_learning_heat_uncertainty));
    watch_number_(id(oq_ph_learning_maximum_flow));
    watch_number_(id(oq_ph_learning_junction_tolerance));
    watch_number_(id(oq_ph_learning_gain_bound));
    watch_number_(id(house_cold_temp_c));
    watch_number_(id(house_zero_power_temp_c));
    watch_number_(id(house_rated_power_w));
    watch_number_(id(ph_kp_w_per_k));
    watch_number_(id(ph_comfort_band_below_c));
    watch_number_(id(ph_comfort_band_above_c));
    watch_number_(id(ph_demand_rise_time_min));
    watch_number_(id(ph_demand_fall_time_min));
    watch_number_(id(hp1_water_in_temp_offset));
    watch_number_(id(hp1_water_out_temp_offset));
#if OQ_TOPOLOGY_DUO
    watch_number_(id(hp2_water_in_temp_offset));
    watch_number_(id(hp2_water_out_temp_offset));
#endif
    id(oq_ph_learning_calorimetry_confirmed).add_on_state_callback([this](bool confirmed) {
      this->calorimetry_confirmation_changed_(confirmed);
    });
    id(cic_feed_url).add_on_state_callback([this](const std::string&) { this->configuration_changed(); });
    return true;
  }

  static oq_power_house::HouseLine active_line_() {
    const float span = id(house_zero_power_temp_c).state - id(house_cold_temp_c).state;
    return {span > 0.0f ? id(house_rated_power_w).state / span : NAN, id(house_zero_power_temp_c).state};
  }

  void calorimetry_confirmation_changed_(bool confirmed) {
    if (!storage_) return;
    auto& state = storage_[0];
    if (!calorimetry_confirmation_invalidates(state.calorimetry_reconfirmation, confirmed)) {
      pause();
      return;
    }
    configuration_changed();
  }

  void capture_(RuntimeStorage& state, uint64_t now_ms, uint32_t epoch) {
    state.input = LearningSourceInput{};
    auto& in = state.input;
    in.monotonic_ms = now_ms;
    in.epoch_s = epoch;
    state.sources[0] = oq_sensor_source::runtime().resolved_room_temperature();
    state.sources[1] = oq_sensor_source::runtime().resolved_room_setpoint();
    state.sources[2] = oq_sensor_source::runtime().resolved_outside_temperature();
    state.sources[3] = oq_sensor_source::runtime().resolved_flow_rate();
    in.room_c = resolved_learning_measurement(state.sources[0], now_ms);
    in.setpoint_c = resolved_learning_measurement(state.sources[1], now_ms);
    in.outside_c = resolved_learning_measurement(state.sources[2], now_ms);
    in.flow_lph = resolved_learning_measurement(state.sources[3], now_ms);
    in.hp1 = learning_hp_measurements(oq_sources::hp1, PhysicalUnit::HP1, id(hp1_water_in_temp_offset).state,
                                      id(hp1_water_out_temp_offset).state, state.context_revision);
#if OQ_TOPOLOGY_DUO
    in.hp2 = learning_hp_measurements(oq_sources::hp2, PhysicalUnit::HP2, id(hp2_water_in_temp_offset).state,
                                      id(hp2_water_out_temp_offset).state, state.context_revision);
#endif
    apply_compile_time_installation_contract(in, OQ_TOPOLOGY_DUO);
    auto& calorimetry = in.calorimetry;
    calorimetry.calorimetry_generation = state.context_revision;
    calorimetry.heat_uncertainty_w = id(oq_ph_learning_heat_uncertainty).state;
    calorimetry.uncertainty_proven =
        id(oq_ph_learning_calorimetry_confirmed).state && calorimetry.heat_uncertainty_w > 0.0f;
    calorimetry.max_flow_lph = id(oq_ph_learning_maximum_flow).state;
    calorimetry.max_series_junction_delta_c = id(oq_ph_learning_junction_tolerance).state;
    calorimetry.zero_flow_proof =
        calorimetry.uncertainty_proven ? ZeroFlowProof::PHYSICAL_METER_COVERS_BOUNDARY : ZeroFlowProof::NOT_PROVEN;
    const bool opentherm_selected = id(oq_boiler_connection).current_option() == "OpenTherm";
    const bool boiler_runtime_available = id(oq_boiler_connection).has_state() &&
                                          (opentherm_selected || id(oq_boiler_connection).current_option() == "R1") &&
                                          id(oq_heat_mode_code) == 0 && !id(oq_runtime_polling_paused).state &&
                                          !id(oq_boiler_runtime_pause_state) &&
                                          !id(oq_boiler_connection_mismatch_state) &&
                                          !id(oq_boiler_transport_settle_required) && !id(oq_boiler_rearm_required);
    const oq_boiler::BoilerCommand boiler_command{id(oq_boiler_command_valid),
                                                  id(oq_boiler_command_demand_present),
                                                  id(oq_boiler_command_heat_request),
                                                  id(oq_boiler_command_requested_power_w),
                                                  id(oq_boiler_command_target_temperature_c),
                                                  id(oq_boiler_command_source_code),
                                                  id(oq_boiler_command_updated_ms)};
    bool applied_ot_command_active = false;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    applied_ot_command_active = opentherm_selected && id(oq_otb_applied_command_active);
#endif
    const auto boiler_contract = learning_cm2_boiler_contract(
        boiler_runtime_available, id(oq_control_mode_code), boiler_command, id(oq_boiler_output_request),
        id(boiler_relay).state, now_ms, applied_ot_command_active);
    PhysicalMeasurement<BoilerHeatState> boiler_telemetry;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    if (opentherm_selected) {
      const auto receipt = oq_otb::telemetry_state.field_receipt(oq_otb::FIELD_STATUS);
      // oq_boiler_transport_active denotes CH activity, not a healthy idle link.
      boiler_telemetry = learning_boiler_status(
          receipt.value, receipt.received_ms, receipt.valid,
          boiler_runtime_available && id(oq_otb_link_available_state) && !id(oq_otb_startup_probe_active));
    }
#endif
    in.boiler_heat = learning_boiler_heat(opentherm_selected, boiler_contract, boiler_telemetry);
    auto& op = in.operation;
    op.captured_monotonic_ms = now_ms;
    const int cm = id(oq_control_mode_code);
    op.control_mode_valid = true;
    op.control_mode = id(oq_heat_mode_code) == 0 && (cm == 0 || cm == 1 || cm == 2) ? LearningControlMode::HEATING
                                                                                    : LearningControlMode::OTHER;
    op.active_limit_valid =
        learning_strategy_output_current(id(oq_strategy_output_valid), id(oq_strategy_output_source_code),
                                         id(oq_strategy_active_code), millis(), id(oq_strategy_output_updated_ms)) &&
        isfinite(id(oq_strategy_water_limit_factor));
    op.active_limit = id(oq_strategy_water_limit_factor) < 0.999f || id(oq_strategy_water_trip_active) ||
                      id(oq_strategy_water_hard_trip_active) || id(oq_strategy_hp_saturated) ||
                      id(oq_power_cap_f) < 20 || !learning_hp_protection_clear(oq_sources::hp1, now_ms);
#if OQ_TOPOLOGY_DUO
    op.active_limit = op.active_limit || !learning_hp_protection_clear(oq_sources::hp2, now_ms);
#endif
    op.service_or_ota_valid = true;
    op.service_or_ota = cm >= 98 || id(oq_commissioning_active) || id(oq_hp_water_calibration_active) ||
                        id(oq_runtime_polling_paused).state ||
                        (id(oq_ota_phase_code) >= 1 && id(oq_ota_phase_code) <= 3);
    if (in.setpoint_c.valid && isfinite(in.setpoint_c.value)) {
      if (!isfinite(state.last_setpoint) || fabsf(state.last_setpoint - in.setpoint_c.value) > 0.01f)
        state.recovery_until_ms = now_ms + kSetpointRecoveryMs;
      state.last_setpoint = in.setpoint_c.value;
    }
    op.setpoint_recovery_valid = in.setpoint_c.valid;
    op.setpoint_recovery = now_ms < state.recovery_until_ms;
    op.comfort_acceptable_valid = in.room_c.valid && in.setpoint_c.valid &&
                                  isfinite(id(ph_comfort_band_below_c).state) &&
                                  isfinite(id(ph_comfort_band_above_c).state);
    op.comfort_acceptable = in.room_c.value >= in.setpoint_c.value - id(ph_comfort_band_below_c).state &&
                            in.room_c.value <= in.setpoint_c.value + id(ph_comfort_band_above_c).state;
    state.config.thermal_model.initial_heat_loss_w_per_k = active_line_().heat_loss_w_per_k;
    const float gain = id(oq_ph_learning_gain_bound).state;
    state.config.thermal_window.unmodeled_gain_bound_valid = isfinite(gain) && gain > 0;
    state.config.thermal_window.unmodeled_gain_bound_w = gain > 0 ? gain : NAN;
  }

  void build_context_(RuntimeStorage& state) {
    auto writer = learning_journal_detail::Writer{state.context, sizeof(state.context)};
    const auto u32 = [&](uint32_t v) { learning_journal_detail::write_u32(writer, v); };
    const auto f32 = [&](float v) { learning_journal_detail::write_float(writer, v); };
    const auto chars = [&](const char* value, size_t length) {
      u32(static_cast<uint32_t>(length));
      for (size_t index = 0; index < length; ++index) {
        if (writer.position >= writer.capacity) {
          writer.ok = false;
          break;
        }
        writer.bytes[writer.position++] = static_cast<uint8_t>(value[index]);
      }
    };
    const auto str = [&](const std::string& value) { chars(value.data(), value.size()); };
    // Compatibility is explicit: ordinary rebuilds and web fixes retain data.
    u32(kLearningAlgorithmVersion);
    u32(static_cast<uint32_t>(state.input.topology));
    u32(static_cast<uint32_t>(state.input.calorimetry.meter_boundary));
    u32(static_cast<uint32_t>(state.input.calorimetry.fluid_model));
    u32(static_cast<uint32_t>(state.input.calorimetry.duo_series_order));
    u32(OQ_HARDWARE_HEATPUMP_CONTROLLER_Q);
    for (const auto& source : state.sources) {
      u32(static_cast<uint32_t>(source.route));
      u32(static_cast<uint32_t>(source.component_route));
      u32(static_cast<uint32_t>(source.secondary_route));
      u32(static_cast<uint32_t>(source.composite_operation));
      u32(source.configuration.selected);
      u32(source.configuration.auxiliary_a);
      u32(source.configuration.auxiliary_b);
    }
    str(id(room_temp_source).current_option());
    str(id(room_setpoint_source).current_option());
    str(id(outside_temp_source).current_option());
    str(id(flow_source).current_option());
    str(id(oq_duo_outdoor_flow_mode).current_option());
    str(id(hp_generation).current_option());
    str(id(cic_feed_url).state);
    str(id(oq_heat_control_mode).current_option());
    str(id(oq_cm_override).current_option());
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    str(id(oq_q_flow_source).current_option());
#endif
    str(id(oq_boiler_connection).current_option());
    f32(id(oq_ph_learning_heat_uncertainty).state);
    f32(id(oq_ph_learning_maximum_flow).state);
    f32(id(oq_ph_learning_junction_tolerance).state);
    f32(id(oq_ph_learning_gain_bound).state);
    f32(id(house_rated_power_w).state);
    f32(id(house_cold_temp_c).state);
    f32(id(house_zero_power_temp_c).state);
    f32(id(ph_kp_w_per_k).state);
    f32(id(ph_comfort_band_below_c).state);
    f32(id(ph_comfort_band_above_c).state);
    f32(id(ph_demand_rise_time_min).state);
    f32(id(ph_demand_fall_time_min).state);
    f32(id(hp1_water_in_temp_offset).state);
    f32(id(hp1_water_out_temp_offset).state);
#if OQ_TOPOLOGY_DUO
    f32(id(hp2_water_in_temp_offset).state);
    f32(id(hp2_water_out_temp_offset).state);
#endif
    state.context_size = writer.ok ? writer.position : 0;
  }

  static size_t slot_offset_(size_t slot) {
    return OpenQuattFlashLayout::HOUSE_LEARNING_OFFSET + slot * OpenQuattFlashLayout::HOUSE_LEARNING_SLOT_SIZE;
  }
  static bool read_slot_(const esp_partition_t* partition, size_t slot, uint8_t* data, size_t size) {
    return partition != nullptr && esp_partition_read(partition, slot_offset_(slot), data, size) == ESP_OK;
  }
  static bool erase_slot_(const esp_partition_t* partition, size_t slot) {
    return partition != nullptr && esp_partition_erase_range(partition, slot_offset_(slot),
                                                             OpenQuattFlashLayout::HOUSE_LEARNING_SLOT_SIZE) == ESP_OK;
  }
  static bool erase_slots_(const esp_partition_t* partition) {
    return partition != nullptr && OpenQuattFlashLayout::HOUSE_LEARNING_END_OFFSET <= partition->size &&
           esp_partition_erase_range(partition, OpenQuattFlashLayout::HOUSE_LEARNING_OFFSET,
                                     2U * OpenQuattFlashLayout::HOUSE_LEARNING_SLOT_SIZE) == ESP_OK;
  }
  static void reset_without_storage_() {
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "openquatt_data");
    const bool cleared = erase_learning_journal(
        [partition]() { return erase_slots_(partition); },
        [partition](size_t slot, uint8_t* data, size_t size) { return read_slot_(partition, slot, data, size); });
    const char* status =
        cleared
            ? R"({"schema":1,"mode":"passive","enabled":false,"storage_ready":false,"status":"paused","journal_status":"cleared","auto_apply_allowed":false})"
            : R"({"schema":1,"mode":"passive","enabled":false,"storage_ready":false,"status":"reset_failed","journal_status":"reset_failed","auto_apply_allowed":false})";
    id(oq_ph_learning_status_endpoint).publish_status_json(status, strlen(status));
    publish_reset_pending_export_();
  }

  static void capture_diagnostics_(RuntimeStorage& state, const SnapshotBuildResult& dynamic, uint32_t epoch,
                                   bool use_previous) {
    const DiagnosticRow previous =
        use_previous && state.row_count > 0
            ? state.rows[(state.row_next + kMaxExportDiagnosticRows - 1U) % kMaxExportDiagnosticRows]
            : DiagnosticRow{};
    auto& row = state.rows[state.row_next];
    row = DiagnosticRow{};
    row.epoch = epoch;
    row.invalid = dynamic.invalid_reasons;
    row.source_generation = state.input.source_cohort_generation;
    row.control_generation = state.input.control_generation;
    row.room = state.input.room_c.valid ? state.input.room_c.value : NAN;
    row.setpoint = state.input.setpoint_c.valid ? state.input.setpoint_c.value : NAN;
    row.outside = state.input.outside_c.valid ? state.input.outside_c.value : NAN;
    const auto diagnostic_heat = diagnostic_signed_heat(state.input, state.config.quality);
    row.heat = diagnostic_heat.valid ? diagnostic_heat.heat_to_water_w : NAN;
    row.heat_valid = diagnostic_heat.valid;
    row.training_qualified = dynamic.measurement_valid;
    row.base = oq_power_house::house_line_power_w(active_line_(), row.outside);
    row.request_known = state.input.operation.active_limit_valid && isfinite(id(oq_strategy_requested_power_w));
    row.request = row.request_known ? id(oq_strategy_requested_power_w) : NAN;
    row.control_mode = id(oq_control_mode_code);
    row.hp1_active = state.input.hp1.compressor_active.value;
    row.hp2_active = state.input.hp2.compressor_active.value;
    row.hp1_active_known = live_measurement_fresh(state.input.hp1.compressor_active, state.input.monotonic_ms);
#if OQ_TOPOLOGY_DUO
    row.hp2_active_known = live_measurement_fresh(state.input.hp2.compressor_active, state.input.monotonic_ms);
#endif
    row.active_limit_known = state.input.operation.active_limit_valid;
    row.active_limit = row.active_limit_known && state.input.operation.active_limit;
    row.boiler_known = live_measurement_fresh(state.input.boiler_heat, state.input.monotonic_ms);
    row.boiler_active = row.boiler_known && state.input.boiler_heat.value == BoilerHeatState::HEAT_ACTIVE;
    const auto protection = [&](const HeatPumpRawMeasurements& hp, bool& known, bool& active) {
      known = live_measurement_fresh(hp.defrost_active, state.input.monotonic_ms) &&
              live_measurement_fresh(hp.valve_transition_active, state.input.monotonic_ms) &&
              live_measurement_fresh(hp.oil_return_active, state.input.monotonic_ms);
      active = known && (hp.defrost_active.value || hp.valve_transition_active.value || hp.oil_return_active.value);
    };
    bool hp1_protection_known = false, hp1_protection_active = false;
    protection(state.input.hp1, hp1_protection_known, hp1_protection_active);
#if OQ_TOPOLOGY_DUO
    bool hp2_protection_known = false, hp2_protection_active = false;
    protection(state.input.hp2, hp2_protection_known, hp2_protection_active);
    row.protection_known = hp1_protection_known && hp2_protection_known;
    row.protection_active = row.protection_known && (hp1_protection_active || hp2_protection_active);
#else
    row.protection_known = hp1_protection_known;
    row.protection_active = row.protection_known && hp1_protection_active;
#endif
    row.coverage_s = diagnostic_coverage_seconds(
        previous.epoch, epoch, previous.heat_valid, row.heat_valid, previous.source_generation, row.source_generation,
        previous.control_generation, row.control_generation, state.config.quality.max_interval_ms);
    state.row_next = (state.row_next + 1U) % kMaxExportDiagnosticRows;
    if (state.row_count < kMaxExportDiagnosticRows) ++state.row_count;
  }

  static const char* route_name_(oq_sources::LearningSourceRoute route) {
    using R = oq_sources::LearningSourceRoute;
    switch (route) {
      case R::OPENTHERM_ROOM:
      case R::OPENTHERM_SETPOINT:
        return "OpenTherm";
      case R::CIC_ROOM:
      case R::CIC_SETPOINT:
      case R::CIC_FLOW:
        return "CIC";
      case R::HP1_FLOW:
      case R::HP1_OUTSIDE:
        return "HP1";
      case R::HP2_FLOW:
      case R::HP2_OUTSIDE:
        return "HP2";
      case R::FLOW_AGGREGATE:
      case R::OUTSIDE_AGGREGATE:
        return "HP1/HP2 composition";
      case R::CONTROLLER_FLOW:
        return "Q flowmeter";
      case R::SYNTHESIZED_ZERO_FLOW:
        return "Synthesized zero";
      default:
        return "Unverified source";
    }
  }
  static void publish_status_error_() {
    static constexpr char ERROR[] =
        R"({"schema":1,"mode":"passive","enabled":false,"storage_ready":true,"status":"serialization_failed","batch_advice_ready":false,"advice_ready":false,"rls_ready":false,"auto_apply_allowed":false,"h_batch":null,"t0_batch":null,"u_rls":null,"c_rls_wh_per_k":null,"invalid_reasons":[],"rls_readiness_reasons":[],"blocked_reasons":["status_serialization_failed"]})";
    id(oq_ph_learning_status_endpoint).publish_status_json(ERROR, sizeof(ERROR) - 1U);
  }

  static void publish_export_error_() {
    static constexpr char ERROR[] =
        R"({"schema":1,"mode":"passive","status":"serialization_failed","auto_apply_allowed":false,"record_columns":[],"records":[],"diagnostic_columns":[],"diagnostics":[]})";
    id(oq_ph_learning_status_endpoint).publish_export_json(ERROR, sizeof(ERROR) - 1U);
  }

  static void publish_status_(RuntimeStorage& state, bool enabled, uint32_t epoch) {
    auto& summary = state.summary;
    JsonWriter json(state.json, sizeof(state.json));
    struct ReasonName {
      uint32_t mask;
      const char* name;
    };
    static constexpr ReasonName INVALID_REASON_NAMES[]{
        {INVALID_ESSENTIAL_SOURCE, "essential_source"},
        {INVALID_SOURCE_STALE, "source_stale"},
        {INVALID_NOT_HEATING, "not_heating"},
        {INVALID_BOILER_HEAT, "boiler_heat"},
        {INVALID_DEFROST_OR_OIL_RETURN, "defrost_or_oil_return"},
        {INVALID_CONTROL_MODE, "control_mode"},
        {INVALID_ACTIVE_LIMIT, "active_limit"},
        {INVALID_SERVICE_OR_OTA, "service_or_ota"},
        {INVALID_COOLING, "cooling"},
        {INVALID_SOURCE_UNCERTAIN, "source_uncertain"},
        {INVALID_SETPOINT_RECOVERY, "setpoint_recovery"},
    };
    static constexpr ReasonName RLS_REASON_NAMES[]{
        {THERMAL_READY_NOT_ENOUGH_SAMPLES, "not_enough_samples"},
        {THERMAL_READY_OUTSIDE_SPAN, "outside_span"},
        {THERMAL_READY_HEAT_SPAN, "heat_span"},
        {THERMAL_READY_UNOBSERVABLE, "unobservable"},
        {THERMAL_READY_PARAMETER_BOUNDS, "parameter_bounds"},
        {THERMAL_READY_RESIDUAL_RMS, "residual_rms"},
        {THERMAL_READY_RESIDUAL_BIAS, "residual_bias"},
        {THERMAL_READY_UNMODELED_GAINS, "unmodeled_gains"},
        {THERMAL_READY_INVALID_STATE, "invalid_state"},
        {THERMAL_READY_RECENT_DATA_INVALID, "recent_data_invalid"},
        {THERMAL_READY_STALE_MODEL, "stale_model"},
    };
    const auto write_reasons = [&json](uint32_t mask, const ReasonName* names, size_t count) {
      json.add("[");
      bool comma = false;
      for (size_t index = 0; index < count; ++index) {
        if ((mask & names[index].mask) == 0U) continue;
        json.add("%s\"%s\"", comma ? "," : "", names[index].name);
        comma = true;
      }
      json.add("]");
    };
    uint32_t last_sample_epoch = 0U;
    if (state.learner.record_count > 0U)
      last_sample_epoch = state.learner.records[state.learner.record_count - 1U].end_epoch_s;
    if (state.learner.batch_accumulator.active && state.learner.batch_accumulator.last_measurement_valid &&
        state.learner.batch_accumulator.last_epoch_s > last_sample_epoch)
      last_sample_epoch = state.learner.batch_accumulator.last_epoch_s;
    if (state.learner.thermal_accumulator.active && state.learner.thermal_accumulator.last.epoch_s > last_sample_epoch)
      last_sample_epoch = state.learner.thermal_accumulator.last.epoch_s;
    const uint32_t invalid_reasons = state.source_diagnostics.invalid_reasons;
    const uint32_t rls_reasons = summary.thermal.readiness_reasons;
    json.add("{\"schema\":1,\"build\":\"" __DATE__ " " __TIME__
             " ph-passive-1\",\"mode\":\"passive\",\"enabled\":%s,"
             "\"storage_ready\":true,"
             "\"status\":\"%s\",\"source_status\":\"%s\",\"invalid_reasons\":",
             enabled ? "true" : "false",
             enabled ? (summary.current_observation_valid ? "collecting" : "blocked") : "paused",
             snapshot_source_status_name(state.source_diagnostics.status));
    write_reasons(invalid_reasons, INVALID_REASON_NAMES,
                  sizeof(INVALID_REASON_NAMES) / sizeof(INVALID_REASON_NAMES[0]));
    json.add(
        ",\"invalid_reasons_mask\":%u,\"records\":%u,\"batch_status\":\"%s\",\"batch_advice_ready\":%s,"
        "\"advice_ready\":%s,\"auto_apply_allowed\":false,\"h_batch\":",
        invalid_reasons, static_cast<unsigned>(summary.record_count), learning_status_name(summary.batch.status),
        enabled && summary.batch_advice_ready ? "true" : "false",
        enabled && summary.cross_validated_advice_ready ? "true" : "false");
    json.number(summary.batch.candidate_available ? summary.batch.candidate.heat_loss_w_per_k : NAN);
    json.add(",\"t0_batch\":");
    json.number(summary.batch.candidate_available ? summary.batch.candidate.zero_power_temp_c : NAN);
    json.add(",\"u_rls\":");
    json.number(summary.thermal.accepted_samples > 0 ? summary.thermal.heat_loss_w_per_k : NAN);
    json.add(",\"c_rls_wh_per_k\":");
    json.number(summary.thermal.accepted_samples > 0 ? summary.thermal.thermal_capacity_wh_per_k : NAN);
    json.add(",\"rls_samples\":%u,\"rls_ready\":%s,\"rls_readiness_reasons\":", summary.thermal.accepted_samples,
             enabled && summary.thermal_model_ready ? "true" : "false");
    write_reasons(rls_reasons, RLS_REASON_NAMES, sizeof(RLS_REASON_NAMES) / sizeof(RLS_REASON_NAMES[0]));
    json.add(",\"rls_readiness_reasons_mask\":%u,\"tick_epoch\":%u,\"last_sample_epoch\":", rls_reasons, epoch);
    if (last_sample_epoch == 0U)
      json.add("null");
    else
      json.add("%u", last_sample_epoch);
    json.add(
        ",\"source_generation\":%u,\"physical_generation\":%u,\"control_generation\":%u,\"journal_status\":\"%s\","
        "\"model_validation_status\":\"%s\",\"sources\":{",
        state.input.source_cohort_generation, state.input.physical_context_generation, state.input.control_generation,
        state.journal.status, model_validation_status_name(summary.validation.status));
    const char* names[]{"room", "setpoint", "outside", "flow"};
    const bool valid[]{state.input.room_c.valid, state.input.setpoint_c.valid, state.input.outside_c.valid,
                       state.input.flow_lph.valid};
    for (size_t i = 0; i < 4; ++i)
      json.add("%s\"%s\":{\"route\":\"%s\",\"valid\":%s}", i ? "," : "", names[i], route_name_(state.sources[i].route),
               valid[i] ? "true" : "false");
    json.add("},\"blocked_reasons\":[");
    bool comma = false;
    const auto reason = [&](bool blocked, const char* text) {
      if (blocked) {
        json.add("%s\"%s\"", comma ? "," : "", text);
        comma = true;
      }
    };
    reason(!state.input.calorimetry.uncertainty_proven, "calorimetry_not_verified");
    reason(!state.input.boiler_heat.valid, "external_heat_not_excluded");
    reason(!state.config.thermal_window.unmodeled_gain_bound_valid, "unmodeled_gain_bound_unknown");
    reason(!state.journal.available, "persistence_unavailable");
    if (summary.batch_advice_ready && summary.thermal_model_ready && !summary.cross_validated_advice_ready) {
      const auto validation_status = summary.validation.status;
      reason(validation_status == ModelValidationStatus::CONTEXT_MISMATCH, "model_context_mismatch");
      reason(validation_status == ModelValidationStatus::INSUFFICIENT_SHARED_RANGE,
             "insufficient_shared_temperature_range");
      reason(validation_status == ModelValidationStatus::MODEL_DISAGREEMENT, "model_disagreement");
      reason(validation_status == ModelValidationStatus::THERMAL_STORAGE_ACTIVE, "thermal_storage_not_stationary");
    }
    json.add(
        "],\"memory\":{\"internal_free\":%u,\"internal_min\":%u,\"internal_largest_block\":%u,\"loop_stack_high_water_"
        "bytes\":%u,\"runtime_psram_bytes\":%u,\"max_tick_us\":%llu}}",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned>(sizeof(RuntimeStorage)),
        static_cast<unsigned long long>(state.max_tick_us));
    const size_t length = json.size();
    if (length == 0U || !id(oq_ph_learning_status_endpoint).publish_status_json(state.json, length)) {
      publish_status_error_();
    }
  }

  static void publish_paused_status_(RuntimeStorage& state) {
    state.summary = passive_runtime_summary(state.learner, oq_sources::monotonic_ms());
    const auto clock = id(oq_time).now();
    publish_status_(state, false, clock.is_valid() ? clock.timestamp : 0U);
  }

  static void publish_reset_pending_export_() {
    static constexpr char RESET_PENDING[] =
        R"({"schema":1,"mode":"passive","status":"reset_pending","auto_apply_allowed":false,"record_columns":[],"records":[],"diagnostic_columns":[],"diagnostics":[]})";
    id(oq_ph_learning_status_endpoint).publish_export_json(RESET_PENDING, sizeof(RESET_PENDING) - 1U);
  }

  static void publish_(RuntimeStorage& state, bool enabled, uint32_t epoch) {
    publish_status_(state, enabled, epoch);
    publish_export_(state);
  }

  static void publish_export_(RuntimeStorage& state) {
    JsonWriter json(state.json, sizeof(state.json));
    json.add("%s", kExportRecordsPrefix);
    for (size_t i = 0; i < state.learner.record_count; ++i) {
      const auto& r = state.learner.records[i];
      json.add("%s[%u,%u,", i ? "," : "", r.start_epoch_s, r.end_epoch_s);
      const float values[]{r.mean_room_c, r.mean_setpoint_c,         r.mean_outside_c,
                           r.mean_heat_w, r.mean_heat_uncertainty_w, r.room_trend_k_per_h};
      for (float v : values) {
        json.number(v);
        json.add(",");
      }
      json.add("%u,%u,%u]", r.source_generation, r.physical_context_generation, r.control_generation);
    }
    json.add("%s", kExportDiagnosticsPrefix);
    for (size_t i = 0; i < state.row_count; ++i) {
      const auto& r =
          state.rows[(state.row_next + kMaxExportDiagnosticRows - state.row_count + i) % kMaxExportDiagnosticRows];
      json.add("%s[%u,%u,%d,", i ? "," : "", r.epoch, r.invalid, r.control_mode);
      const float values[]{r.room, r.setpoint, r.outside, r.heat};
      for (float v : values) {
        json.number(v);
        json.add(",");
      }
      json.add("%s,%s,", r.heat_valid ? "true" : "false", r.training_qualified ? "true" : "false");
      json.number(r.base);
      json.add(",");
      json.number(r.request);
      json.add(",%s,", r.request_known ? "true" : "false");
      json.number(r.heat - r.base);
      json.add(",");
      json.number(r.request - r.heat);
      json.add(",%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%u]", r.coverage_s, r.hp1_active ? "true" : "false",
               r.hp1_active_known ? "true" : "false", r.hp2_active ? "true" : "false",
               r.hp2_active_known ? "true" : "false", r.active_limit ? "true" : "false",
               r.active_limit_known ? "true" : "false", r.boiler_active ? "true" : "false",
               r.boiler_known ? "true" : "false", r.protection_active ? "true" : "false",
               r.protection_known ? "true" : "false", r.source_generation, r.control_generation);
    }
    json.add("%s", kExportSuffix);
    const size_t length = json.size();
    if (length == 0U || !id(oq_ph_learning_status_endpoint).publish_export_json(state.json, length)) {
      publish_export_error_();
    }
  }
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_ph_learning

#endif  // ESP_PLATFORM && OQ_PH_LEARNING_CORE_AVAILABLE
