#include "components/openquatt_odu_eeprom/OpenQuattOduEeprom.cpp"
#include <iostream>

using namespace esphome;
using namespace esphome::openquatt_odu_eeprom;
using oq_eeprom::State;

struct Device : OpenQuattOduEeprom {
  uint32_t generation() const { return status_.generation; }
  State state() { return record_().state; }
  std::string result() const { return status_.result; }
  const oq_eeprom::Record& record() { return record_(); }
  bool busy() const { return status_.busy; }
  bool available() const { return status_.available; }
};
modbus_controller::ModbusController controller;
openquatt_odu_eeprom_dump::OpenQuattOduEepromDump dump;
openquatt_odu_settings::OpenQuattOduSettings settings;
openquatt_odu_defrost::OpenQuattOduDefrost defrost;
openquatt_web_auth::OpenQuattWebAuth auth;
oq_eeprom::Image stored{}, shadow{};
uint16_t mode = 0U, program = 0x0201U, serial = 123U;
unsigned writes = 0U, commits = 0U;
bool commit_works = true;
uint16_t lose_address = 0U;
uint16_t write_status = 0U;

void safe() { oq_eeprom::safety[0] = {true, true, true, true, true, mode, test_now}; }
void attach(Device& device) {
  dump.busy = false;
  device.set_controller(&controller);
  device.set_eeprom_dump(&dump);
  device.set_settings(&settings);
  device.set_defrost(&defrost);
  device.set_web_auth(&auth);
  device.set_hp_index(1U);
  device.setup();
}
void reset() {
  durable.clear();
  pending_storage.clear();
  modbus::requests.clear();
  instances[0] = instances[1] = nullptr;
  bus_owner = nullptr;
  oq_eeprom::hold_mask.store(0U);
  oq_eeprom::configured_mask = 0U;
  oq_eeprom::stop_epoch = 0U;
  oq_eeprom::permit = {};
  allocation_ok = nvs_write_ok = nvs_commit_ok = nvs_read_ok = commit_works = profile_sync_ok = true;
  commit_despite_error = false;
  available_entries = 1400U;
  test_partition.size = 65536U;
  revocations = nvs_commits = writes = commits = 0U;
  lose_address = write_status = 0U;
  mode = 0U;
  program = 0x0201U;
  serial = 123U;
  test_now = 1000U;
  web_server_base::test_server = {};
  stored = {};
  stored[237] = 3U;
  stored[238] = 34U;
  stored[239] = 3U;
  stored[275] = 1U;
  stored[276] = 4U;
  for (size_t index = 1U; index <= 20U; ++index) {
    const auto value = static_cast<uint8_t>(20U + index * 3U);
    stored[(index <= 10U ? 3000U + index : 3060U + index - 11U) - 2999U] = value;
    stored[(index <= 10U ? 3011U + index : 3050U + index - 11U) - 2999U] = value;
  }
  oq_eeprom::finalize_image(stored);
  shadow = stored;
  safe();
  assert(oq_eeprom::supported_values(stored, oq_odu::Variant::V2_NEW_MODEL));
}
void respond(const modbus::Request& request) {
  const auto& pdu = request.pdu;
  const uint16_t address = uint16_t(pdu[1]) << 8U | pdu[2];
  const uint16_t value = uint16_t(pdu[3]) << 8U | pdu[4];
  std::vector<uint8_t> response;
  if (pdu[0] == 3U) {
    response = {3U, static_cast<uint8_t>(value * 2U)};
    for (size_t index = 0; index < value; ++index) {
      uint16_t word = 0U;
      if (address == 2114U)
        word = index == 13U ? 0x1037U : index == 8U ? program : 0U;
      else if (address == 11219U)
        word = index == 0U ? serial : 0U;
      else if (address == 2099U)
        word = index == 0U ? mode : 0U;
      else if (address == 4003U)
        word = write_status;
      else
        word = shadow.at(address - 2999U + index);
      response.push_back(word >> 8U);
      response.push_back(word & 255U);
    }
  } else {
    assert(oq_eeprom::permitted_write(request.device->address_, pdu.data(), pdu.size(), test_now));
    ++writes;
    if (pdu[0] == 16U) {
      for (size_t index = 0; index < value; ++index) shadow.at(address - 2999U + index) = pdu.at(7U + index * 2U);
    } else if (address == 3999U)
      mode = value;
    else if (address == 4000U) {
      ++commits;
      write_status = 4U;
      if (commit_works) stored = shadow;
    }
    response.assign(pdu.begin(), pdu.begin() + 5);
  }
  if (lose_address == address && pdu[0] != 3U) {
    lose_address = 0U;
    assert(!request.device->lose(request));
  } else
    request.device->deliver(request, response);
}
void pump(Device& device) {
  for (unsigned iteration = 0; iteration < 500U; ++iteration) {
    test_now += 20U;
    safe();
    device.loop();
    if (!modbus::requests.empty()) {
      const auto request = modbus::requests.front();
      modbus::requests.erase(modbus::requests.begin());
      respond(request);
    }
    if (!device.busy() && modbus::requests.empty()) return;
  }
  assert(false && "operation did not terminate");
}
void command(Device& device, OpenQuattOduEeprom::Action action, uint8_t groups = 0U, bool cold = false) {
  assert(device.enqueue(action, groups, device.generation(), cold));
  pump(device);
}
void cold_boot(Device& device) {
  device.offline();
  device.loop();
  shadow = stored;
  mode = write_status = 0U;
  safe();
}
void backup(Device& device) {
  command(device, OpenQuattOduEeprom::Action::BEGIN_BACKUP);
  assert(device.state() == State::WAIT_BASELINE && oq_eeprom::holding() && revocations == 2U);
  command(device, OpenQuattOduEeprom::Action::CAPTURE, 0U, true);
  assert(device.result() == "INVALID_STATE");  // A checkbox is insufficient without an observed disconnect.
  cold_boot(device);
  command(device, OpenQuattOduEeprom::Action::CAPTURE, 0U, true);
  assert(device.state() == State::BASELINE && !oq_eeprom::holding() && writes == 0U);
}
void prepare(Device& device) {
  shadow[238] = 33U;
  command(device, OpenQuattOduEeprom::Action::PREPARE, oq_eeprom::BOTTOM);
  assert(device.state() == State::PREPARED && oq_eeprom::holding());
  assert(device.record().candidate[238] == 33U && device.record().original[238] == 34U);
}
int main() {
  static_assert(sizeof(oq_eeprom::Record) == 2116U);
  reset();
  Device happy;
  attach(happy);
  backup(happy);
  prepare(happy);
  assert(!happy.enqueue(OpenQuattOduEeprom::Action::COMMIT, 0U, happy.generation() - 1U, false));
  command(happy, OpenQuattOduEeprom::Action::COMMIT);
  assert(writes == 9U && commits == 1U && happy.state() == State::WAIT_VERIFY && oq_eeprom::holding());
  command(happy, OpenQuattOduEeprom::Action::VERIFY, 0U, true);
  assert(happy.result() == "SAFETY_BLOCKED");
  cold_boot(happy);
  command(happy, OpenQuattOduEeprom::Action::VERIFY, 0U, true);
  assert(happy.result() == "VERIFIED" && !oq_eeprom::holding() && happy.record().original[238] == 34U);
  auto baseline_record = happy.record();
  auto source = baseline_record.baseline;
  source[100] ^= 1U;  // Unknown fields must never enter a candidate.
  assert(!oq_eeprom::prepare(baseline_record, source, oq_eeprom::ALL, false));
  source = baseline_record.baseline;
  source[237] = 255U;
  assert(!oq_eeprom::prepare(baseline_record, source, oq_eeprom::BOTTOM, false));
  source = baseline_record.baseline;
  source[3069U - 2999U] = 100U;  // Monotone but beyond this unit's original cooling envelope.
  assert(!oq_eeprom::prepare(baseline_record, source, oq_eeprom::FREQUENCY, false));
  source = baseline_record.baseline;
  source[238] = 32U;
  source[3001U - 2999U] = 24U;
  assert(oq_eeprom::prepare(baseline_record, source, oq_eeprom::BOTTOM, false));
  assert(baseline_record.candidate[3001U - 2999U] == happy.record().baseline[3001U - 2999U]);
  assert(baseline_record.candidate[238] == 32U && oq_eeprom::valid_crc(baseline_record.candidate));
  http_output.clear();
  happy.write_status(nullptr, true);
  assert(http_output.find("private-csrf-token") == std::string::npos);
  assert(http_output.find("\"original_crc\":") != std::string::npos &&
         http_output.find("\"identity\":") != std::string::npos);

  reset();
  Device stale_status;
  attach(stale_status);
  backup(stale_status);
  prepare(stale_status);
  write_status = 4U;
  command(stale_status, OpenQuattOduEeprom::Action::COMMIT);
  assert(stale_status.result() == "COMMIT_NOT_IDLE" && writes == 0U && commits == 0U);

  reset();
  Device stale;
  attach(stale);
  backup(stale);
  prepare(stale);
  shadow[238] = 32U;
  command(stale, OpenQuattOduEeprom::Action::COMMIT);
  assert(stale.result() == "STALE" && writes == 0U);
  command(stale, OpenQuattOduEeprom::Action::CANCEL);
  assert(!oq_eeprom::holding());

  reset();
  Device false_success;
  attach(false_success);
  backup(false_success);
  prepare(false_success);
  commit_works = false;
  command(false_success, OpenQuattOduEeprom::Action::COMMIT);
  assert(false_success.state() == State::WAIT_VERIFY);
  cold_boot(false_success);
  command(false_success, OpenQuattOduEeprom::Action::VERIFY, 0U, true);
  assert(false_success.result() == "NOT_PERSISTED" && !oq_eeprom::holding());

  reset();
  Device lost;
  attach(lost);
  backup(lost);
  prepare(lost);
  lose_address = 4000U;
  command(lost, OpenQuattOduEeprom::Action::COMMIT);
  assert(lost.state() == State::UNCERTAIN && commits == 1U && oq_eeprom::holding());
  Device rebooted;
  oq_eeprom::hold_mask.store(0U);
  attach(rebooted);
  assert(oq_eeprom::holding() && rebooted.state() == State::UNCERTAIN);
  for (unsigned i = 0; i < 20U; ++i) rebooted.loop();
  assert(modbus::requests.empty() && commits == 1U);
  cold_boot(rebooted);
  command(rebooted, OpenQuattOduEeprom::Action::VERIFY, 0U, true);
  assert(rebooted.result() == "VERIFIED");

  reset();
  Device persistence;
  attach(persistence);
  backup(persistence);
  prepare(persistence);
  nvs_commit_ok = false;
  command(persistence, OpenQuattOduEeprom::Action::COMMIT);
  assert(writes == 0U && persistence.result() == "STORAGE_FAILED" && oq_eeprom::holding());

  reset();
  Device identity;
  attach(identity);
  backup(identity);
  prepare(identity);
  serial = 999U;
  command(identity, OpenQuattOduEeprom::Action::COMMIT);
  assert(identity.result() == "IDENTITY_MISMATCH" && writes == 0U);

  reset();
  Device cancelled;
  attach(cancelled);
  command(cancelled, OpenQuattOduEeprom::Action::BEGIN_BACKUP);
  command(cancelled, OpenQuattOduEeprom::Action::CANCEL);
  const uint32_t previous_generation = cancelled.generation();
  command(cancelled, OpenQuattOduEeprom::Action::BEGIN_BACKUP);
  assert(cancelled.generation() > previous_generation);
  allocation_ok = false;
  oq_eeprom::hold_mask.store(0U);
  Device no_memory;
  attach(no_memory);
  assert(!no_memory.available() && oq_eeprom::holding());

  reset();
  test_partition.size = 24576U;
  Device small;
  attach(small);
  assert(!small.available() && !oq_eeprom::holding() && durable.empty());

  reset();
  program = 0x0122U;
  Device unsupported;
  attach(unsupported);
  command(unsupported, OpenQuattOduEeprom::Action::BEGIN_BACKUP);
  assert(unsupported.result() == "UNSUPPORTED" && durable.empty() && !oq_eeprom::holding());
  reset();
  Device staged_lost;
  attach(staged_lost);
  backup(staged_lost);
  prepare(staged_lost);
  lose_address = 2999U;
  command(staged_lost, OpenQuattOduEeprom::Action::COMMIT);
  assert(writes == 1U && commits == 0U && staged_lost.state() == State::UNCERTAIN);
  cold_boot(staged_lost);
  command(staged_lost, OpenQuattOduEeprom::Action::VERIFY, 0U, true);
  assert(staged_lost.result() == "NOT_PERSISTED" && !oq_eeprom::holding());

  reset();
  Device native_failed;
  attach(native_failed);
  backup(native_failed);
  prepare(native_failed);
  nvs_commit_ok = false;
  commit_despite_error = true;  // Flash changed despite the lost/error acknowledgement.
  command(native_failed, OpenQuattOduEeprom::Action::COMMIT);
  assert(writes == 0U && native_failed.result() == "STORAGE_FAILED");
  nvs_commit_ok = true;
  oq_eeprom::hold_mask.store(0U);
  Device native_reboot;
  attach(native_reboot);
  assert(native_reboot.state() == State::WRITING && oq_eeprom::holding());
  assert(modbus::requests.empty());

  reset();
  Device corrupt;
  attach(corrupt);
  backup(corrupt);
  prepare(corrupt);
  durable.at("hp1a").back() ^= 1U;
  oq_eeprom::hold_mask.store(0U);
  Device corrupt_reboot;
  attach(corrupt_reboot);
  assert(corrupt_reboot.result() == "STORAGE_FAILED" && oq_eeprom::holding());
  assert(!corrupt_reboot.enqueue(OpenQuattOduEeprom::Action::BEGIN_BACKUP, 0U, 0U, false));

  reset();
  Device interrupted;
  attach(interrupted);
  backup(interrupted);
  prepare(interrupted);
  assert(interrupted.enqueue(OpenQuattOduEeprom::Action::COMMIT, 0U, interrupted.generation(), false));
  for (unsigned i = 0; i < 40U; ++i) {
    safe();
    interrupted.loop();
    if (modbus::requests.empty()) continue;
    const auto request = modbus::requests.front();
    if (request.pdu[0] == 16U) break;
    modbus::requests.erase(modbus::requests.begin());
    respond(request);
  }
  assert(!modbus::requests.empty() && modbus::requests.front().pdu[0] == 16U);
  const auto owned = oq_eeprom::permit;
  // An unreserved competing operation may not revoke the first owner's immutable permit.
  Device competitor;
  competitor.set_hp_index(2U);
  assert(owned.words != nullptr);
  struct ReleaseProbe : Device {
    void release() { release_(); }
  } probe;
  probe.release();
  assert(oq_eeprom::permit.words == owned.words);
  web_server_base::test_server.epoch++;
  pump(interrupted);
  assert(writes == 0U && interrupted.state() == State::UNCERTAIN && oq_eeprom::holding());

  reset();
  Device full;
  attach(full);
  backup(full);
  prepare(full);
  available_entries = 100U;
  command(full, OpenQuattOduEeprom::Action::COMMIT);
  assert(writes == 0U && full.result() == "STORAGE_FAILED");

  reset();
  stored[275] = 0U;
  oq_eeprom::finalize_image(stored);
  shadow = stored;
  Device manual_config;
  attach(manual_config);
  command(manual_config, OpenQuattOduEeprom::Action::BEGIN_BACKUP);
  cold_boot(manual_config);
  command(manual_config, OpenQuattOduEeprom::Action::CAPTURE, 0U, true);
  assert(manual_config.result() == "INVALID_IMAGE" && writes == 0U && oq_eeprom::holding());

  std::cout << "EEPROM production runtime: PASS\n";
}
