#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "../oq_service_runtime.h"

namespace oq_flow_autotune {

enum StateCode {
  STATE_IDLE = 0,
  STATE_ARM = 1,
  STATE_STEP1 = 2,
  STATE_RECOVER = 3,
  STATE_STEP2 = 4,
  STATE_VALIDATE_RECOVER = 5,
  STATE_VALIDATE = 6,
  STATE_ABORT = 7,
};

struct RuntimeConfig {
  int sample_time_s;
  int duration_s;
  float validate_step_frac;
  float validate_step_min_lph;
  float validate_step_max_lph;
  float validate_band_floor_lph;
  float validate_band_frac;
  float validate_overshoot_frac;
  float validate_seed_lambda_frac;
  float validate_seed_ti_frac;
  float validate_seed_min_lambda_s;
  float validate_seed_min_ti_s;
  int validate_recover_max_s;
  int validate_min_active_s;
  int validate_duration_s;
  int validate_max_retries;
  int baseline_pwm;
  int baseline_wait_max_s;
  float step1_frac;
  int step1_min_ipwm;
  int step1_max_ipwm;
  float step2_frac;
  int step2_min_ipwm;
  int step2_max_ipwm;
  float step_consistency_frac;
  int pwm_min;
  int pwm_max;
  int min_step;
  int settle_s;
  float stable_band_lph;
  float tau_fallback_s;
  float kp_min;
  float kp_max;
  float ki_min;
  float ki_max;
};

inline RuntimeConfig make_runtime_config(int sample_time_s, int pwm_min, int pwm_max, int min_step, int settle_s,
                                         float stable_band_lph, float tau_fallback_s, float kp_min, float kp_max,
                                         float ki_min, float ki_max) {
  return RuntimeConfig{sample_time_s,
                       120,
                       0.05f,
                       15.0f,
                       40.0f,
                       8.0f,
                       0.15f,
                       0.25f,
                       8.0f,
                       2.0f,
                       60.0f,
                       60.0f,
                       120,
                       60,
                       180,
                       2,
                       400,
                       60,
                       0.06f,
                       10,
                       28,
                       0.10f,
                       15,
                       40,
                       0.20f,
                       pwm_min,
                       pwm_max,
                       min_step,
                       settle_s,
                       stable_band_lph,
                       tau_fallback_s,
                       kp_min,
                       kp_max,
                       ki_min,
                       ki_max};
}

inline int clamp_ipwm(int value, int min_ipwm, int max_ipwm) {
  if (value < min_ipwm) return min_ipwm;
  if (value > max_ipwm) return max_ipwm;
  return value;
}

// Replay the bounded response against the confirmed endpoint, not an intermediate window.
inline float response_t63_s(const float* samples, uint8_t count, float baseline, float settled, int sample_time_s) {
  if (!isfinite(baseline) || !isfinite(settled) || !(settled > baseline) || sample_time_s <= 0) return NAN;
  const float threshold = baseline + 0.632f * (settled - baseline);
  float previous = baseline;
  for (uint8_t i = 0; i < count; ++i) {
    const float current = samples[i];
    if (current >= threshold && previous < threshold) {
      const float fraction = (threshold - previous) / (current - previous);
      return ((float)i + fraction) * (float)sample_time_s;
    }
    previous = current;
  }
  return NAN;
}

class FlowAutotuneRuntime {
 public:
  bool busy() const { return state_ != STATE_IDLE; }

  void reset() {
    state_ = STATE_IDLE;
    t_s_ = 0;
    pwm0_ = 850;
    pwm_step_ = 850;
    pv0_ = NAN;
    pv_ss_ = NAN;
    pv63_time_s_ = NAN;
    step1_k_ = NAN;
    step1_tau_ = NAN;
    step1_pwm_step_ = 850;
    clear_window();
    ss_confirm_cnt_ = 0;
    response_count_ = 0;
    saved_kp_ = NAN;
    saved_ki_ = NAN;
    seed_kp_ = NAN;
    seed_ki_ = NAN;
    validate_sp0_ = NAN;
    validate_sp1_ = NAN;
    validate_peak_pv_ = NAN;
    validate_confirm_cnt_ = 0;
    validate_settle_ready_ = false;
    validate_pre_overshoot_ = false;
    validate_attempt_ = 0;
    id(oq_flow_autotune_state) = STATE_IDLE;
  }

