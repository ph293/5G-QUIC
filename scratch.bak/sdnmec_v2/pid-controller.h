#ifndef SDNMEC_PID_CONTROLLER_H
#define SDNMEC_PID_CONTROLLER_H

#include <algorithm>

namespace sdnmec {

// Discrete PID with conditional anti-windup (integral clamped to [0, iMax]).
// Positive error (CPU above target) -> positive output -> throttle harder.
class PidController {
public:
  PidController(double kp = 0.8, double ki = 0.0, double kd = 0.1, double iMax = 1.0)
    : m_kp(kp), m_ki(ki), m_kd(kd), m_iMax(iMax),
      m_i(0.0), m_prev(0.0), m_first(true) {}

  double Step(double err, double dt)
  {
    if (dt <= 0.0) dt = 1e-3;
    m_i = std::min(m_iMax, std::max(0.0, m_i + err * dt));
    double d = m_first ? 0.0 : (err - m_prev) / dt;
    m_first = false;
    m_prev = err;
    return m_kp * err + m_ki * m_i + m_kd * d;
  }

  void Reset() { m_i = 0.0; m_prev = 0.0; m_first = true; }

private:
  double m_kp, m_ki, m_kd, m_iMax, m_i, m_prev;
  bool m_first;
};

} // namespace sdnmec
#endif
