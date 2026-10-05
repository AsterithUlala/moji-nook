#include "progression.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct TileData {
  ProgressTile tile;
  bool usableReading = false, usableMeaning = false;
  int development = 0;
  int readingClears = 0, meaningClears = 0;
};
struct Skill {
  int successes = 0, clears = 0;
  bool challenge = false, complete = false;
  double progress = 0;
};
std::optional<QDateTime> preciseInstant(const QString &text) {
  static const QRegularExpression preciseTime(
      "T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\\.[0-9]+)?(?:Z|[+-][0-9]{2}:[0-9]{2})$");
  const auto instant = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (instant.isValid() && preciseTime.match(text).hasMatch())
    return instant;
  return std::nullopt;
}
bool validDay(const QString &text) {
  const auto date = QDate::fromString(text, Qt::ISODate);
  return date.isValid() && date.toString(Qt::ISODate) == text;
}
QString gradingDay(const QJsonObject &metadata, const QString &recordedDay,
                   const std::optional<QDateTime> &gradedAt) {
  const QString explicitDay = metadata["graded_local_day"].toString();
  if (validDay(explicitDay))
    return explicitDay;
  if (gradedAt) {
    const QTimeZone zone(metadata["timezone"].toString().toUtf8());
    if (zone.isValid())
      return gradedAt->toTimeZone(zone).date().toString(Qt::ISODate);
    const auto offset = metadata["utc_offset_seconds"];
    const double seconds = offset.toDouble();
    if (offset.isDouble() && std::isfinite(seconds) &&
        seconds == std::floor(seconds) && qAbs(seconds) <= 14 * 3600)
      return gradedAt->toOffsetFromUtc(int(seconds))
          .date()
          .toString(Qt::ISODate);
  }
  return recordedDay;
}
QString firstText(const QJsonValue &value) {
  for (const auto &entry : value.toArray())
    if (entry.isString() && !entry.toString().trimmed().isEmpty())
      return entry.toString();
  return {};
}
QString firstText(const QStringList &values) {
  for (const auto &value : values)
    if (!value.trimmed().isEmpty())
      return value;
  return {};
}
TileData decode(int id, const QByteArray &bytes) {
  const auto o = QJsonDocument::fromJson(bytes).object();
  TileData d;
  auto &t = d.tile;
  t.id = id;
  t.characters = o["characters"].toString();
  t.reading = o["reading"].toString();
  t.meaning = o["meaning"].toString();
  t.source = o["source"].toString();
  t.subjectKind = o["subject_kind"].toString();
  t.settled = o["settled"].toBool();
  t.readingChallenge = o["reading_challenge"].toBool();
  t.meaningChallenge = o["meaning_challenge"].toBool();
  t.readingSuccesses = o["reading_successes"].toInt();
  t.meaningSuccesses = o["meaning_successes"].toInt();
  t.progress = o["progress"].toDouble();
  t.lastAttempt = qint64(o["last_attempt"].toDouble());
  d.usableReading = o["usable_reading"].toBool();
  d.usableMeaning = o["usable_meaning"].toBool();
  d.development = o["development"].toInt();
  d.readingClears = o["reading_clears"].toInt();
  d.meaningClears = o["meaning_clears"].toInt();
  t.eligible = d.usableReading && d.usableMeaning;
  t.phase = t.settled           ? TilePhase::Settled
            : d.development > 0 ? TilePhase::Growing
                                : TilePhase::Seen;
  return d;
}
QByteArray encode(const TileData &d) {
  const auto &t = d.tile;
  return QJsonDocument(QJsonObject{{"characters", t.characters},
                                   {"reading", t.reading},
                                   {"meaning", t.meaning},
                                   {"source", t.source},
                                   {"subject_kind", t.subjectKind},
                                   {"settled", t.settled},
                                   {"reading_challenge", t.readingChallenge},
                                   {"meaning_challenge", t.meaningChallenge},
                                   {"reading_successes", t.readingSuccesses},
                                   {"meaning_successes", t.meaningSuccesses},
                                   {"progress", t.progress},
                                   {"last_attempt", double(t.lastAttempt)},
                                   {"usable_reading", d.usableReading},
                                   {"usable_meaning", d.usableMeaning},
                                   {"development", d.development},
                                   {"reading_clears", d.readingClears},
                                   {"meaning_clears", d.meaningClears}})
      .toJson(QJsonDocument::Compact);
}
Skill evaluate(const QVector<QPair<qint64, bool>> &days) {
  Skill s;
  QVector<bool> recent;
  qint64 earliest = std::numeric_limits<qint64>::max();
  qint64 latest = std::numeric_limits<qint64>::min();
  qint64 previousSuccess = std::numeric_limits<qint64>::min();
  qint64 gap = 0;
  int clearing = 0;
  for (const auto &day : days) {
    if (day.second) {
      ++s.successes;
      if (day.first != std::numeric_limits<qint64>::min()) {
        earliest = qMin(earliest, day.first);
        latest = qMax(latest, day.first);
        if (previousSuccess != std::numeric_limits<qint64>::min())
          gap = qMax(gap, day.first - previousSuccess);
      }
      // Unknown success timing breaks adjacency; it cannot be skipped to
      // invent a week-long gap between the surrounding successful encounters.
      previousSuccess = day.first;
    }
    if (s.challenge) {
      if (day.second && ++clearing >= 3) {
        s.challenge = false;
        ++s.clears;
        clearing = 0;
        // Resolved misses cannot immediately recreate the mark.
        recent.clear();
      }
    } else {
      recent.append(day.second);
      if (recent.size() > 6)
        recent.removeFirst();
      if (std::count(recent.cbegin(), recent.cend(), false) >= 3) {
        s.challenge = true;
        clearing = 0;
      }
    }
  }
  constexpr qint64 dayMs = 86400000;
  const qint64 span = latest > earliest ? latest - earliest : 0;
  s.complete = s.successes >= 3 && span >= 14 * dayMs && gap >= 7 * dayMs;
  s.progress =
      (qMin(1.0, s.successes / 3.0) + qMin(1.0, double(span) / (14 * dayMs)) +
       qMin(1.0, double(gap) / (7 * dayMs))) /
      3.0;
  return s;
}
bool consistencyOccasion(int days) {
  return days == 7 || days == 20 || days == 50 ||
         (days >= 100 && days % 100 == 0);
}
} // namespace