  void start(const RuntimeConfig& cfg, uint32_t now_ms) {
    const int cm_code = id(oq_control_mode_code);
    const bool cm100_ready = oq_commissioning::is_cm100(cm_code);
    const bool task_running =
        oq_commissioning::task_running(id(oq_commissioning_active), id(oq_commissioning_task_code));
    if (busy() || id(oq_flow_autotune_active) || id(oq_flow_autotune_req) || task_running) {
      oq_service_status::set_flow_autotune("REFUSED: BUSY");
      return;
    }
    if (!cm100_ready) {
      oq_service_status::set_flow_autotune("REFUSED: not CM100");
      return;
    }

    ESP_LOGI(
        "quatt.cm100.autotune",
        "Autotune requested (cm=%d flow_mode=%s setpoint=%.0fL/h last_good_pwm=%d baseline_pwm=%d active=%d state=%d)",
        cm_code, id(oq_flow_control_mode).current_option().c_str(), id(oq_flow_setpoint_lph).state,
        (int)id(oq_flow_last_good_pwm), cfg.baseline_pwm, (int)id(oq_flow_autotune_active), (int)state_);

    reset();
    id(oq_commissioning_task_code) = oq_commissioning::TASK_FLOW_AUTOTUNE;
    id(oq_commissioning_request_pending) = false;
    id(oq_commissioning_active) = true;
    id(oq_commissioning_abort_requested) = false;
    id(oq_commissioning_state_code) = oq_commissioning::TASK_STATE_REQUESTED;
    id(oq_commissioning_started_ms) = now_ms;
    id(oq_commissioning_state_since_ms) = now_ms;
    id(oq_flow_autotune_req) = true;
    id(oq_flow_autotune_abort) = false;
    id(oq_flow_autotune_active) = false;
    id(oq_flow_autotune_pwm) = cfg.baseline_pwm;
    oq_service_status::set_commissioning("FLOW AUTOTUNE STARTED");
    oq_service_status::set_flow_autotune("REQUESTED");
  }

  void tick(const RuntimeConfig& cfg, uint32_t now_ms) {
    sync_external_idle();
    if (id(oq_flow_autotune_abort)) {
      id(oq_flow_autotune_abort) = false;
      if (state_ != STATE_ABORT) publish("ABORTED");
      state_ = STATE_ABORT;
    }

    const float pv = id(flow_rate_selected).state;
    const bool flow_valid = !(isnan(pv) || pv <= 0.0f);
    if (state_ != STATE_IDLE && id(oq_flow_autotune_req)) {
      id(oq_flow_autotune_req) = false;
      oq_service_status::set_flow_autotune("REFUSED: BUSY");
    }

    switch (state_) {
      case STATE_IDLE:
        run_idle(cfg, now_ms, pv, flow_valid);
        break;
      case STATE_ARM:
        run_baseline_wait(cfg, pv, flow_valid, false);
        break;
      case STATE_STEP1:
        run_open_loop_step(cfg, pv, flow_valid, false);
        break;
      case STATE_RECOVER:
        run_baseline_wait(cfg, pv, flow_valid, true);
        break;
      case STATE_STEP2:
        run_open_loop_step(cfg, pv, flow_valid, true);
        break;
      case STATE_VALIDATE_RECOVER:
        run_validation_recover(cfg, pv, flow_valid);
        break;
      case STATE_VALIDATE:
        run_validation(cfg, pv, flow_valid);
        break;
      case STATE_ABORT:
      default:
        finish_abort(cfg);
        break;
    }
    id(oq_flow_autotune_state) = state_;
  }

 private:
  StateCode state_{STATE_IDLE};
  int t_s_{0};
  int pwm0_{850};
  int pwm_step_{850};
  float pv0_{NAN};
  float pv_ss_{NAN};
  float pv63_time_s_{NAN};
  float step1_k_{NAN};
  float step1_tau_{NAN};
  int step1_pwm_step_{850};
  float w0_{NAN};
  float w1_{NAN};
  float w2_{NAN};
  int ss_confirm_cnt_{0};
  float step_window_[6]{};
  uint8_t step_window_count_{0};
  float response_samples_[32]{};
  uint8_t response_count_{0};
  float saved_kp_{NAN};
  float saved_ki_{NAN};
  float seed_kp_{NAN};
  float seed_ki_{NAN};
  float validate_sp0_{NAN};
  float validate_sp1_{NAN};
  float validate_peak_pv_{NAN};
  int validate_confirm_cnt_{0};
  bool validate_settle_ready_{false};
  bool validate_pre_overshoot_{false};
  int validate_attempt_{0};

  template <typename NumberEntity>
  void set_number_value(NumberEntity& number_entity, float value) {
    auto call = number_entity.make_call();
    call.set_value(value);
    call.perform();
  }

