#include "ui.h"
PracticeClock::PracticeClock(QObject *parent) : QObject(parent) {
  timer.setSingleShot(true);
  timer.setTimerType(Qt::PreciseTimer);
  connect(&timer, &QTimer::timeout, this, &PracticeClock::offer);
}
// An explicit request, such as Practice now, runs even during quiet hours.
void PracticeClock::request() {
  if (paused || pending)
    return;
  snoozed = false;
  timer.stop();
  pending = true;
  sessionTotal = qBound(1, sessionSize, maxSessionSize);
  position = 1;
  sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  emit due();
}
void PracticeClock::offer() {
  refreshQuiet();
  if (!quiet)
    request();
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
  snoozed = false;
  startInterval();
}
void PracticeClock::setPaused(bool value) {
  paused = value;
  snoozed = false;
  if (paused)
    timer.stop();
  else
    startInterval();
}
void PracticeClock::refreshQuiet() {
  const bool value = quietHours.contains(QDateTime::currentDateTime());
  if (value == quiet)
    return;
  quiet = value;
  snoozed = false;
  if (quiet)
    timer.stop();
  else
    startInterval();
  emit quietChanged(quiet);
}
void PracticeClock::startInterval() {
  if (!paused && !quiet && !pending)
    timer.start(intervalMs);
}
