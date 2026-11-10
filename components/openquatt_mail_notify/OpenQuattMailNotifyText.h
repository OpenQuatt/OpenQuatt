#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "OpenQuattMailNotifyPolicy.h"
#include "includes/incidents/oq_hp_incident_catalog.h"

// Dutch mail copy. Incident titles follow the web app's nl.js incident labels.
namespace oq_mail {

struct IncidentTitle {
  const char* key;
  const char* title;
};

constexpr IncidentTitle kIncidentTitles[] = {
    {"main_line_current", "Netstroombeveiliging"},
    {"compressor_phase_current", "Compressorfasestroom"},
    {"ipm_module", "IPM-vermogensmodule"},
    {"compressor_oil_return", "Olie-retour actief"},
    {"high_pressure_switch", "Hogedrukbeveiliging"},
    {"high_pressure_speed_limit", "Toerental begrensd door hoge druk"},
    {"first_start_preheat", "Eerste-startvoorverwarming"},
    {"gas_discharge_temperature", "Persgastemperatuur te hoog"},
    {"evaporator_coil_temperature", "Verdampertemperatuur buiten bereik"},
    {"ac_voltage", "Netspanning buiten bereik"},
    {"ambient_temperature_range", "Buitentemperatuur buiten werkgebied"},
    {"ambient_temperature_frequency_limit", "Vermogen begrensd door buitentemperatuur"},
    {"low_pressure_switch", "Lagedrukbeveiliging"},
    {"low_pressure_speed_limit", "Toerental begrensd door lage druk"},
    {"ambient_temperature_sensor", "Buitentemperatuursensor"},
    {"evaporator_coil_temperature_sensor", "Verdampertemperatuursensor"},
    {"gas_discharge_temperature_sensor", "Persgastemperatuursensor"},
    {"gas_return_temperature_sensor", "Zuiggastemperatuursensor"},
    {"evaporator_pressure_sensor_lock", "Verdamperdruksensor vergrendeld"},
    {"condenser_pressure_sensor", "Condensordruksensor"},
    {"high_pressure_switch_lock", "Hogedrukbeveiliging vergrendeld"},
    {"low_pressure_switch_lock", "Lagedrukbeveiliging vergrendeld"},
    {"fan", "Ventilatorstoring"},
    {"evaporating_pressure_lock", "Verdamperdruk vergrendeld"},
    {"condenser_pressure_lock", "Condensordruk vergrendeld"},
    {"evi_pressure_sensor", "EVI-druksensor"},
    {"evi_inlet_temperature_sensor", "EVI-inlaattemperatuursensor"},
    {"evi_outlet_temperature_sensor", "EVI-uitlaattemperatuursensor"},
    {"odu_master_slave_communication", "Communicatie tussen buitenunits"},
    {"odu_control_pcb_communication", "Communicatie met ODU-regelprint"},
    {"compressor_phase_current_failure", "Compressorfasestroomstoring"},
    {"compressor_phase_current_overload", "Compressorfasestroom overbelast"},
    {"compressor_driver", "Compressordriver"},
    {"module_vdc_voltage", "DC-tussenkringspanning"},
    {"ac_current", "AC-stroommeting"},
    {"eeprom", "ODU-geheugen"},
    {"fan_drive_pcb", "Ventilatorregelprint"},
    {"inlet_water_temperature_sensor", "Inlaatwatertemperatuursensor"},
    {"outlet_water_temperature_sensor", "Uitlaatwatertemperatuursensor"},
    {"inner_coil_temperature_sensor", "Binnenste-wisselaartemperatuursensor"},
    {"dc_water_pump", "Waterpomp in buitenunit"},
    {"unclassified_odu_fault", "Onbekende ODU-melding"},
};

inline const char* incident_title(int16_t incident_id, uint8_t reason_code) {
  switch (reason_code) {
    case reason::HP_LINK_LOSS:
      return "Verbinding met warmtepomp weg";
    case reason::HP_START_FAILED:
      return "Warmtepompstart niet bevestigd";
    case reason::HP_STOP_UNCONFIRMED:
      return "Warmtepompstop niet bevestigd";
    case reason::HP_PERSISTENCE_FAILURE:
      return "Opslag van handmatige resetstatus mislukt";
    default:
      break;
  }
  if (incident_id == static_cast<int16_t>(oq_incidents::kRuntimeFrequencyMappingIncidentId)) {
    return "ODU-frequentietabel moet opnieuw worden gevalideerd";
  }
  if (incident_id > 0 && incident_id <= static_cast<int16_t>(oq_incidents::kRawIncidentSlotCount)) {
    const oq_incidents::IncidentDefinition definition =
        oq_incidents::definition_for_id(static_cast<oq_incidents::IncidentId>(incident_id));
    for (const IncidentTitle& entry : kIncidentTitles) {
      if (std::strcmp(entry.key, definition.key) == 0) return entry.title;
    }
  }
  return "Onbekende ODU-melding";
}

inline const char* hp_label(uint8_t subject_code) {
  switch (subject_code) {
    case subject::HP1:
      return "warmtepomp 1";
    case subject::HP2:
      return "warmtepomp 2";
    case subject::BOTH:
      return "beide warmtepompen";
    default:
      return "de warmtepomp";
  }
}

// esp_reset_reason_t values relevant for an unexpected restart.
namespace reset_cause {
constexpr int16_t PANIC = 1;
constexpr int16_t WATCHDOG = 2;
constexpr int16_t BROWNOUT = 3;
}  // namespace reset_cause

inline bool append_duration(Buffer& out, uint16_t seconds) {
  if (seconds < 60U) return out.appendf("%u s", static_cast<unsigned>(seconds));
  const unsigned minutes = seconds / 60U;
  if (minutes < 60U) return out.appendf("%u min", minutes);
  return out.appendf("%u u %u min", minutes / 60U, minutes % 60U);
}

// Short title: used as the subject of a single-notice mail and as line prefix.
inline bool append_short_title(Buffer& out, const Notice& notice) {
  const char* hp = hp_label(notice.subject);
  switch (static_cast<Kind>(notice.kind)) {
    case Kind::HP_FAULT:
      out.append(notice.recovery ? "Storing opgelost bij " : "Storing bij ");
      return out.append(hp);
    case Kind::HP_WARNING:
      out.append(notice.recovery ? "Beveiliging voorbij bij " : "Beveiliging actief bij ");
      return out.append(hp);
    case Kind::HP_LINK_LOSS:
      out.append(notice.recovery ? "Verbinding hersteld met " : "Geen verbinding met ");
      return out.append(hp);
    case Kind::HP_START_STOP:
      if (notice.recovery) {
        out.append("Start/stop weer bevestigd bij ");
      } else {
        out.append(notice.reason_code == reason::HP_STOP_UNCONFIRMED ? "Stop niet bevestigd bij "
                                                                     : "Start niet bevestigd bij ");
      }
      return out.append(hp);
    case Kind::BOILER_FALLBACK:
      return out.append(notice.recovery ? "Ketel stopt met overnemen" : "Ketel neemt verwarming over");
    case Kind::FROST_PROTECTION:
      return out.append(notice.recovery ? "Vorstbeveiliging beëindigd" : "Vorstbeveiliging actief");
    case Kind::COMPRESSOR_CYCLING:
      return out.append("Veel compressorstarts");
    case Kind::CONTROLLER_RESTART:
      return out.append("Controller onverwacht herstart");
  }
  return out.append("Melding");
}

inline bool append_detail(Buffer& out, const Notice& notice) {
  switch (static_cast<Kind>(notice.kind)) {
    case Kind::HP_FAULT:
    case Kind::HP_WARNING:
      out.append(": ");
      out.append(incident_title(notice.value_a, notice.reason_code));
      if (notice.recovery && notice.duration_s > 0U) {
        out.append(" (duurde ");
        append_duration(out, notice.duration_s);
        out.append_char(')');
      }
      out.append_char('.');
      if (!notice.recovery && (notice.flags & flag::MANUAL_RESET_REQUIRED) != 0U) {
        out.append(" Herstel vraagt een bevestigde powercycle van de buitenunit; zie de web-app.");
      }
      return out.ok();
    case Kind::HP_LINK_LOSS:
    case Kind::HP_START_STOP:
      if (notice.recovery && notice.duration_s > 0U) {
        out.append(" (duurde ");
        append_duration(out, notice.duration_s);
        out.append_char(')');
      }
      return out.append_char('.');
    case Kind::BOILER_FALLBACK:
      return out.append(notice.recovery ? "." : ": de warmtepomp is niet beschikbaar.");
    case Kind::FROST_PROTECTION:
      if (notice.recovery && notice.duration_s > 0U) {
        out.append(" (duurde ");
        append_duration(out, notice.duration_s);
        out.append_char(')');
      }
      return out.append_char('.');
    case Kind::COMPRESSOR_CYCLING:
      if (notice.subject == subject::HP2) {
        return out.appendf(": warmtepomp 2 %d starts (grens %d).", notice.value_b, notice.threshold_a);
      }
      if (notice.subject == subject::HP1) {
        return out.appendf(": warmtepomp 1 %d starts (grens %d).", notice.value_a, notice.threshold_a);
      }
      return out.appendf(": warmtepomp 1 %d en warmtepomp 2 %d starts (grens %d).", notice.value_a, notice.value_b,
                         notice.threshold_a);
    case Kind::CONTROLLER_RESTART:
      switch (notice.value_a) {
        case reset_cause::PANIC:
          return out.append(": de firmware is gecrasht.");
        case reset_cause::WATCHDOG:
          return out.append(": een watchdog greep in.");
        case reset_cause::BROWNOUT:
          return out.append(": de voedingsspanning zakte te ver weg.");
        default:
          return out.append_char('.');
      }
  }
  return out.append_char('.');
}

// Removes control characters from a device name before it enters a header.
inline void append_header_text(Buffer& out, const char* text) {
  for (const char* cursor = text == nullptr ? "" : text; *cursor != '\0'; ++cursor) {
    const auto c = static_cast<uint8_t>(*cursor);
    out.append_char(c < 0x20U || c == 0x7FU ? ' ' : static_cast<char>(c));
  }
}

inline bool append_subject(Buffer& out, const char* device_name, const Notice* notices, size_t count, size_t overflow) {
  out.append("OpenQuatt ");
  append_header_text(out, device_name);
  out.append(": ");
  if (count == 1 && overflow == 0) return append_short_title(out, notices[0]);
  return out.appendf("%u meldingen", static_cast<unsigned>(count + overflow));
}

inline bool append_footer(Buffer& out, const char* web_url) {
  if (web_url != nullptr && web_url[0] != '\0') {
    out.append("\nWeb-app: ");
    out.append(web_url);
    out.append_char('\n');
  }
  return out.append(
      "\nJe ontvangt deze mail omdat e-mailmeldingen aan staan in OpenQuatt "
      "(Instellingen > Bronnen / integraties > E-mailmeldingen).\n");
}

// format_time(epoch_s, buffer, size) writes a local timestamp or returns false.
template <typename FormatTime>
inline bool append_body(Buffer& out, const char* device_name, const char* web_url, const Notice* notices, size_t count,
                        size_t overflow, FormatTime format_time) {
  out.append("Melding van OpenQuatt \"");
  append_header_text(out, device_name);
  out.append("\":\n\n");
  for (size_t index = 0; index < count; ++index) {
    char time_text[24];
    if (notices[index].epoch_s != 0U && format_time(notices[index].epoch_s, time_text, sizeof(time_text))) {
      out.append(time_text);
    } else {
      out.append("tijd onbekend");
    }
    out.append("  ");
    append_short_title(out, notices[index]);
    append_detail(out, notices[index]);
    out.append_char('\n');
  }
  if (overflow > 0) {
    out.appendf("... en nog %u meldingen. De beslislog in de web-app toont alles.\n", static_cast<unsigned>(overflow));
  }
  return append_footer(out, web_url);
}

inline bool append_test_subject(Buffer& out, const char* device_name) {
  out.append("OpenQuatt ");
  append_header_text(out, device_name);
  return out.append(": testmail");
}

inline bool append_test_body(Buffer& out, const char* device_name, const char* web_url) {
  out.append("Dit is een testmail van OpenQuatt \"");
  append_header_text(out, device_name);
  out.append("\". De e-mailinstellingen werken.\n");
  return append_footer(out, web_url);
}

}  // namespace oq_mail
