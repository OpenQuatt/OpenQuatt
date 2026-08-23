#include <array>
#include <cassert>
#include <cstring>
#include <string>

#include "components/openquatt_usage_telemetry/OpenQuattUsageTelemetryPolicy.h"

using esphome::openquatt_usage_telemetry::append_json_escaped;
using esphome::openquatt_usage_telemetry::configured_source_wire_value;
using esphome::openquatt_usage_telemetry::data_publish_allowed;
using esphome::openquatt_usage_telemetry::FixedBufferWriter;
using esphome::openquatt_usage_telemetry::flow_source_config_wire_value;
using esphome::openquatt_usage_telemetry::heating_strategy_wire_value;
using esphome::openquatt_usage_telemetry::mqtt_cleanup_decision;
using esphome::openquatt_usage_telemetry::mqtt_publish_policy;
using esphome::openquatt_usage_telemetry::MqttCleanupDecision;
using esphome::openquatt_usage_telemetry::PublishKind;
using esphome::openquatt_usage_telemetry::quatt_hybrid_generation_wire_value;
using esphome::openquatt_usage_telemetry::retained_cleanup_can_complete;
using esphome::openquatt_usage_telemetry::retained_cleanup_ready_for_tombstone;
using esphome::openquatt_usage_telemetry::retained_cleanup_recoverable_after_reboot;
using esphome::openquatt_usage_telemetry::retained_crash_cleanup_required_on_disabled_boot;

