"""Execute production restart snapshots, image classification and OTA callbacks."""
from pathlib import Path
import unittest

from test_manual_hp_start_runtime import compile_run
from test_restart_handoff_boot_contract import function_body

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/openquatt_incident_manager"
HANDOFF = (COMPONENT / "OpenQuattRestartHandoff.cpp").read_text()
MANAGER = (COMPONENT / "OpenQuattIncidentManager.cpp").read_text()


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    return source[start:opening + 1] + function_body(source, signature) + "}\n"


def fixture():
    header = (COMPONENT / "OpenQuattRestartHandoff.h").read_text()
    header = header.replace('#pragma once', '')
    header = header.replace('#include "esphome/core/component.h"', '')
    header = header.replace('#include "esphome/components/ota/ota_backend.h"', '')
    header = header.replace('#include "OpenQuattRestartHandoffPolicy.h"',
                            '#include "components/openquatt_incident_manager/OpenQuattRestartHandoffPolicy.h"')
    prefix = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#define USE_OTA_STATE_LISTENER
template<typename... Args> void log_stub(const Args&...) {}
#define ESP_LOGI(...) log_stub(__VA_ARGS__)
#define ESP_LOGW(...) log_stub(__VA_ARGS__)
#define ESP_LOGE(...) log_stub(__VA_ARGS__)
namespace esphome {
class Component { public: virtual ~Component() = default; virtual void setup() {} virtual void loop() {} };
namespace ota {
class OTAComponent {};
enum OTAState { OTA_COMPLETED, OTA_STARTED, OTA_IN_PROGRESS, OTA_ABORT, OTA_ERROR };
class OTAGlobalStateListener {
 public: virtual ~OTAGlobalStateListener() = default;
 virtual void on_ota_global_state(OTAState, float, uint8_t, OTAComponent*) = 0;
};
}
}
namespace oq_incidents {
enum class LinkState { HEALTHY, RECOVERING };
enum class RunState { RUNNING, STOPPED };
}
uint32_t now_ms = 1000;
uint32_t millis() { return now_ms; }
struct AppStub { unsigned reboots{}; void safe_reboot() { ++reboots; } } App;
using esp_err_t = int;
enum { ESP_OK = 0, ESP_ERR_NOT_FOUND = 1, ESP_FAIL = 2, ESP_RST_SW = 3 };
enum esp_ota_img_states_t { ESP_OTA_IMG_UNDEFINED, ESP_OTA_IMG_VALID, ESP_OTA_IMG_NEW,
                          ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED };
struct esp_partition_t { uint32_t address = 0x210000; } partition;
struct esp_app_desc_t { uint8_t app_elf_sha256[32]{1}; } description;
esp_err_t state_result = ESP_OK;
esp_ota_img_states_t image_state = ESP_OTA_IMG_VALID;
const esp_partition_t* esp_ota_get_running_partition() { return &partition; }
const esp_partition_t* esp_ota_get_boot_partition() { return &partition; }
const esp_app_desc_t* esp_app_get_description() { return &description; }
int esp_reset_reason() { return ESP_RST_SW; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* out) {
 *out = image_state; return state_result;
}
const char* esp_err_to_name(esp_err_t) { return "test"; }
"""
    support = r"""
