#include "quiet_hours.h"
#include <QTimeZone>

namespace {
constexpr qint64 dayMs = 86400000;
struct Span {
  qint64 start, end;
};
// Occurrences starting on `count` days from `first`, in ms from first's midnight.
QVector<Span> spans(const QVector<QuietWindow> &windows, QDate first, int count) {
  QVector<Span> result;
  for (int day = 0; day < count; ++day) {
    const int bit = first.addDays(day).dayOfWeek() - 1;
    for (const auto &w : windows) {
      if (!(w.days >> bit & 1) || w.start == w.end)
        continue;
      const qint64 start = day * dayMs + w.start.msecsSinceStartOfDay();
      qint64 end = day * dayMs + w.end.msecsSinceStartOfDay();
      if (w.end < w.start)
        end += dayMs;
      result.append({start, end});
    }
  }
  return result;
}
} // namespace

bool QuietSchedule::contains(const QDateTime &local) const {
  const qint64 at = dayMs + local.time().msecsSinceStartOfDay();
  for (const auto &span : spans(windows, local.date().addDays(-1), 2))
    if (span.start <= at && at < span.end)
      return true;
  return false;
}

std::optional<QDateTime> QuietSchedule::endAfter(const QDateTime &local) const {
  const QDate first = local.date().addDays(-1);
  const auto all = spans(windows, first, 9);
  const qint64 from = dayMs + local.time().msecsSinceStartOfDay();
  // Covering a whole week means the weekly schedule covers every week.
  const qint64 limit = from + 7 * dayMs;
  qint64 at = from;
  for (bool extended = true; extended && at < limit;) {
    extended = false;
    for (const auto &span : all)
      if (span.start <= at && at < span.end) {
        at = span.end;
        extended = true;
      }
  }
  if (at == from || at >= limit)
    return std::nullopt;
  return QDateTime(first.addDays(at / dayMs),
                   QTime::fromMSecsSinceStartOfDay(int(at % dayMs)),
                   local.timeRepresentation());
}

QuietSchedule QuietSchedule::parse(const QStringList &entries) {
  QuietSchedule schedule;
  for (const auto &entry : entries) {
    const auto parts = entry.split(',');
    if (parts.size() != 3 || parts[0].size() != 7)
      continue;
    QuietWindow window;
    bool ok = true;
    for (int i = 0; i < 7; ++i) {
      if (parts[0][i] == '1')
        window.days |= 1 << i;
      else if (parts[0][i] != '0')
        ok = false;
    }
    window.start = QTime::fromString(parts[1], "HH:mm");
    window.end = QTime::fromString(parts[2], "HH:mm");
    if (ok && window.start.isValid() && window.end.isValid())
      schedule.windows.append(window);
  }
  return schedule;
}

QStringList QuietSchedule::serialize() const {
  QStringList entries;
  for (const auto &w : windows) {
    QString days;
    for (int i = 0; i < 7; ++i)
      days += (w.days >> i & 1) ? '1' : '0';
    entries << days + ',' + w.start.toString("HH:mm") + ',' +
                   w.end.toString("HH:mm");
  }
  return entries;
}
