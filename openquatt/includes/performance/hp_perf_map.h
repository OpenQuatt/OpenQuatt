#pragma once
// V1/V1.5 performance map and generation-aware dispatch to the reconstructed
// V2 CiC 4.2.0 heating model.
// Provides thermisch vermogen (W) en COP als functie van (level, Tamb, Tsup).
#include <cmath>

#include "hp_perf_map_v2.h"

namespace oq_perf_v1 {

static constexpr int N_AMB = 5;
static constexpr int N_SUP = 3;
static constexpr int N_LVL = 10;  // levels 1..10

static constexpr float T_amb_bp[N_AMB] = {-15.00f, -7.00f, 2.00f, 7.00f, 12.00f};
static constexpr float T_sup_bp[N_SUP] = {35.00f, 45.00f, 55.00f};

static constexpr float P_th_W[N_AMB][N_SUP][N_LVL] = {
    {
        {792.39f, 1054.23f, 1358.42f, 1547.65f, 1741.89f, 1941.17f, 2111.07f, 2354.79f, 2569.14f, 2751.61f},
        {750.50f, 1023.67f, 1340.45f, 1537.22f, 1739.02f, 1945.85f, 2122.04f, 2374.58f, 2596.48f, 2785.24f},
        {285.03f, 569.53f, 898.90f, 1103.22f, 1312.57f, 1526.95f, 1709.44f, 1970.78f, 2200.24f, NAN},
    },
    {
        {1119.63f, 1491.69f, 1918.35f, 2181.05f, 2448.78f, 2721.53f, 2952.66f, 3282.11f, 3569.94f, 3813.64f},
        {998.50f, 1381.88f, 1821.13f, 2091.38f, 2366.66f, 2646.96f, 2884.39f, 3222.65f, 3518.03f, 3768.03f},
        {453.78f, 848.49f, 1300.33f, 1578.13f, 1860.96f, 2148.82f, 2392.54f, 2739.61f, NAN, NAN},
    },
    {
        {1675.91f, 2171.97f, 2736.40f, 3081.76f, 3432.15f, 3787.56f, 4087.58f, 4513.47f, 4883.97f, 5196.55f},
        {1465.62f, 1973.00f, 2550.02f, 2902.94f, 3260.88f, 3623.85f, 3930.16f, 4364.86f, 4742.91f, 5061.78f},
        {831.75f, 1350.46f, 1940.07f, 2300.54f, 2666.03f, 3036.55f, 3349.15f, 3792.67f, NAN, NAN},
    },
    {
        {2071.03f, 2635.97f, 3276.94f, 3668.23f, 4064.54f, 4465.88f, 4804.17f, 5283.63f, 5700.05f, 6050.91f},
        {1811.21f, 2387.48f, 3041.04f, 3439.87f, 3843.74f, 4252.63f, 4597.21f, 5085.49f, 5509.46f, 5866.61f},
        {1127.81f, 1715.41f, 2381.55f, 2787.94f, 3199.36f, 3615.80f, 3966.68f, 4463.77f, NAN, NAN},
    },
    {
        {2527.63f, 3161.45f, 3878.96f, 4316.17f, 4758.41f, 5205.67f, 5582.23f, 6115.28f, NAN, NAN},
        {2218.28f, 2863.43f, 3593.53f, 4038.29f, 4488.08f, 4942.89f, 5325.75f, 5867.60f, NAN, NAN},
        {1485.35f, 2141.83f, 2884.52f, 3336.83f, 3794.17f, 4256.54f, 4645.68f, 5196.35f, NAN, NAN},
    },
};
static constexpr float COP[N_AMB][N_SUP][N_LVL] = {
    {
        {1.78f, 1.98f, 2.15f, 2.22f, 2.28f, 2.30f, 2.31f, 2.30f, 2.26f, 2.21f},
        {1.03f, 1.31f, 1.57f, 1.70f, 1.80f, 1.89f, 1.94f, 1.99f, 2.00f, 2.00f},
        {0.22f, 0.59f, 0.94f, 1.12f, 1.28f, 1.41f, 1.51f, 1.62f, 1.69f, NAN},
    },
    {
        {2.73f, 2.88f, 2.99f, 3.02f, 3.04f, 3.03f, 3.01f, 2.95f, 2.87f, 2.79f},
        {1.80f, 2.03f, 2.23f, 2.31f, 2.38f, 2.43f, 2.45f, 2.45f, 2.43f, 2.40f},
        {0.81f, 1.12f, 1.41f, 1.55f, 1.67f, 1.77f, 1.83f, 1.90f, NAN, NAN},
    },
    {
        {3.96f, 4.04f, 4.08f, 4.07f, 4.04f, 3.99f, 3.94f, 3.83f, 3.71f, 3.60f},
        {2.82f, 2.98f, 3.11f, 3.16f, 3.18f, 3.19f, 3.17f, 3.13f, 3.06f, 2.99f},
        {1.62f, 1.87f, 2.08f, 2.18f, 2.26f, 2.32f, 2.35f, 2.37f, NAN, NAN},
    },
    {
        {4.71f, 4.76f, 4.75f, 4.72f, 4.67f, 4.60f, 4.52f, 4.39f, 4.25f, 4.11f},
        {3.45f, 3.58f, 3.67f, 3.69f, 3.70f, 3.68f, 3.64f, 3.57f, 3.48f, 3.39f},
        {2.14f, 2.35f, 2.53f, 2.61f, 2.66f, 2.69f, 2.71f, 2.70f, NAN, NAN},
    },
    {
        {5.50f, 5.52f, 5.48f, 5.42f, 5.35f, 5.25f, 5.15f, 4.99f, NAN, NAN},
        {4.13f, 4.23f, 4.28f, 4.28f, 4.26f, 4.21f, 4.16f, 4.06f, NAN, NAN},
        {2.71f, 2.89f, 3.02f, 3.08f, 3.11f, 3.12f, 3.11f, 3.07f, NAN, NAN},
    },
};

static inline int find_interval(const float* bp, int n, float x) {
  if (x <= bp[0]) return 0;
  if (x >= bp[n - 1]) return n - 2;
  for (int i = 0; i < n - 1; i++) {
    if (x >= bp[i] && x <= bp[i + 1]) return i;
  }
  return n - 2;
}

static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

static inline float bilerp(float q11, float q21, float q12, float q22, float tx, float ty) {
  // x: amb, y: sup
  float r1 = lerp(q11, q21, tx);
  float r2 = lerp(q12, q22, tx);
  return lerp(r1, r2, ty);
}

static inline float interp_3d(const float data[N_AMB][N_SUP][N_LVL], int level, float Tamb, float Tsup) {
  if (level <= 0) return 0.0f;  // level 0 => off
  if (level > N_LVL) level = N_LVL;
  const int li = level - 1;

  int ai = find_interval(T_amb_bp, N_AMB, Tamb);
  int si = find_interval(T_sup_bp, N_SUP, Tsup);

  float ax0 = T_amb_bp[ai];
  float ax1 = T_amb_bp[ai + 1];
  float sy0 = T_sup_bp[si];
  float sy1 = T_sup_bp[si + 1];

  float tx = (ax1 == ax0) ? 0.0f : (Tamb - ax0) / (ax1 - ax0);
  float ty = (sy1 == sy0) ? 0.0f : (Tsup - sy0) / (sy1 - sy0);
  if (tx < 0) tx = 0;
  if (tx > 1) tx = 1;
  if (ty < 0) ty = 0;
  if (ty > 1) ty = 1;

  float q11 = data[ai][si][li];
  float q21 = data[ai + 1][si][li];
  float q12 = data[ai][si + 1][li];
  float q22 = data[ai + 1][si + 1][li];

  // If any cell is missing (NaN), return NaN so the caller can fallback/skip.
  if (std::isnan(q11) || std::isnan(q21) || std::isnan(q12) || std::isnan(q22)) return NAN;

  return bilerp(q11, q21, q12, q22, tx, ty);
}

static inline float interp_power_th_w(int level, float Tamb, float Tsup) {
  return interp_3d(P_th_W, level, Tamb, Tsup);
}

static inline float interp_cop(int level, float Tamb, float Tsup) { return interp_3d(COP, level, Tamb, Tsup); }

static inline float interp_power_el_w(int level, float Tamb, float Tsup, float cop_fallback = 3.0f) {
  float pth = interp_power_th_w(level, Tamb, Tsup);
  if (std::isnan(pth) || pth <= 0.0f) return 0.0f;
  float cop = interp_cop(level, Tamb, Tsup);
  if (std::isnan(cop) || cop <= 0.1f) cop = cop_fallback;
  return pth / cop;
}

}  // namespace oq_perf_v1

namespace oq_perf {

static inline bool uses_v2_map() {
  if (!id(hp_generation).has_state()) {
    return false;
  }
  return id(hp_generation).current_option() == "V2";
}

static inline float interp_power_th_w(int level, float Tamb, float Tsup) {
  if (uses_v2_map()) {
    return oq_v2_model::predict_heating(oq_v2_model::reference_frequency_hz(level), Tamb, Tsup).pth_w;
  }
  return oq_perf_v1::interp_power_th_w(level, Tamb, Tsup);
}

static inline float interp_cop(int level, float Tamb, float Tsup) {
  if (uses_v2_map()) {
    return oq_v2_model::predict_heating(oq_v2_model::reference_frequency_hz(level), Tamb, Tsup).cop;
  }
  return oq_perf_v1::interp_cop(level, Tamb, Tsup);
}

static inline float interp_power_el_w(int level, float Tamb, float Tsup, float cop_fallback = 3.0f) {
  if (uses_v2_map()) {
    return oq_v2_model::predict_heating(oq_v2_model::reference_frequency_hz(level), Tamb, Tsup).pel_w;
  }
  return oq_perf_v1::interp_power_el_w(level, Tamb, Tsup, cop_fallback);
}

}  // namespace oq_perf
