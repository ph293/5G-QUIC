#ifndef SDNMEC_PREDICTOR_H
#define SDNMEC_PREDICTOR_H

#include <algorithm>

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

} // namespace sdnmec
#endif
