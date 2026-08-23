#include "openquatt/includes/incidents/oq_hp_incident_engine.h"
#include "openquatt/includes/incidents/oq_hp_incident_sources.h"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace oq_incidents;

int main() {
  assert(std::strcmp(source_description_for(2121U, 13U), "DC water pump failure") == 0);
  assert(std::strcmp(source_description_for(2120U, 9U), "DC fan B failure") == 0);
  assert(std::strcmp(source_description_for(2119U, 5U),
                     "Compressor speed down caused by too high pressure detected by pressure sensor") == 0);
  assert(source_description_for(2120U, 12U)[0] == '\0');
  assert(source_description_for(2122U, 0U)[0] == '\0');

  // The source mapping is presentation-only: an unclassified bit keeps the
  // existing conservative incident policy.
  const IncidentDefinition fan_b = definition_for(2120U, 9U);
  assert(fan_b.documentation_confidence == DocumentationConfidence::REVIEW_REQUIRED);
  assert(fan_b.category == IncidentCategory::FAULT);
  assert(has_effect(fan_b.effects, IncidentEffect::BLOCK_START));
  assert(!has_effect(fan_b.effects, IncidentEffect::STOP_COMPRESSOR));

  std::cout << "HP incident source tests passed\n";
  return 0;
}