  bool valid_mode() const {
    return oq_commissioning::flow_autotune_mode_valid(id(oq_control_mode_code), id(oq_commissioning_task_code));
  }

  int clamp_runtime_ipwm(const RuntimeConfig& cfg, int value) const {
    return clamp_ipwm(value, cfg.pwm_min, cfg.pwm_max);
  }

  void clear_window() {
    w0_ = NAN;
    w1_ = NAN;
    w2_ = NAN;
    step_window_count_ = 0;
  }

  void clear_open_loop_memory() {
    t_s_ = 0;
    response_count_ = 0;
    clear_window();
    ss_confirm_cnt_ = 0;
    pv0_ = NAN;
    pv_ss_ = NAN;
    pv63_time_s_ = NAN;
  }

  void clear_validation_run_memory() {
    t_s_ = 0;
    clear_window();
    validate_peak_pv_ = NAN;
    validate_confirm_cnt_ = 0;
    validate_settle_ready_ = false;
    validate_pre_overshoot_ = false;
  }

  void push_window(float sample) {
    w0_ = w1_;
    w1_ = w2_;
    w2_ = sample;
  }

  bool window_ready() const { return !isnan(w0_) && !isnan(w1_) && !isnan(w2_); }

  float window_mean() const { return (w0_ + w1_ + w2_) / 3.0f; }

  float window_spread() const { return fmaxf(w0_, fmaxf(w1_, w2_)) - fminf(w0_, fminf(w1_, w2_)); }

  bool steady_window(float spread_limit_lph, float slope_limit_lph) const {
    return window_ready() && window_spread() <= spread_limit_lph && fabsf(w1_ - w0_) <= slope_limit_lph &&
           fabsf(w2_ - w1_) <= slope_limit_lph;
  }

  // A wider noise band alone can mistake a sustained ramp for a plateau.
  // Compare averaged halves over six observations (50 s with the normal 10 s cadence).
  bool flow_plateau(float pv, float spread_limit_lph) {
    for (int i = 0; i < 5; ++i) step_window_[i] = step_window_[i + 1];
    step_window_[5] = pv;
    if (step_window_count_ < 6) ++step_window_count_;
    if (step_window_count_ < 6) return false;
    float low = step_window_[0];
    float high = low;
    for (float value : step_window_) {
      low = fminf(low, value);
      high = fmaxf(high, value);
    }
    const float first_mean = (step_window_[0] + step_window_[1] + step_window_[2]) / 3.0f;
    const float last_mean = (step_window_[3] + step_window_[4] + step_window_[5]) / 3.0f;
    bool rising = false;
    bool falling = false;
    for (int i = 1; i < 6; ++i) {
      const float delta = step_window_[i] - step_window_[i - 1];
      rising = rising || delta > 0.001f;
      falling = falling || delta < -0.001f;
    }
    // Allow a nearly settled monotonic tail within meter noise, but reject sustained one-way drift.
    const bool one_way_drift = rising != falling && high - low > 4.5f;
    return !one_way_drift && high - low <= spread_limit_lph && fabsf(last_mean - first_mean) <= 10.0f;
  }

  bool baseline_ready(const RuntimeConfig& cfg) const {
    return window_ready() && t_s_ >= cfg.settle_s && t_s_ <= cfg.baseline_wait_max_s &&
           window_spread() <= cfg.stable_band_lph && window_mean() > 0.0f;
  }

  void publish(const char* status) { oq_service_status::set_flow_autotune(status); }

  void abort_with(const char* status) {
    publish(status);
    state_ = STATE_ABORT;
  }

  void release_commissioning(bool keep_cm100 = true) {
    id(oq_flow_autotune_active) = false;
    id(oq_flow_autotune_abort) = false;
    id(oq_flow_autotune_req) = false;
    oq_commissioning::clear_container(keep_cm100);
  }

  void sync_external_idle() {
    if (state_ != STATE_IDLE && id(oq_flow_autotune_state) == STATE_IDLE && !id(oq_flow_autotune_req) &&
        !id(oq_flow_autotune_abort) && !id(oq_flow_autotune_active)) {
      reset();
    }
  }

