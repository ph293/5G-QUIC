#ifndef SDNMEC_PREDICTOR_H
#define SDNMEC_PREDICTOR_H

#include <algorithm>
#include <cmath>
#include "lrsnn-weights.h"

namespace sdnmec {

// Interface for the CPU-pressure predictor.
// Replace EwmaTrendPredictor with an LRSNN class (weights trained offline in
// Python/snnTorch, forward pass implemented here) without touching the controller.
class CpuPredictor {
public:
  virtual ~CpuPredictor() {}
  virtual void Observe(double cpuUtil, double pps) = 0;
  virtual double Forecast() const = 0;   // predicted CPU utilisation, next interval
};

// Stand-in predictor: EWMA level plus a trend term. Used to validate the
// closed loop before the LRSNN is plugged in (also a useful baseline).
class EwmaTrendPredictor : public CpuPredictor {
public:
  EwmaTrendPredictor(double a = 0.6, double b = 0.3)
    : m_a(a), m_b(b), m_level(0.0), m_prev(0.0), m_init(false) {}

  void Observe(double cpuUtil, double /*pps*/) override
  {
    if (!m_init) { m_level = cpuUtil; m_init = true; }
    else         { m_level = m_a * cpuUtil + (1.0 - m_a) * m_level + m_b * (cpuUtil - m_prev); }
    m_prev = cpuUtil;
  }

  double Forecast() const override { return std::min(2.0, std::max(0.0, m_level)); }

private:
  double m_a, m_b, m_level, m_prev;
  bool m_init;
};

// Unclamped EWMA + trend predictor for any non-negative signal (offered demand,
// byte rate, ...). Same recurrence as EwmaTrendPredictor but without the [0,2] clamp.
// Replace with an LRSNN-based CpuPredictor later.
class TrendPredictor : public CpuPredictor {
public:
  TrendPredictor(double a = 0.6, double b = 0.3)
    : m_a(a), m_b(b), m_level(0.0), m_prev(0.0), m_init(false) {}

  void Observe(double v, double /*unused*/) override
  {
    if (!m_init) { m_level = v; m_init = true; }
    else         { m_level = m_a * v + (1.0 - m_a) * m_level + m_b * (v - m_prev); }
    m_prev = v;
  }

  double Forecast() const override { return std::max(0.0, m_level); }

private:
  double m_a, m_b, m_level, m_prev;
  bool m_init;
};

// Holt linear (level + trend) forecaster. Forecast(h) predicts h control intervals ahead.
// This is the stand-in that an LRSNN predictor has to beat.
class HoltPredictor {
public:
  HoltPredictor(double alpha = 0.8, double beta = 0.5)
    : m_a(alpha), m_b(beta), m_lv(0.0), m_tr(0.0), m_init(false) {}

  void Observe(double v)
  {
    if (!m_init) { m_lv = v; m_tr = 0.0; m_init = true; return; }
    double nl = m_a * v + (1.0 - m_a) * (m_lv + m_tr);
    m_tr = m_b * (nl - m_lv) + (1.0 - m_b) * m_tr;
    m_lv = nl;
  }

  double Forecast(double h) const { return std::max(0.0, m_lv + h * m_tr); }

private:
  double m_a, m_b, m_lv, m_tr;
  bool m_init;
};

// LRSNN forecaster: a fixed random spiking reservoir (leaky integrate-and-fire neurons,
// soft reset) whose spike counts and membrane potentials feed a linear readout.
// The readout is trained offline (lrsnn_train.py). With lagrangian=true the readout was trained
// by Lagrangian relaxation: minimise forecast error subject to a cap on under-prediction,
// solved by dual ascent on the multiplier. With lagrangian=false it is plain (weighted) ridge.
// No random numbers are used here, so ns-3 seeds are not disturbed.
class LrsnnPredictor {
public:
  explicit LrsnnPredictor(bool lagrangian = true) : m_lag(lagrangian), m_nobs(0)
  {
    for (int i = 0; i < lrsnn::kN; ++i) { m_v[i] = 0.0; m_sp[i] = 0.0; m_c1[i] = 0.0; m_c0[i] = 0.0; }
  }

  void Observe(double demand)
  {
    const int N = lrsnn::kN;
    double dd = std::min(std::max(demand, 0.0), lrsnn::kDMax);
    double x = std::sqrt(dd / lrsnn::kDMax);
    double cur[lrsnn::kN], cnt[lrsnn::kN], ns[lrsnn::kN];
    for (int i = 0; i < N; ++i) { cur[i] = lrsnn::kIw[i] * x + lrsnn::kIb[i]; cnt[i] = 0.0; }
    for (int t = 0; t < lrsnn::kSteps; ++t) {
      for (int i = 0; i < N; ++i) {
        double rec = 0.0;
        const double* row = &lrsnn::kW[i * N];
        for (int j = 0; j < N; ++j) rec += row[j] * m_sp[j];
        double v = lrsnn::kBeta[i] * m_v[i] + cur[i] + rec;
        double s = (v >= lrsnn::kTheta) ? 1.0 : 0.0;
        m_v[i] = v - lrsnn::kTheta * s;
        ns[i] = s;
        cnt[i] += s;
      }
      for (int i = 0; i < N; ++i) m_sp[i] = ns[i];
    }
    for (int i = 0; i < N; ++i) {
      m_f[i]         = cnt[i] / lrsnn::kSteps;   // spike rate this interval
      m_f[N + i]     = m_c1[i];                  // spike rate previous interval
      m_f[2 * N + i] = m_v[i];                   // membrane potential
      m_c1[i] = m_f[i];
    }
    m_f[3 * N] = 1.0;
    m_nobs++;
  }

  // h = number of control intervals ahead (1..3 supported; others are clamped)
  double Forecast(double h) const
  {
    if (m_nobs == 0) return 0.0;
    int hi = static_cast<int>(std::floor(h + 0.5));
    if (hi < 1) hi = 1;
    if (hi > 3) hi = 3;
    const double* w = m_lag ? (hi == 1 ? lrsnn::kRO_lag_h1 : hi == 2 ? lrsnn::kRO_lag_h2 : lrsnn::kRO_lag_h3)
                            : (hi == 1 ? lrsnn::kRO_ridge_h1 : hi == 2 ? lrsnn::kRO_ridge_h2 : lrsnn::kRO_ridge_h3);
    double y = 0.0;
    for (int k = 0; k < lrsnn::kFeat; ++k) y += w[k] * m_f[k];
    return std::max(0.0, y);
  }

private:
  bool m_lag;
  int m_nobs;
  double m_v[lrsnn::kN], m_sp[lrsnn::kN], m_c1[lrsnn::kN], m_c0[lrsnn::kN];
  double m_f[lrsnn::kFeat];
};

} // namespace sdnmec
#endif
