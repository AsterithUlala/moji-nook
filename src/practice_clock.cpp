#include "ui.h"
PracticeClock::PracticeClock(QObject *parent) : QObject(parent) {
  timer.setSingleShot(true);
  timer.setTimerType(Qt::PreciseTimer);
  connect(&timer, &QTimer::timeout, this, &PracticeClock::offer);
}
// An explicit request, such as Practice now, runs even during quiet hours.
void PracticeClock::request(bool explicitRequest) {
  if (paused || pending)
    return;
  explicitSession = explicitRequest;
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
    request(false);
}
void PracticeClock::advance() {
  if (!pending || paused)
    return;
  // A scheduled session that quiet hours interrupted ends after the card in use.
  if (position >= sessionTotal || (quiet && !explicitSession)) {
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
  if (quiet)
    timer.stop();
  else
    startInterval();
  emit quietChanged(quiet);
}
void PracticeClock::snooze(int ms) {
  snoozed = true;
  snoozeUntil = QDateTime::currentDateTimeUtc().addMSecs(ms);
  startInterval();
}
void PracticeClock::startInterval() {
  if (paused || quiet || pending)
    return;
  const qint64 left =
      snoozed ? QDateTime::currentDateTimeUtc().msecsTo(snoozeUntil) : 0;
  timer.start(left > 0 ? int(qMin<qint64>(left, INT_MAX)) : intervalMs);
}