  void run_idle(const RuntimeConfig& cfg, uint32_t now_ms, float pv, bool flow_valid) {
    if (!id(oq_flow_autotune_req)) return;
    if (!valid_mode()) {
      id(oq_commissioning_request_pending) = true;
      id(oq_commissioning_active) = false;
      publish("WAITING_FOR_CM100");
      ESP_LOGI("quatt.cm100.autotune", "Waiting for CM100 before autotune can start");
      return;
    }

    id(oq_flow_autotune_req) = false;
    clear_open_loop_memory();
    pwm0_ = clamp_runtime_ipwm(cfg, cfg.baseline_pwm);
    id(oq_flow_autotune_pwm) = pwm0_;
    id(oq_flow_autotune_active) = true;
    id(oq_commissioning_request_pending) = false;
    id(oq_commissioning_active) = true;
    id(oq_commissioning_abort_requested) = false;
    id(oq_commissioning_started_ms) = now_ms;
    id(oq_commissioning_state_since_ms) = now_ms;

    ESP_LOGI("quatt.cm100.autotune",
             "Baseline settle started (pwm0=%d pv=%.1f target=%.1f settle=%us stable_band=%.1f)", pwm0_, pv,
             id(oq_flow_setpoint_lph).state, (unsigned int)cfg.settle_s, cfg.stable_band_lph);
    publish(flow_valid ? "SETTLING" : "WAITING_FOR_FLOW");
    state_ = STATE_ARM;
  }

  int step_size(const RuntimeConfig& cfg, bool second_step, int pwm0) const {
    const float frac = second_step ? cfg.step2_frac : cfg.step1_frac;
    const int step_min = second_step ? cfg.step2_min_ipwm : cfg.step1_min_ipwm;
    const int step_max = second_step ? cfg.step2_max_ipwm : cfg.step1_max_ipwm;
    int u_step = (int)roundf(frac * (float)pwm0);
    if (u_step < step_min) u_step = step_min;
    if (u_step > step_max) u_step = step_max;
    if (second_step) {
      const int step1_u = pwm0 - clamp_runtime_ipwm(cfg, step1_pwm_step_);
      if (u_step <= step1_u) u_step = step1_u + 8;
    }
    const int headroom = pwm0 - cfg.pwm_min;
    if (u_step > headroom) u_step = headroom;
    return u_step;
  }

  void run_baseline_wait(const RuntimeConfig& cfg, float pv, bool flow_valid, bool second_step) {
    const int pwm0 = clamp_runtime_ipwm(cfg, pwm0_);
    const int t = t_s_;
    id(oq_flow_autotune_pwm) = pwm0;
    id(oq_flow_autotune_active) = true;

    if (!valid_mode()) {
      abort_with("ABORT: not CM100");
      return;
    }
    if (!flow_valid) {
      if (t_s_ >= cfg.baseline_wait_max_s) {
        abort_with("ABORT: NO_BASELINE_FLOW");
      } else {
        t_s_ += cfg.sample_time_s;
        publish("WAITING_FOR_FLOW");
      }
      return;
    }

    push_window(pv);
    const bool plateau = flow_plateau(pv, cfg.stable_band_lph);
    if (plateau && baseline_ready(cfg)) {
      const int u_step = step_size(cfg, second_step, pwm0);
      const int step1_u = pwm0 - clamp_runtime_ipwm(cfg, step1_pwm_step_);
      if (u_step < cfg.min_step || (second_step && u_step <= step1_u)) {
        ESP_LOGW("quatt.cm100.autotune",
                 "Autotune refused: insufficient step headroom (pwm0=%d min=%d headroom=%d required=%d)", pwm0,
                 cfg.pwm_min, pwm0 - cfg.pwm_min, cfg.min_step);
        abort_with("REFUSED: STEP_HEADROOM");
        return;
      }

      pv0_ = window_mean();
      pwm_step_ = clamp_runtime_ipwm(cfg, pwm0 - u_step);
      id(oq_flow_autotune_pwm) = pwm_step_;
      clear_window();
      response_count_ = 0;
      ss_confirm_cnt_ = 0;
      t_s_ = 0;
      state_ = second_step ? STATE_STEP2 : STATE_STEP1;
      ESP_LOGI("quatt.cm100.autotune", "%s stable -> step%d (pwm0=%d pwm_step=%d pv0=%.1f t=%ds)",
               second_step ? "Recovery" : "Baseline", second_step ? 2 : 1, pwm0, pwm_step_, pv0_, t);
      publish(second_step ? "STEP2" : "STEP");
      return;
    }

    if (t_s_ >= cfg.baseline_wait_max_s) {
      abort_with("ABORT: NO_BASELINE_FLOW");
      return;
    }
    t_s_ += cfg.sample_time_s;
    publish("SETTLING");
  }