Progression::Progression(const QSqlDatabase &database) : db_(database) {}

std::optional<ProgressTile> Progression::tile(int id) const {
  for (const auto &t : state_.tiles)
    if (t.id == id)
      return t;
  return std::nullopt;
}
QVector<ProgressNotice> Progression::takeNotices() {
  QVector<ProgressNotice> result;
  result.swap(notices_);
  return result;
}

bool Progression::refresh(const QVector<Subject> &pool, bool notify) {
  error.clear();
  if (!db_.transaction()) {
    error = db_.lastError().text();
    return false;
  }
  auto fail = [&](const QSqlQuery &query) {
    error = query.lastError().text();
    db_.rollback();
    return false;
  };
  QSqlQuery q(db_);
  for (const auto &sql :
       {"CREATE TABLE IF NOT EXISTS progression_checkpoint (singleton INTEGER "
        "PRIMARY KEY CHECK(singleton=1), attempt_id INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS progression_tiles (subject_id INTEGER "
        "PRIMARY KEY, data_json TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS progression_daily (subject_id INTEGER NOT "
        "NULL, dimension TEXT NOT NULL, local_day TEXT NOT NULL, attempt_id "
        "INTEGER NOT NULL, instant INTEGER NOT NULL, correct INTEGER NOT NULL, "
        "PRIMARY KEY(subject_id,dimension,local_day))",
        "CREATE TABLE IF NOT EXISTS progression_days (local_day TEXT PRIMARY "
        "KEY)",
        "INSERT OR IGNORE INTO progression_checkpoint(singleton,attempt_id) "
        "VALUES(1,0)"})
    if (!q.exec(sql))
      return fail(q);
  if (!q.exec(
          "SELECT attempt_id FROM progression_checkpoint WHERE singleton=1") ||
      !q.next())
    return fail(q);
  qint64 cursor = q.value(0).toLongLong();
  QHash<int, TileData> tiles;
  if (!q.exec("SELECT subject_id,data_json FROM progression_tiles"))
    return fail(q);
  while (q.next())
    tiles.insert(q.value(0).toInt(),
                 decode(q.value(0).toInt(), q.value(1).toByteArray()));
  const auto before = tiles;
  if (!q.exec("SELECT COUNT(*) FROM progression_days") || !q.next())
    return fail(q);
  const int previousDays = q.value(0).toInt();
  QSet<int> touched;
  QSqlQuery events(db_);
  events.prepare("SELECT "
                 "id,subject_id,characters,dimension,correct,created_at,local_"
                 "day,event_json FROM attempts WHERE id>? ORDER BY id");
  events.addBindValue(cursor);
  if (!events.exec())
    return fail(events);
  while (events.next()) {
    cursor = events.value(0).toLongLong();
    const QString dimension = events.value(3).toString();
    const auto metadata =
        QJsonDocument::fromJson(events.value(7).toByteArray()).object();
    const auto gradedAt = preciseInstant(metadata["graded_at"].toString());
    const QString localDay =
        gradingDay(metadata, events.value(6).toString(), gradedAt);
    if (events.value(4).isNull() ||
        (dimension != "Reading" && dimension != "Meaning") ||
        !validDay(localDay))
      continue;
    const int id = events.value(1).toInt();
    auto &d = tiles[id];
    d.tile.id = id;
    if (!events.value(2).toString().isEmpty())
      d.tile.characters = events.value(2).toString();
    const auto reading = firstText(metadata["accepted_readings"]);
    const auto meaning = firstText(metadata["accepted_meanings"]);
    if (!reading.isEmpty()) {
      d.tile.reading = reading;
      d.usableReading = true;
    }
    if (!meaning.isEmpty()) {
      d.tile.meaning = meaning;
      d.usableMeaning = true;
    }
    // An actual legacy prompt establishes applicability of that dimension;
    // supplemental reveal text establishes no encounter in the other one.
    if (dimension == "Reading" && !metadata.contains("accepted_readings"))
      d.usableReading = true;
    if (dimension == "Meaning" && !metadata.contains("accepted_meanings"))
      d.usableMeaning = true;
    if (!metadata["source"].toString().isEmpty())
      d.tile.source = metadata["source"].toString();
    if (!metadata["subject_kind"].toString().isEmpty())
      d.tile.subjectKind = metadata["subject_kind"].toString();
    d.tile.lastAttempt = cursor;
    touched.insert(id);
    const auto instant =
        gradedAt ? gradedAt : preciseInstant(events.value(5).toString());
    // A date alone establishes practice activity, not an exact elapsed
    // interval.
    const qint64 instantMs = instant ? instant->toMSecsSinceEpoch()
                                     : std::numeric_limits<qint64>::min();
    q.prepare("INSERT OR IGNORE INTO "
              "progression_daily(subject_id,dimension,local_day,attempt_id,"
              "instant,correct) VALUES(?,?,?,?,?,?)");
    q.addBindValue(id);
    q.addBindValue(dimension);
    q.addBindValue(localDay);
    q.addBindValue(cursor);
    q.addBindValue(instantMs);
    q.addBindValue(events.value(4).toInt() == 1 ? 1 : 0);
    if (!q.exec())
      return fail(q);
    q.prepare("INSERT OR IGNORE INTO progression_days(local_day) VALUES(?)");
    q.addBindValue(localDay);
    if (!q.exec())
      return fail(q);
  }
  events.finish();
  for (const auto &subject : pool) {
    if (!tiles.contains(subject.id))
      continue;
    auto &d = tiles[subject.id];
    const auto reading = firstText(subject.readings);
    const auto meaning = firstText(subject.meanings);
    if (!reading.isEmpty()) {
      d.tile.reading = reading;
      d.usableReading = true;
    }
    if (!meaning.isEmpty()) {
      d.tile.meaning = meaning;
      d.usableMeaning = true;
    }
    if (!subject.characters.isEmpty())
      d.tile.characters = subject.characters;
    if (!subject.source.isEmpty())
      d.tile.source = subject.source;
    if (!subject.kind.isEmpty())
      d.tile.subjectKind = subject.kind;
    if (encode(d) != encode(before.value(subject.id)))
      touched.insert(subject.id);
  }
  QVector<ProgressNotice> pending;
  QList<int> ordered = touched.values();
  std::sort(ordered.begin(), ordered.end());
  for (const int id : ordered) {
    auto &d = tiles[id];
    auto &t = d.tile;
    QVector<QPair<qint64, bool>> reading, meaning;
    q.prepare("SELECT dimension,instant,correct FROM progression_daily WHERE "
              "subject_id=? ORDER BY local_day,attempt_id");
    q.addBindValue(id);
    if (!q.exec())
      return fail(q);
    while (q.next())
      (q.value(0).toString() == "Reading" ? reading : meaning)
          .append({q.value(1).toLongLong(), q.value(2).toBool()});
    const auto r = evaluate(reading), m = evaluate(meaning);
    t.eligible = d.usableReading && d.usableMeaning;
    t.readingSuccesses = r.successes;
    t.meaningSuccesses = m.successes;
    t.readingChallenge = r.challenge;
    t.meaningChallenge = m.challenge;
    d.readingClears = r.clears;
    d.meaningClears = m.clears;
    t.settled = t.settled || (t.eligible && r.complete && m.complete);
    t.progress = t.settled    ? 1.0
                 : t.eligible ? (r.progress + m.progress) / 2
                              : 0;
    int development = t.eligible && r.successes > 0 && m.successes > 0 ? 1 : 0;
    if (development && t.progress >= .5)
      development = 2;
    if (t.settled)
      development = 3;
    d.development = qMax(d.development, development);
    t.phase = t.settled           ? TilePhase::Settled
              : d.development > 0 ? TilePhase::Growing
                                  : TilePhase::Seen;
    const auto old = before.value(id);
    if (!old.tile.settled && t.settled)
      pending.append({ProgressNotice::Completed, id, t.characters, 0});
    else if (d.development > old.development)
      pending.append({ProgressNotice::Developed, id, t.characters, 0});
    // One notice describes overcoming the whole mark, including both skills.
    if (!t.challenged() &&
        (old.tile.challenged() || d.readingClears > old.readingClears ||
         d.meaningClears > old.meaningClears))
      pending.append({ProgressNotice::ChallengeCleared, id, t.characters, 0});
    q.prepare(
        "INSERT INTO progression_tiles(subject_id,data_json) VALUES(?,?) ON "
        "CONFLICT(subject_id) DO UPDATE SET data_json=excluded.data_json");
    q.addBindValue(id);
    q.addBindValue(QString::fromUtf8(encode(d)));
    if (!q.exec())
      return fail(q);
  }
  if (!q.exec("SELECT COUNT(*) FROM progression_days") || !q.next())
    return fail(q);
  const int practiceDays = q.value(0).toInt();
  // A batched refresh acknowledges only the latest occasion it crossed.
  int occasion = 0;
  for (int days = previousDays + 1; days <= practiceDays; ++days)
    if (consistencyOccasion(days))
      occasion = days;
  if (occasion)
    pending.append({ProgressNotice::Consistency, 0, {}, occasion});
  q.prepare("UPDATE progression_checkpoint SET attempt_id=? WHERE singleton=1");
  q.addBindValue(cursor);
  if (!q.exec())
    return fail(q);
  if (!db_.commit()) {
    error = db_.lastError().text();
    db_.rollback();
    return false;
  }
  ProgressState next;
  next.practiceDays = practiceDays;
  for (const auto &d : tiles) {
    next.tiles.append(d.tile);
    next.settled += d.tile.settled;
  }
  std::sort(next.tiles.begin(), next.tiles.end(),
            [](const ProgressTile &a, const ProgressTile &b) {
              return a.lastAttempt != b.lastAttempt
                         ? a.lastAttempt > b.lastAttempt
                         : a.id < b.id;
            });
  next.total = next.tiles.size();
  state_ = next;
  if (notify && initialized_)
    notices_ += pending;
  initialized_ = true;
  return true;
}
