#pragma once

#include <array>
#include <cstdint>

namespace oq_incidents {

inline constexpr std::array<std::array<const char*, 16U>, 3U> kHpIncidentSourceDescriptions{{
    {{"Main line current protection", "Compressor phase current protection", "IPM module protection",
      "Compressor oil return protection", "High pressure switch protection",
      "Compressor speed down caused by too high pressure detected by pressure sensor", "1st start pre-heat protection",
      "Outdoor gas discharge temp. sensor protection", "Outdoor evaporator coil temp. sensor protection",
      "AC high/low voltage protection", "Failure caused by ambient temperature",
      "Frequency limit protection by ambient temperature", "Low pressure switch protection",
      "Compressor speed down caused by too low pressure detected by low pressure sensor", "", ""}},
    {{"Outdoor ambient temp. sensor failure", "Outdoor evaporator coil temp. sensor failure",
      "Outdoor gas discharge temp. sensor failure", "Outdoor gas return temp. sensor failure",
      "Evaporator pressure sensor failure", "Condenser pressure sensor failure", "High pressure switch lock protection",
      "Low pressure switch lock protection", "DC fan A failure", "DC fan B failure",
      "Evaporating pressure lock protection", "Condenser pressure lock protection", "", "EVI pressure sensor failure",
      "EVI inlet temp. sensor failure", "EVI outlet temp. sensor failure"}},
    {{"Master and slave communication failure", "Outdoor main control PCB and module communication failure",
      "Compressor phase current failure (open/short circuit)", "Compressor phase current overload (over current)",
      "Compressor driver failure", "Module VDC over high/low voltage failure", "AC current failure", "EEPROM failure",
      "Fan drive PCB failure", "Inlet water temp. sensor failure", "Outlet water temp. sensor failure",
      "Inner coil temp. sensor failure", "", "DC water pump failure", "", ""}},
}};

constexpr const char* source_description_for(uint16_t register_address, uint8_t bit) {
  if (register_address < 2119U || register_address > 2121U || bit >= 16U) return "";
  return kHpIncidentSourceDescriptions[register_address - 2119U][bit];
}

}  // namespace oq_incidents
