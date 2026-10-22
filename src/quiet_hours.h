#pragma once
#include <QDateTime>
#include <QStringList>
#include <QVector>
#include <optional>

// A weekly window with no scheduled cards. Bit 0 of days is Monday, bit 6 is
// Sunday; a window belongs to the day it starts, so start > end ends the next
// day. A window with no days or with start == end never applies.
struct QuietWindow {
  quint8 days = 0;
  QTime start, end;
};

struct QuietSchedule {
  QVector<QuietWindow> windows;
  // Start inclusive, end exclusive, on local wall-clock time.
  bool contains(const QDateTime &local) const;
  // End of the quiet period around local, merging overlapping and adjacent
  // windows. Empty when local is not quiet or the schedule never ends.
  std::optional<QDateTime> endAfter(const QDateTime &local) const;
  // Entries look like "1111100,09:00,17:00"; malformed entries are dropped.
  static QuietSchedule parse(const QStringList &entries);
  QStringList serialize() const;
};