int main() {
  const auto telemetry_policy = mqtt_publish_policy(PublishKind::TELEMETRY);
  assert(telemetry_policy.qos == 1);
  assert(telemetry_policy.retain == 0);
  assert(!telemetry_policy.empty_payload);

  const auto crash_policy = mqtt_publish_policy(PublishKind::CRASH);
  assert(crash_policy.qos == 1);
  assert(crash_policy.retain == 1);
  assert(!crash_policy.empty_payload);

  const auto tombstone_policy = mqtt_publish_policy(PublishKind::TOMBSTONE);
  assert(tombstone_policy.qos == 1);
  assert(tombstone_policy.retain == 1);
  assert(tombstone_policy.empty_payload);

  assert(data_publish_allowed(PublishKind::TELEMETRY, true, true, false, false));
  assert(data_publish_allowed(PublishKind::CRASH, true, true, false, false));
  assert(!data_publish_allowed(PublishKind::TELEMETRY, false, true, false, false));
  assert(!data_publish_allowed(PublishKind::CRASH, true, false, false, false));
  assert(!data_publish_allowed(PublishKind::CRASH, true, true, true, false));
  assert(!data_publish_allowed(PublishKind::CRASH, true, true, false, true));
  assert(data_publish_allowed(PublishKind::TOMBSTONE, false, false, true, true));
  assert(!data_publish_allowed(PublishKind::TOMBSTONE, true, true, false, false));

  assert(retained_cleanup_recoverable_after_reboot(true, false));
  assert(retained_cleanup_recoverable_after_reboot(false, true));
  assert(retained_cleanup_recoverable_after_reboot(true, true));
  assert(!retained_cleanup_recoverable_after_reboot(false, false));

  assert(retained_cleanup_ready_for_tombstone(true, true));
  assert(!retained_cleanup_ready_for_tombstone(true, false));
  assert(retained_cleanup_ready_for_tombstone(false, true));
  assert(!retained_cleanup_ready_for_tombstone(false, false));

  assert(retained_crash_cleanup_required_on_disabled_boot(false, true, true));
  assert(!retained_crash_cleanup_required_on_disabled_boot(true, true, true));
  assert(!retained_crash_cleanup_required_on_disabled_boot(false, false, true));
  assert(!retained_crash_cleanup_required_on_disabled_boot(false, true, false));

  assert(retained_cleanup_can_complete(true, true, true, true));
  assert(!retained_cleanup_can_complete(false, true, true, true));
  assert(!retained_cleanup_can_complete(true, false, true, true));
  assert(!retained_cleanup_can_complete(true, true, false, true));
  assert(!retained_cleanup_can_complete(true, true, true, false));

  assert(mqtt_cleanup_decision(true, false, false, 0U) == MqttCleanupDecision::DESTROY);
  assert(mqtt_cleanup_decision(false, true, false, 1U) == MqttCleanupDecision::FORCE_DISCONNECT);
  assert(mqtt_cleanup_decision(false, false, false, 1U) == MqttCleanupDecision::RETRY_STOP);
  assert(mqtt_cleanup_decision(false, false, false, 2U) == MqttCleanupDecision::DESTROY_ALREADY_STOPPED);
  assert(mqtt_cleanup_decision(false, true, true, 2U) == MqttCleanupDecision::DESTROY_ALREADY_STOPPED);

  assert(std::strcmp(quatt_hybrid_generation_wire_value("V1"), "v1") == 0);
  assert(std::strcmp(quatt_hybrid_generation_wire_value("V1.5"), "v1_5") == 0);
  assert(std::strcmp(quatt_hybrid_generation_wire_value("V2"), "v2") == 0);
  assert(quatt_hybrid_generation_wire_value("unknown") == nullptr);

  assert(std::strcmp(heating_strategy_wire_value("Power House"), "power_house") == 0);
  assert(std::strcmp(heating_strategy_wire_value("Water Temperature Control (heating curve)"), "heating_curve") == 0);
  assert(heating_strategy_wire_value("unknown") == nullptr);

  assert(std::strcmp(configured_source_wire_value("Auto"), "auto") == 0);
  assert(std::strcmp(configured_source_wire_value("Local"), "local") == 0);
  assert(std::strcmp(configured_source_wire_value("Outdoor unit"), "outdoor_unit") == 0);
  assert(std::strcmp(configured_source_wire_value("CIC"), "cic") == 0);
  assert(std::strcmp(configured_source_wire_value("OT thermostat"), "opentherm") == 0);
  assert(std::strcmp(configured_source_wire_value("HA input"), "home_assistant") == 0);
  assert(std::strcmp(configured_source_wire_value("Home Assistant"), "home_assistant") == 0);
  assert(std::strcmp(configured_source_wire_value("API input"), "api_input") == 0);
  assert(std::strcmp(configured_source_wire_value("MQTT"), "mqtt") == 0);
  assert(std::strcmp(configured_source_wire_value("CIC or HA input"), "cic_or_home_assistant") == 0);
  assert(std::strcmp(configured_source_wire_value("Disabled"), "disabled") == 0);
  assert(configured_source_wire_value("unknown") == nullptr);

  assert(std::strcmp(flow_source_config_wire_value("CIC", false, ""), "cic") == 0);
  assert(std::strcmp(flow_source_config_wire_value("Outdoor unit", false, ""), "outdoor_unit") == 0);
  assert(std::strcmp(flow_source_config_wire_value("Outdoor unit", true, "Local"), "controller_local") == 0);
  assert(std::strcmp(flow_source_config_wire_value("Outdoor unit", true, "Auto"), "outdoor_unit") == 0);
  assert(std::strcmp(flow_source_config_wire_value("Outdoor unit", true, "Outdoor unit"), "outdoor_unit") == 0);
  assert(flow_source_config_wire_value("Outdoor unit", true, "") == nullptr);
  assert(flow_source_config_wire_value("unknown", false, "") == nullptr);

  std::array<char, 128U> escaped{};
  FixedBufferWriter json(escaped.data(), escaped.size());
  json += R"({"value":")";
  append_json_escaped(json, std::string{"\"\\\b\f\n\r\t"} + '\x01' + "x");
  json += R"("})";
  assert(json.ok());
  assert(std::strcmp(escaped.data(), R"({"value":"\"\\\b\f\n\r\t\u0001x"})") == 0);

  std::array<char, 5U> exact{};
  FixedBufferWriter exact_writer(exact.data(), exact.size());
  exact_writer += "1234";
  assert(exact_writer.ok());
  assert(exact_writer.size() == 4U);
  assert(std::strcmp(exact.data(), "1234") == 0);
  exact_writer += '5';
  assert(!exact_writer.ok());
  assert(std::strcmp(exact.data(), "1234") == 0);

  std::array<char, 2049U> maximum{};
  FixedBufferWriter maximum_writer(maximum.data(), maximum.size());
  maximum_writer += std::string(2048U, 'x');
  assert(maximum_writer.ok());
  assert(maximum_writer.size() == 2048U);
  maximum_writer += 'y';
  assert(!maximum_writer.ok());
  return 0;
}