  bool update_open_loop_measure(const RuntimeConfig& cfg, float pv, bool flow_valid, int pwm0, int pwm_step,
                                float* gain, float* tau_s) {
    if (!valid_mode()) {
      abort_with("ABORT: not CM100");
      return false;
    }
    if (!flow_valid) {
      abort_with("ABORT: FLOW_INVALID");
      return false;
    }

    if (response_count_ >= sizeof(response_samples_) / sizeof(response_samples_[0])) {
      ESP_LOGW("quatt.cm100.autotune", "Response history full; sample time=%ds", cfg.sample_time_s);
      abort_with("FAILED: INVALID_GAIN");
      return false;
    }
    response_samples_[response_count_++] = pv;
    push_window(pv);
    const bool quiet_window = steady_window(30.0f, 15.0f);
    const bool plateau = flow_plateau(pv, 30.0f);
    if (quiet_window && plateau) {
      pv_ss_ = window_mean();
      if (ss_confirm_cnt_ < 255) ss_confirm_cnt_++;
    } else {
      ss_confirm_cnt_ = 0;
    }

    const bool early_done = ss_confirm_cnt_ >= 2;
    if (!early_done && t_s_ < cfg.duration_s) {
      t_s_ += cfg.sample_time_s;
      return false;
    }
    if (isnan(pv_ss_) || ss_confirm_cnt_ < 2) {
      abort_with("ABORT: NO_STEADY_STATE");
      return false;
    }

    const float du = (float)(pwm0 - pwm_step);
    *gain = (pv_ss_ - pv0_) / du;
    if (!(*gain > 0.0f)) {
      abort_with("FAILED: INVALID_GAIN");
      return false;
    }
    pv63_time_s_ = response_t63_s(response_samples_, response_count_, pv0_, pv_ss_, cfg.sample_time_s);
    *tau_s = pv63_time_s_;
    if (isnan(*tau_s) || *tau_s < (float)cfg.sample_time_s) {
      *tau_s = cfg.tau_fallback_s;
    }
    return true;
  }

  void run_open_loop_step(const RuntimeConfig& cfg, float pv, bool flow_valid, bool second_step) {
    const int t = t_s_;
    const int pwm0 = clamp_runtime_ipwm(cfg, pwm0_);
    const int pwm_step = clamp_runtime_ipwm(cfg, pwm_step_);
    const float pv0 = pv0_;
    float gain = NAN;
    float tau_s = NAN;
    if (!update_open_loop_measure(cfg, pv, flow_valid, pwm0, pwm_step, &gain, &tau_s)) {
      if (state_ == STATE_ABORT) {
        ESP_LOGW("quatt.cm100.autotune", "Autotune step%d ended: %s (pv=%.1f t=%ds)", second_step ? 2 : 1,
                 id(oq_flow_autotune_status_value).c_str(), pv, t);
      }
      return;
    }

    if (!second_step) {
      step1_k_ = gain;
      step1_tau_ = tau_s;
      step1_pwm_step_ = pwm_step;
      id(oq_flow_autotune_pwm) = pwm0;
      ESP_LOGI("quatt.cm100.autotune",
               "Step1 captured (pwm0=%d step=%d pv0=%.1f pv_ss=%.1f t63=%.1f K1=%.3f tau1=%.0fs) -> recover", pwm0,
               pwm_step, pv0, pv_ss_, pv63_time_s_, gain, tau_s);
      clear_open_loop_memory();
      publish("RECOVERING");
      state_ = STATE_RECOVER;
      return;
    }

    start_validation(cfg, gain, tau_s, pwm0, pwm_step);
  }

