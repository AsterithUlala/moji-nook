#include "ui.h"
PracticeClock::PracticeClock(QObject *parent) : QObject(parent) {
  timer.setSingleShot(true);
  timer.setTimerType(Qt::PreciseTimer);
  connect(&timer, &QTimer::timeout, this, &PracticeClock::request);
}
void PracticeClock::request() {
  if (paused || pending)
    return;
  timer.stop();
  pending = true;
  sessionTotal = qBound(1, sessionSize, maxSessionSize);
  position = 1;
  sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  emit due();
}
void PracticeClock::advance() {
  if (!pending || paused)
    return;
  if (position >= sessionTotal) {
    complete();
    return;
  }
  ++position;
  emit due();
}
void PracticeClock::complete() {
  pending = false;
  if (!paused)
    timer.start(intervalMs);
}
void PracticeClock::setPaused(bool value) {
  paused = value;
  if (paused)
    timer.stop();
  else if (!pending)
    timer.start(intervalMs);
}
