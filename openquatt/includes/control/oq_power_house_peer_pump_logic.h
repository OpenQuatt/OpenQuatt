#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "../diagnostics/oq_pump_ipwm_feedback.h"
#include "../odu/oq_odu_generation.h"
#include "../performance/oq_energy_logic.h"
#include "../sources/oq_raw_receipt.h"

namespace oq_peer_pump {

constexpr uint64_t kLeaseMs = 12000U;
constexpr uint64_t kMinimumConfirmationSpacingMs = 8900U;
constexpr size_t kConfirmations = 6U;
constexpr float kMaxFlowDeviationLph = 40.0f;
constexpr int kMaxPumpSpreadTenthsW = 30;

enum class EstimateState : uint8_t { UNKNOWN, CONFIRMED_ZERO, ESTIMATED };

struct Estimate {
  EstimateState state = EstimateState::UNKNOWN;
  float watts = NAN;

  static Estimate zero() { return {EstimateState::CONFIRMED_ZERO, 0.0f}; }
  bool available() const { return state != EstimateState::UNKNOWN; }
};

inline bool flow_in_band(float flow_lph, float target_lph) {
  return std::isfinite(target_lph) && target_lph > 0.0f && std::isfinite(flow_lph) && flow_lph > 0.0f &&
         std::fabs(flow_lph - target_lph) <= kMaxFlowDeviationLph;
}

// Keep the break even when another receipt overwrites the failed sample
// before Power House observes the producer.
inline void record_receipt(oq_sources::RawFloatReceipt& receipt, uint64_t& revoked_ms, float value, uint64_t now_ms,
                           bool qualified) {
  if (!qualified) {
    revoked_ms = now_ms;
  } else if (receipt.received && now_ms > receipt.received_ms && now_ms - receipt.received_ms > kLeaseMs) {
    revoked_ms = std::max(revoked_ms, receipt.received_ms + kLeaseMs);
  }
  receipt.observe(value, now_ms, std::isfinite(value));
}

inline void record_flow(oq_sources::RawFloatReceipt& receipt, uint64_t& revoked_ms, float flow_lph, float target_lph,
                        uint64_t now_ms, bool online) {
  record_receipt(receipt, revoked_ms, flow_lph, now_ms, online && flow_in_band(flow_lph, target_lph));
}

struct FlowEvidence {
  oq_sources::RawFloatReceipt receipt;
  uint32_t generation = 0;
  uint8_t route = 0;
  uint8_t contributors = 0;
};

struct Observation {
  uint64_t now_ms = 0;
  bool active = false;
  bool online = false;
  oq_odu::Variant variant{};
  float flow_target_lph = NAN;
  FlowEvidence flow;
  oq_sources::RawFloatReceipt feedback;
  oq_sources::RawFloatReceipt status;
  uint64_t invalidated_ms = 0;
};

inline bool valid_word(float word) {
  return std::isfinite(word) && word >= 0.0f && word <= UINT16_MAX && std::floor(word) == word;
}

inline bool circulation_without_auxiliaries(float raw) {
  constexpr uint16_t kPumpRelay = 0x0800U;
  constexpr uint16_t kAuxiliaries = 0x000CU;  // Bottom plate and crankcase heaters.
  if (!valid_word(raw)) return false;
  return (static_cast<uint16_t>(raw) & (kPumpRelay | kAuxiliaries)) == kPumpRelay;
}

inline oq_pump_ipwm::DecodedFeedback decode_feedback(float raw) {
  return valid_word(raw) ? oq_pump_ipwm::decode(static_cast<uint16_t>(raw)) : oq_pump_ipwm::DecodedFeedback{};
}

inline void record_status(oq_sources::RawFloatReceipt& receipt, uint64_t& revoked_ms, float raw, uint64_t now_ms) {
  record_receipt(receipt, revoked_ms, raw, now_ms, circulation_without_auxiliaries(raw));
}

inline void record_feedback(oq_sources::RawFloatReceipt& receipt, uint64_t& revoked_ms, float raw, uint64_t now_ms) {
  const bool jumped = receipt.valid && decode_feedback(receipt.value).power_valid &&
                      std::fabs(receipt.value - raw) > kMaxPumpSpreadTenthsW;
  record_receipt(receipt, revoked_ms, raw, now_ms, decode_feedback(raw).power_valid && !jumped);
}

class Estimator {
 public:
  Estimate observe(const Observation& in) {
    const auto feedback = decode_feedback(in.feedback.value);
    const auto& flow = in.flow.receipt;
    if (!in.active || !in.online || in.variant != oq_odu::Variant::V1_5 || !in.flow.route ||
        !flow.fresh(in.now_ms, kLeaseMs) || !flow_in_band(flow.value, in.flow_target_lph) ||
        !in.feedback.fresh(in.now_ms, kLeaseMs) || !feedback.power_valid || !in.status.fresh(in.now_ms, kLeaseMs) ||
        !circulation_without_auxiliaries(in.status.value)) {
      revoke(in.now_ms);
      return {};
    }

    if (target_lph_ != in.flow_target_lph || generation_ != in.flow.generation || route_ != in.flow.route ||
        contributors_ != in.flow.contributors) {
      if (route_ != 0U) revoke(in.now_ms);
      target_lph_ = in.flow_target_lph;
      generation_ = in.flow.generation;
      route_ = in.flow.route;
      contributors_ = in.flow.contributors;
    }

    if (in.invalidated_ms > revoked_ms_) revoke(in.invalidated_ms);
    const auto oldest = std::min({in.feedback.received_ms, in.status.received_ms, flow.received_ms});
    if (oldest <= revoked_ms_) return {};

    const uint64_t feedback_ms = in.feedback.received_ms;
    const uint64_t status_ms = in.status.received_ms;
    const uint64_t flow_ms = flow.received_ms;
    if (prior_feedback_ms_ &&
        (feedback_ms < prior_feedback_ms_ || status_ms < prior_status_ms_ || flow_ms < prior_flow_ms_ ||
         (feedback_ms == prior_feedback_ms_ && prior_raw_ != in.feedback.value))) {
      revoke(in.now_ms);
      return {};
    }

    if (prior_feedback_ms_ && (feedback_ms - prior_feedback_ms_ > kLeaseMs || status_ms - prior_status_ms_ > kLeaseMs ||
                               flow_ms - prior_flow_ms_ > kLeaseMs)) {
      clear_confirmations();
    }
    const bool duplicate = feedback_ms == prior_feedback_ms_;
    prior_feedback_ms_ = feedback_ms;
    prior_status_ms_ = status_ms;
    prior_flow_ms_ = flow_ms;
    prior_raw_ = in.feedback.value;
    if (duplicate) return estimate_;

    if (count_) {
      const auto bounds = std::minmax_element(watts_tenths_.begin(), watts_tenths_.begin() + count_);
      const auto raw = static_cast<uint16_t>(in.feedback.value);
      if (std::max(*bounds.second, raw) - std::min(*bounds.first, raw) > kMaxPumpSpreadTenthsW) {
        clear_confirmations();
      }
    }
    if (accepted_ms_ && feedback_ms - accepted_ms_ < kMinimumConfirmationSpacingMs) return estimate_;

    watts_tenths_[next_] = static_cast<uint16_t>(in.feedback.value);
    next_ = (next_ + 1U) % kConfirmations;
    if (count_ < kConfirmations) ++count_;
    accepted_ms_ = feedback_ms;
    if (count_ == kConfirmations) {
      auto sorted = watts_tenths_;
      std::sort(sorted.begin(), sorted.end());
      estimate_ = {EstimateState::ESTIMATED, (sorted[2] + sorted[3]) * 0.05f + oq_energy::kLegacyIdleBaseW};
    }
    return estimate_;
  }

  void revoke(uint64_t now_ms) {
    revoked_ms_ = now_ms;
    prior_feedback_ms_ = prior_status_ms_ = prior_flow_ms_ = 0;
    clear_confirmations();
  }

 private:
  void clear_confirmations() {
    count_ = next_ = 0;
    accepted_ms_ = 0;
    estimate_ = {};
  }

  std::array<uint16_t, kConfirmations> watts_tenths_{};
  size_t count_ = 0;
  size_t next_ = 0;
  Estimate estimate_;
  uint64_t revoked_ms_ = 0;
  uint64_t accepted_ms_ = 0;
  uint64_t prior_feedback_ms_ = 0;
  uint64_t prior_status_ms_ = 0;
  uint64_t prior_flow_ms_ = 0;
  float prior_raw_ = NAN;
  float target_lph_ = NAN;
  uint32_t generation_ = 0;
  uint8_t route_ = 0;
  uint8_t contributors_ = 0;
};

struct DuoEstimator {
  Estimator hp1;
  Estimator hp2;
};

struct DuoEstimate {
  Estimate hp1;
  Estimate hp2;
};

}  // namespace oq_peer_pump