  void start_validation(const RuntimeConfig& cfg, float k2, float tau2, int pwm0, int pwm_step2) {
    const float k1 = step1_k_;
    const float tau1 = step1_tau_;
    const float du1 = (float)(pwm0 - clamp_runtime_ipwm(cfg, step1_pwm_step_));
    const float du2 = (float)(pwm0 - pwm_step2);
    const float sp0 = id(oq_flow_setpoint_lph).state;
    if (!(k1 > 0.0f) || !(k2 > 0.0f) || isnan(tau1) || isnan(tau2)) {
      abort_with("FAILED: INVALID_GAIN");
      return;
    }

    const float rel_k = fabsf(k1 - k2) / fmaxf(fmaxf(k1, k2), 1e-3f);
    const float rel_tau = fabsf(tau1 - tau2) / fmaxf(fmaxf(tau1, tau2), (float)cfg.sample_time_s);
    const bool consistent = rel_k <= cfg.step_consistency_frac && rel_tau <= 0.35f;
    const float model_k = consistent ? (k1 * du1 + k2 * du2) / fmaxf(du1 + du2, 1.0f) : fmaxf(k1, k2);
    float model_tau = fmaxf(tau1, tau2);
    if (isnan(model_tau) || model_tau < (float)cfg.sample_time_s) model_tau = tau2;
    if (isnan(model_tau) || model_tau < (float)cfg.sample_time_s) {
      abort_with("FAILED: INVALID_GAIN");
      return;
    }

    float lambda_seed = fmaxf(cfg.validate_seed_lambda_frac * model_tau, cfg.validate_seed_min_lambda_s);
    float ti_seed = fmaxf(cfg.validate_seed_ti_frac * model_tau, cfg.validate_seed_min_ti_s);
    float kp_seed = model_tau / (model_k * (lambda_seed + (float)cfg.sample_time_s));
    float ki_seed = (ti_seed > 1e-3f) ? (kp_seed / ti_seed) : 0.0f;
    kp_seed = fminf(fmaxf(kp_seed, cfg.kp_min), cfg.kp_max);
    ki_seed = fminf(fmaxf(ki_seed, cfg.ki_min), cfg.ki_max);

    float validate_step_lph = roundf(cfg.validate_step_frac * sp0);
    validate_step_lph = fminf(fmaxf(validate_step_lph, cfg.validate_step_min_lph), cfg.validate_step_max_lph);
    if (!(sp0 + validate_step_lph > sp0)) {
      abort_with("FAILED: INVALID_VALIDATION_STEP");
      return;
    }

    saved_kp_ = id(oq_flow_kp).state;
    saved_ki_ = id(oq_flow_ki).state;
    seed_kp_ = kp_seed;
    seed_ki_ = ki_seed;
    validate_sp0_ = sp0;
    validate_sp1_ = sp0 + validate_step_lph;
    validate_attempt_ = 0;
    clear_validation_run_memory();

    set_number_value(id(oq_flow_kp), kp_seed);
    set_number_value(id(oq_flow_ki), ki_seed);
    set_number_value(id(oq_flow_setpoint_lph), validate_sp0_);
    id(oq_flow_autotune_active) = false;
    ESP_LOGI("quatt.cm100.autotune",
             "Step2 captured -> validation recovery (%s) step1 K=%.3f tau=%.0fs step2 K=%.3f tau=%.0fs combined K=%.3f "
             "tau=%.0fs seed=%.3f/%.4f sp=%.1f->%.1f",
             consistent ? "2STEP" : "2STEP (LIMITED)", k1, tau1, k2, tau2, model_k, model_tau, kp_seed, ki_seed, sp0,
             validate_sp1_);
    publish("VALIDATION_RECOVER");
    state_ = STATE_VALIDATE_RECOVER;
  }

  void run_validation_recover(const RuntimeConfig& cfg, float pv, bool flow_valid) {
    const float sp0 = validate_sp0_;
    const float sp1 = validate_sp1_;
    if (isnan(sp0) || isnan(sp1) || !(sp1 > sp0) || isnan(seed_kp_) || isnan(seed_ki_)) {
      finish_without_suggestion("FAILED: INVALID_VALIDATION_STATE");
      return;
    }
    if (!valid_mode()) {
      finish_without_suggestion("ABORT: not CM100");
      return;
    }
    if (!flow_valid) {
      finish_without_suggestion("ABORT: FLOW_INVALID");
      return;
    }

    set_number_value(id(oq_flow_setpoint_lph), sp0);
    const float step_lph = sp1 - sp0;
    const float recover_band = fmaxf(cfg.validate_band_floor_lph, cfg.validate_band_frac * step_lph);
    push_window(pv);

    const bool recovered = t_s_ >= 20 && steady_window(recover_band, recover_band) &&
                           fabsf(window_mean() - sp0) <= recover_band && pv <= sp1 - recover_band;
    validate_confirm_cnt_ = recovered ? validate_confirm_cnt_ + 1 : 0;
    if (validate_confirm_cnt_ >= 2) {
      clear_window();
      validate_confirm_cnt_ = 0;
      validate_settle_ready_ = false;
      validate_pre_overshoot_ = false;
      validate_peak_pv_ = NAN;
      t_s_ = 0;
      set_number_value(id(oq_flow_setpoint_lph), sp1);
      ESP_LOGI("quatt.cm100.autotune", "Validation baseline recovered (sp0=%.1f sp1=%.1f pv=%.1f band=%.1f)", sp0, sp1,
               pv, recover_band);
      publish("VALIDATING_SETTLING");
      state_ = STATE_VALIDATE;
      return;
    }

    if (t_s_ >= cfg.validate_recover_max_s) {
      ESP_LOGW("quatt.cm100.autotune", "Validation baseline failed (sp0=%.1f sp1=%.1f pv=%.1f band=%.1f t=%ds)", sp0,
               sp1, pv, recover_band, t_s_);
      finish_without_suggestion("FAILED: VALIDATION_BASELINE");
      return;
    }

    t_s_ += cfg.sample_time_s;
    publish("VALIDATION_RECOVER");
  }

