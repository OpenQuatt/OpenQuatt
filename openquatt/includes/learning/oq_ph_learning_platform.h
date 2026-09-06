#pragma once

// ESPHome includes the entire runtime header directory. Keep the passive core
// absent from Listener/unknown firmware even when none of its functions is used.
// This compile target flag is not a learner capability, opt-in or apply consent.
#if !defined(ESP_PLATFORM) || (defined(OQ_POWER_HOUSE_LEARNING_TARGET) && OQ_POWER_HOUSE_LEARNING_TARGET == 1)
#define OQ_PH_LEARNING_CORE_AVAILABLE 1
#else
#define OQ_PH_LEARNING_CORE_AVAILABLE 0
#endif