namespace esphome::openquatt_incident_manager {
constexpr const char* TAG = "test";
using restart_handoff::BootContext;
using restart_handoff::Record;
using restart_handoff::ResumeState;
struct Output {
 oq_incidents::LinkState link_state{oq_incidents::LinkState::HEALTHY};
 oq_incidents::RunState run_state{oq_incidents::RunState::RUNNING};
 bool available_for_start{true}, running_confirmed{true}, must_stop{}, fault_active{}, protection_active{};
 bool stop_confirmed{}, stop_confirmation_pending{};
};
struct Polling { bool present{true}, state{}; bool has_state() const { return present; } } polling;
struct UnitState {
 struct { uint32_t credit{}; uint32_t snapshot_credit_ms(uint32_t) const { return credit; } } restart_guard;
 struct { bool ready{true}; bool readback_ready(uint32_t) const { return ready; } } boot_resume;
 bool start_feedback_armed{}, released{true}; Output out{};
};
class OpenQuattIncidentManager {
 public:
 bool restarting_{};
 Polling* polling_paused_{&polling};
 std::array<UnitState, 2> units{};
 unsigned configured_hp_count() const { return OQ_TOPOLOGY_DUO ? 2 : 1; }
 const UnitState* unit_(uint8_t hp) const { return hp >= 1 && hp <= configured_hp_count() ? &units[hp-1] : nullptr; }
 bool startup_inhibited(uint8_t hp) const { return unit_(hp) == nullptr || !unit_(hp)->released; }
 bool is_failed() const { return false; }
 bool storage_ready() const { return true; }
 bool hp_configured(uint8_t hp) const { return unit_(hp) != nullptr; }
 uint32_t minimum_off_remaining_ms(uint8_t, uint32_t) const { return 0; }
 Output get_outputs(uint8_t hp) const {
   auto out = unit_(hp)->out;
   if (restarting_) { out.must_stop = true; out.available_for_start = false; }
   return out;
 }
 bool ota_resume_candidate(uint8_t, uint32_t) const;
 void perform_restart_(uint32_t);
};
bool storage_ready = true;
uint32_t configured_minimum_off_ms = 240000;
ResumeState restored_resume{};
PowerHouseResumeReader power_house_reader = nullptr;
OtaResumeGuardReader ota_guard_reader = nullptr;
uint8_t live_mask = OQ_TOPOLOGY_DUO ? 3 : 1;
OpenQuattIncidentManager* live_manager = nullptr;
uint8_t live_guard() {
 if (live_manager == nullptr) return live_mask;
 assert(!live_manager->restarting_);
 uint8_t mask = 0;
 for (uint8_t hp = 1; hp <= live_manager->configured_hp_count(); ++hp)
   if ((live_mask & (1U << (hp-1))) && live_manager->ota_resume_candidate(hp, now_ms)) mask |= 1U << (hp-1);
 return mask;
}
uint32_t current_configuration_hash(uint32_t ms) { return restart_handoff::configuration_hash(ms, OQ_TOPOLOGY_DUO); }
bool restart_handoff_storage_ready() { return storage_ready; }
bool valid_handoff_credit(uint32_t a, uint32_t b) { return storage_ready && a <= 240000 && b <= 240000; }
bool confirm_pending_image_for_controlled_restart() { return true; }
Record written{};
unsigned write_calls = 0;
bool saved = false;
bool persist_target_handoff(uint32_t a, uint32_t b, uint32_t address, const uint8_t* hash,
                            uint8_t mask, const restart_handoff::PowerHouseResumeState& power, bool ota) {
 ++write_calls;
 written = {};
 written.magic = restart_handoff::kRecordMagic;
 written.version = restart_handoff::kRecordVersion;
 written.state = ota ? restart_handoff::kRecordCompletedOta : restart_handoff::kRecordArmed;
 written.resume_mask = mask;
 written.hp1_credit_ms = a; written.hp2_credit_ms = b;
 written.minimum_off_ms = configured_minimum_off_ms;
 written.config_hash = current_configuration_hash(configured_minimum_off_ms);
 written.boot_partition_address = address;
 std::memcpy(written.image_hash, hash, sizeof(written.image_hash));
 written.power_house = power;
 restart_handoff::finalize_record(&written);
 saved = restart_handoff::valid_record(written);
 return saved;
}
bool arm_completed_ota_handoff(uint32_t a, uint32_t b, uint8_t mask,
                               const restart_handoff::PowerHouseResumeState& power) {
 return persist_target_handoff(a, b, partition.address, description.app_elf_sha256, mask, power, true);
}
bool discard_ota_handoff() { written = {}; saved = false; return true; }
bool select_running_after_storage_failure() { return false; }
void OpenQuattOtaHandoff::setup() {}
"""
    production = []
    for signature in ("bool capture_boot_context(", "bool boot_resume_image_confirmed(",
                      "uint8_t current_running_resume_mask(", "bool arm_restart_handoff(",
                      "void OpenQuattOtaHandoff::loop(", "bool OpenQuattOtaHandoff::cycle_hold(",
                      "uint32_t OpenQuattOtaHandoff::full_credit_if_confirmed_(",
                      "void OpenQuattOtaHandoff::clear_ota_snapshot_(",
                      "void OpenQuattOtaHandoff::on_ota_global_state("):
        production.append(function(HANDOFF, signature))
    for signature in ("bool OpenQuattIncidentManager::ota_resume_candidate(",
                      "void OpenQuattIncidentManager::perform_restart_("):
        production.append(function(MANAGER, signature))
    return prefix + header + support + "\n".join(production) + "\n}\n"


class RestartHandoffRuntimeTest(unittest.TestCase):
    def test_restart_mask_comes_from_confirmed_running_and_live_safety(self):
        main = r"""
using namespace esphome::openquatt_incident_manager;
int main() {
 ota_guard_reader = live_guard;
 for (int fault = -1; fault < 10; ++fault) {
   OpenQuattIncidentManager manager;
   live_manager = &manager;
   polling = {};
   live_mask = OQ_TOPOLOGY_DUO ? 3 : 1;
   if (fault == 0) polling.state = true;
   if (fault == 1) polling.present = false;
   if (fault == 2) live_mask = 0;  // Permission, flow or source safety unavailable.
   for (auto& unit : manager.units) {
     if (fault == 3) { unit.out.running_confirmed = false; unit.out.stop_confirmed = true; }
     if (fault == 4) unit.out.link_state = oq_incidents::LinkState::RECOVERING;
     if (fault == 5) unit.out.must_stop = true;
     if (fault == 6) unit.out.fault_active = true;
     if (fault == 7) unit.out.protection_active = true;
     if (fault == 8) unit.boot_resume.ready = false;
     if (fault == 9) unit.released = false;
   }
   saved = false;
   manager.perform_restart_(now_ms);
   assert(manager.restarting_);
   assert(written.hp1_credit_ms == 0 && written.hp2_credit_ms == 0);
   assert(written.resume_mask == (fault < 0 ? (OQ_TOPOLOGY_DUO ? 3 : 1) : 0));
   assert(saved == (fault < 0));
 }
 OpenQuattIncidentManager manager;
 live_manager = &manager;
 polling = {};
 live_mask = OQ_TOPOLOGY_DUO ? 3 : 1;
 manager.units[1].out.running_confirmed = false;
 manager.units[1].out.stop_confirmed = true;
 manager.units[1].restart_guard.credit = 120000;
 manager.perform_restart_(now_ms);
 assert(saved && written.resume_mask == 1);
 assert(written.hp2_credit_ms == (OQ_TOPOLOGY_DUO ? 120000U : 0U));
 assert(App.reboots == 12);
}
"""
        for duo in (False, True):
            with self.subTest(duo=duo):
                compile_run(self, fixture() + main, duo=duo)

    def test_image_statuses_keep_planned_restart_separate_from_completed_ota(self):
        main = r"""
using namespace esphome::openquatt_incident_manager;
int main() {
 for (int result : {ESP_OK, ESP_ERR_NOT_FOUND, ESP_FAIL}) {
   for (auto state : {ESP_OTA_IMG_VALID, ESP_OTA_IMG_UNDEFINED, ESP_OTA_IMG_NEW,
                      ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED}) {
     state_result = result; image_state = state;
     for (bool completed : {false, true}) {
       restored_resume.completed_ota = completed;
       const bool tracked_valid = result == ESP_OK && state == ESP_OTA_IMG_VALID;
       const bool valid_untracked = result == ESP_ERR_NOT_FOUND || (result == ESP_OK && state == ESP_OTA_IMG_UNDEFINED);
       assert(boot_resume_image_confirmed() == (tracked_valid || (!completed && valid_untracked)));
     }
   }
 }
}
"""
        compile_run(self, fixture() + main)

    def test_web_completion_rechecks_without_an_intervening_loop(self):
        main = r"""
using namespace esphome::openquatt_incident_manager;
int main() {
 ota::OTAComponent web, native;
 ota_guard_reader = live_guard;
 for (int scenario = 0; scenario < 5; ++scenario) {
   OpenQuattOtaHandoff handoff;
   handoff.set_background_ota(&web);
   auto* source = scenario == 3 ? &native : &web;
   now_ms = 1000; live_mask = 3;
   handoff.on_ota_global_state(ota::OTA_STARTED, 0, 0, source);
   if (scenario == 0) live_mask = 0;  // Guard loss after last loop, before deferred completion.
   if (scenario == 1) live_mask = 1;  // Losing only the peer also cancels the captured session.
   if (scenario == 2) { live_mask = 0; handoff.loop(); live_mask = 3; }  // Recovery cannot re-arm.
   if (scenario == 3) { now_ms += 20000; live_mask = 0; }  // Blocked native transfer has no live loop.
   if (scenario == 4) now_ms += 45000;  // The exclusive transfer deadline still applies.
   const unsigned before = write_calls;
   handoff.on_ota_global_state(ota::OTA_COMPLETED, 100, 0, source);
   assert(write_calls == before + 1);
   assert(written.resume_mask == (scenario == 3 ? 3 : 0));
   assert(saved == (scenario == 3));
   live_mask = 3;
   if (scenario != 3) assert(!handoff.cycle_hold(1, now_ms));
 }
 OpenQuattOtaHandoff handoff;
 handoff.set_background_ota(&web);
 now_ms = 1000; live_mask = 3;
 handoff.on_ota_global_state(ota::OTA_STARTED, 0, 0, &web);
 handoff.on_ota_global_state(ota::OTA_COMPLETED, 100, 0, &web);
 assert(saved && written.resume_mask == 3);
}
"""
        compile_run(self, fixture() +
                    "namespace ota = esphome::ota;\n" + main)


if __name__ == "__main__":
    unittest.main()