  void run_validation(const RuntimeConfig& cfg, float pv, bool flow_valid) {
    const float sp0 = validate_sp0_;
    const float sp1 = validate_sp1_;
    if (isnan(sp0) || isnan(sp1) || !(sp1 > sp0)) {
      abort_with("FAILED: INVALID_VALIDATION_STATE");
      ESP_LOGW("quatt.cm100.autotune", "Autotune failed: invalid validation state (sp0=%.1f sp1=%.1f)", sp0, sp1);
      return;
    }
    if (!valid_mode()) {
      abort_with("ABORT: not CM100");
      return;
    }
    if (!flow_valid) {
      abort_with("ABORT: FLOW_INVALID");
      return;
    }

    const float step_lph = sp1 - sp0;
    const float settle_band = fmaxf(cfg.validate_band_floor_lph, cfg.validate_band_frac * step_lph);
    const float target_band = fmaxf(settle_band, 10.0f);
    const float overshoot_band = fmaxf(settle_band, cfg.validate_overshoot_frac * step_lph);
    push_window(pv);

    if (!validate_settle_ready_) {
      if (pv - sp1 > overshoot_band) {
        validate_pre_overshoot_ = true;
      }
      const bool ready_now = t_s_ >= 20 && steady_window(settle_band, settle_band) && fabsf(pv - sp1) <= settle_band;
      validate_confirm_cnt_ = ready_now ? validate_confirm_cnt_ + 1 : 0;
      if (validate_confirm_cnt_ >= 2 || t_s_ >= 60) {
        t_s_ = 0;
        clear_window();
        validate_confirm_cnt_ = 0;
        validate_peak_pv_ = NAN;
        validate_settle_ready_ = true;
        publish("VALIDATING");
      } else {
        t_s_ += cfg.sample_time_s;
        publish("VALIDATING_SETTLING");
      }
      return;
    }

    validate_peak_pv_ = isnan(validate_peak_pv_) ? pv : fmaxf(validate_peak_pv_, pv);
    const bool settled_now = steady_window(target_band, target_band) && fabsf(window_mean() - sp1) <= target_band;
    validate_confirm_cnt_ = settled_now ? validate_confirm_cnt_ + 1 : 0;
    const float overshoot = isnan(validate_peak_pv_) ? 0.0f : validate_peak_pv_ - sp1;
    const bool overshot = overshoot > overshoot_band;
    const bool active_long_enough = t_s_ >= cfg.validate_min_active_s;
    const bool settled = active_long_enough && validate_confirm_cnt_ >= 3 && !overshot && window_ready() &&
                         fabsf(window_mean() - sp1) <= target_band;
    const bool timed_out = t_s_ >= cfg.validate_duration_s;
    if (!settled && !timed_out) {
      t_s_ += cfg.sample_time_s;
      publish("VALIDATING");
      return;
    }

    finish_validation(cfg, sp1, target_band, overshoot, settled, timed_out);
  }

  bool retry_validation(const RuntimeConfig& cfg, const char* reason, float kp_scale, float ki_scale) {
    if (validate_attempt_ >= cfg.validate_max_retries) return false;
    const float raw_kp = seed_kp_ * kp_scale;
    const float raw_ki = seed_ki_ * ki_scale;
    seed_kp_ = fminf(fmaxf(raw_kp, cfg.kp_min), cfg.kp_max);
    seed_ki_ = fminf(fmaxf(raw_ki, cfg.ki_min), cfg.ki_max);
    validate_attempt_++;
    clear_validation_run_memory();
    set_number_value(id(oq_flow_kp), seed_kp_);
    set_number_value(id(oq_flow_ki), seed_ki_);
    set_number_value(id(oq_flow_setpoint_lph), validate_sp0_);
    ESP_LOGI("quatt.cm100.autotune", "%s -> validation retry %d/%d with Kp=%.3f Ki=%.4f", reason, validate_attempt_,
             cfg.validate_max_retries, seed_kp_, seed_ki_);
    publish(reason);
    state_ = STATE_VALIDATE_RECOVER;
    return true;
  }

