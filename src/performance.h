#pragma once
#include <QtCore>

// Local timing diagnostics: never include answers, subject IDs or credentials.
class PerformanceSpan {
public:
  explicit PerformanceSpan(const char *name) : name(name) { timer.start(); }
  ~PerformanceSpan() {
    const qint64 elapsed = timer.elapsed();
    if (elapsed >= 100 || qEnvironmentVariableIsSet("MOJI_NOOK_PROFILE"))
      qInfo().nospace() << "moji-nook.performance " << name << " " << elapsed
                        << "ms";
  }

private:
  const char *name;
  QElapsedTimer timer;
};
