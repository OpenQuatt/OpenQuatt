#pragma once

#include <cmath>
#include <cstdint>

namespace oq_power_house::manual_line {

struct Settings {
  float cold_c = NAN;
  float rated_w = NAN;
  float zero_c = NAN;

  bool operator==(const Settings& other) const {
    return cold_c == other.cold_c && rated_w == other.rated_w && zero_c == other.zero_c;
  }

  bool valid() const {
    return std::isfinite(cold_c) && std::isfinite(rated_w) && std::isfinite(zero_c) && rated_w > 0.0f &&
           zero_c > cold_c;
  }
};

enum class Phase : uint32_t { EMPTY = 0, APPLYING, APPLIED, RESTORING, RESTORED };
enum class Result : uint8_t { DONE, REFUSED, STORE_FAILED, PENDING, CONFIRM_FAILED };

struct Receipt {
  static constexpr uint32_t VERSION = 1U;
  uint32_t version = VERSION;
  Phase phase = Phase::EMPTY;
  Settings before;
  Settings after;

  bool pending() const { return phase == Phase::APPLYING || phase == Phase::RESTORING; }

  bool valid() const {
    return version == VERSION && phase <= Phase::RESTORED &&
           (phase == Phase::EMPTY || (before.valid() && after.valid() && before.cold_c == after.cold_c));
  }

  bool matches(const Receipt& other) const {
    return phase == other.phase && (phase == Phase::EMPTY || (before == other.before && after == other.after));
  }

  bool can_restore(const Settings& active) const { return phase == Phase::APPLIED && active == after; }
};

inline Settings pending_source(const Receipt& receipt) {
  return receipt.phase == Phase::APPLYING ? receipt.before : receipt.after;
}

inline Settings pending_target(const Receipt& receipt) {
  return receipt.phase == Phase::APPLYING ? receipt.after : receipt.before;
}

// An older build can have saved a manual edit while a receipt was pending.
// Preserve its independent values and roll back any identifiable partial writes.
inline Settings recovery_fallback(const Receipt& receipt, const Settings& active) {
  const auto from = pending_source(receipt);
  const auto to = pending_target(receipt);
  return {active.cold_c, active.rated_w == to.rated_w ? from.rated_w : active.rated_w,
          active.zero_c == to.zero_c ? from.zero_c : active.zero_c};
}

template <typename WriteReceipt, typename WriteSettings>
Result cancel_pending(Receipt& receipt, const Settings& settings, WriteReceipt write_receipt,
                      WriteSettings write_settings) {
  if (!receipt.valid() || !receipt.pending()) return Result::REFUSED;
  if (!write_settings(settings)) return Result::PENDING;
  const Receipt cleared;
  if (!write_receipt(cleared)) return Result::CONFIRM_FAILED;
  receipt = cleared;
  return Result::DONE;
}

template <typename WriteReceipt, typename WriteSettings>
bool prepare_manual_edit(Receipt& receipt, const Settings& active, bool uncertain, WriteReceipt write_receipt,
                         WriteSettings write_settings) {
  if (uncertain && !write_receipt(receipt)) return false;
  return !receipt.pending() || cancel_pending(receipt, active, write_receipt, write_settings) == Result::DONE;
}

// The final receipt and the pair are separate writes. A failed receipt must
// never cause a pair already verified on flash to be reverted only in RAM.
template <typename WriteReceipt, typename WriteSettings>
Result finish_pending(Receipt& receipt, const Settings& active, WriteReceipt write_receipt,
                      WriteSettings write_settings) {
  if (!receipt.valid() || !receipt.pending()) return Result::REFUSED;
  const auto from = pending_source(receipt);
  const auto to = pending_target(receipt);
  if (active.cold_c != from.cold_c || (active.rated_w != from.rated_w && active.rated_w != to.rated_w) ||
      (active.zero_c != from.zero_c && active.zero_c != to.zero_c))
    return cancel_pending(receipt, recovery_fallback(receipt, active), write_receipt, write_settings);
  if (!write_settings(to)) return Result::PENDING;
  Receipt completed = receipt;
  completed.phase = receipt.phase == Phase::APPLYING ? Phase::APPLIED : Phase::RESTORED;
  if (!write_receipt(completed)) return Result::CONFIRM_FAILED;
  receipt = completed;
  return Result::DONE;
}

template <typename WriteReceipt, typename WriteSettings>
Result apply(Receipt& receipt, const Settings& active, const Settings& proposal, WriteReceipt write_receipt,
             WriteSettings write_settings) {
  if (!receipt.valid() || receipt.pending() || !active.valid() || !proposal.valid() || active == proposal ||
      active.cold_c != proposal.cold_c)
    return Result::REFUSED;
  const Receipt pending{Receipt::VERSION, Phase::APPLYING, active, proposal};
  if (!write_receipt(pending)) return Result::STORE_FAILED;
  receipt = pending;
  return finish_pending(receipt, active, write_receipt, write_settings);
}

template <typename WriteReceipt, typename WriteSettings>
Result restore(Receipt& receipt, const Settings& active, WriteReceipt write_receipt, WriteSettings write_settings) {
  if (!receipt.valid() || !receipt.can_restore(active)) return Result::REFUSED;
  Receipt pending = receipt;
  pending.phase = Phase::RESTORING;
  if (!write_receipt(pending)) return Result::STORE_FAILED;
  receipt = pending;
  return finish_pending(receipt, active, write_receipt, write_settings);
}

}  // namespace oq_power_house::manual_line