  void finish_validation(const RuntimeConfig& cfg, float sp1, float target_band, float overshoot, bool settled,
                         bool timed_out) {
    const float seed_kp = seed_kp_;
    const float seed_ki = seed_ki_;
    if (isnan(seed_kp) || isnan(seed_ki)) {
      finish_without_suggestion("FAILED: INVALID_VALIDATION_STATE");
      return;
    }
    const float final_pv = window_ready() ? window_mean() : isnan(validate_peak_pv_) ? NAN : validate_peak_pv_;
    const float final_error = isnan(final_pv) ? NAN : sp1 - final_pv;
    if (!settled && timed_out && !isnan(final_error)) {
      if ((final_error > target_band) && retry_validation(cfg, "VALIDATION_RETRY: UNDER_TARGET", 1.05f, 1.35f)) {
        return;
      }
      if ((final_error < -target_band || overshoot > target_band || validate_pre_overshoot_) &&
          retry_validation(cfg, "VALIDATION_RETRY: OVERSHOOT", 0.85f, 0.85f)) {
        return;
      }
    }

    float scale = 1.0f;
    if (overshoot > (2.0f * target_band))
      scale = 0.80f;
    else if (overshoot > target_band)
      scale = 0.90f;
    if (timed_out && !settled && scale > 0.95f) scale = 0.95f;

    const float raw_kp = seed_kp * scale;
    const float raw_ki = seed_ki * scale;
    const float kp = fminf(fmaxf(raw_kp, cfg.kp_min), cfg.kp_max);
    const float ki = fminf(fmaxf(raw_ki, cfg.ki_min), cfg.ki_max);
    const bool clamped = fabsf(kp - raw_kp) > 1.0e-7f || fabsf(ki - raw_ki) > 1.0e-7f;
    const bool limited_validation = validate_pre_overshoot_ || !settled;
    const char* prefix = limited_validation ? "DONE (LIMITED)" : clamped ? "DONE (CLAMPED)" : "DONE (CLOSED-LOOP)";

    id(oq_flow_kp_suggested_value) = kp;
    id(oq_flow_ki_suggested_value) = ki;
    const float peak_pv = validate_peak_pv_;
    restore_live_settings();
    clear_window();
    validate_confirm_cnt_ = 0;
    validate_settle_ready_ = false;
    validate_pre_overshoot_ = false;

    char msg[260];
    snprintf(msg, sizeof(msg), "%s: seed=%.3f/%.4f -> Kp=%.3f Ki=%.4f peak=%.1f target=%.1f band=%.1f", prefix, seed_kp,
             seed_ki, kp, ki, peak_pv, sp1, target_band);
    publish(msg);
    clear_validation_memory();
    release_commissioning();
    reset();
  }

  void restore_live_settings() {
    if (!isnan(saved_kp_) && !isnan(saved_ki_)) {
      set_number_value(id(oq_flow_kp), saved_kp_);
      set_number_value(id(oq_flow_ki), saved_ki_);
    }
    if (!isnan(validate_sp0_)) {
      set_number_value(id(oq_flow_setpoint_lph), validate_sp0_);
    }
  }

  void clear_validation_memory() {
    saved_kp_ = NAN;
    saved_ki_ = NAN;
    seed_kp_ = NAN;
    seed_ki_ = NAN;
    validate_sp0_ = NAN;
    validate_sp1_ = NAN;
    validate_peak_pv_ = NAN;
    validate_attempt_ = 0;
  }

  void finish_without_suggestion(const char* status) {
    id(oq_flow_autotune_active) = false;
    restore_live_settings();
    clear_validation_memory();
    clear_window();
    ss_confirm_cnt_ = 0;
    validate_confirm_cnt_ = 0;
    validate_settle_ready_ = false;
    validate_pre_overshoot_ = false;
    release_commissioning();
    publish(status);
    reset();
  }

  void finish_abort(const RuntimeConfig& cfg) {
    id(oq_flow_autotune_active) = false;
    id(oq_flow_autotune_pwm) = clamp_runtime_ipwm(cfg, (int)id(oq_flow_last_pwm));
    restore_live_settings();
    clear_validation_memory();
    clear_window();
    ss_confirm_cnt_ = 0;
    validate_confirm_cnt_ = 0;
    validate_settle_ready_ = false;
    validate_pre_overshoot_ = false;
    ESP_LOGW("quatt.cm100.autotune", "Autotune ended: %s; restoring last PWM=%d",
             id(oq_flow_autotune_status_value).c_str(), (int)id(oq_flow_last_pwm));
    release_commissioning();
    // Preserve the terminal reason published by abort_with() or the explicit stop request.
    reset();
  }
};

inline FlowAutotuneRuntime& runtime() {
  static FlowAutotuneRuntime instance;
  return instance;
}

}  // namespace oq_flow_autotune
