#include "OpenQuattDailyEnergy.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome::openquatt_daily_energy {
void OpenQuattDailyEnergy::setup() {
  pref_ = global_preferences->make_preference<DatedTotals>(fnv1_hash("oq_dated_daily_energy_v1"));
  pref_.load(&saved_);
  clock_->add_on_time_sync_callback([this] { prepare_(clock_->now(), millis()); });
  prepare_(clock_->now(), millis());
  for (size_t index = 0; index < CHANNEL_COUNT; ++index) {
    powers_[index]->add_on_state_callback([this, index](float watts) { on_power_(index, watts); });
  }
}

bool OpenQuattDailyEnergy::prepare_(const ESPTime& now, uint32_t now_ms) {
  const uint32_t date_key =
      now.is_valid() ? static_cast<uint32_t>(now.year) * 10000U + now.month * 100U + now.day_of_month : 0U;
  const uint32_t previous_date = state_.totals().date_key;
  Totals checkpoint{};
  if (date_key != 0U && previous_date == 0U) {
    // Legacy daily totals may already contain energy from a different day,
    // even when written to the archive under today's date.
    if (!archive_->try_read_daily_checkpoint(date_key, checkpoint)) {
      return false;
    }
  }
  if (!state_.prepare(date_key, now_ms, saved_, checkpoint)) {
    return false;
  }
  if (date_key != previous_date) {
    pref_.save(&state_.totals());
    for (size_t index = 0; index < CHANNEL_COUNT; ++index) {
      outputs_[index]->publish_state(state_.totals().wh[index] / 1000.0f);
    }
  }
  return true;
}

void OpenQuattDailyEnergy::on_power_(size_t index, float watts) {
  const uint32_t now_ms = millis();
  if (prepare_(clock_->now(), now_ms) && state_.add_power(index, watts, now_ms)) {
    pref_.save(&state_.totals());
    outputs_[index]->publish_state(state_.totals().wh[index] / 1000.0f);
  }
}

void OpenQuattDailyEnergy::capture_history() {
  const auto now = clock_->now();
  if (!prepare_(now, millis())) {
    return;
  }
  const auto& wh = state_.totals().wh;
  archive_->capture_day_totals(now, wh[0] / 1000.0f, wh[1] / 1000.0f, wh[2] / 1000.0f, wh[3] / 1000.0f, wh[4] / 1000.0f,
                               wh[5] / 1000.0f, wh[6] / 1000.0f);
}
}  // namespace esphome::openquatt_daily_energy
