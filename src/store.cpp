#include "core.h"

// Indexed below so card selection never parses event JSON. Rows without a
// grading field predate checked grading, when only recall was self-rated.
static const QString gradedStrength =
    "CASE WHEN json_valid(event_json) IS NOT 1 THEN mode IS NOT 'Recall' "
    "WHEN json_extract(event_json,'$.grading') IS NULL "
    "THEN mode IS NOT 'Recall' "
    "ELSE (json_extract(event_json,'$.grading')='checked' AND "
    "json_extract(event_json,'$.overridden') IS NOT 1 AND "
    "json_extract(event_json,'$.near_miss_retry') IS NOT 1) OR "
    "json_extract(event_json,'$.recall_attempt_matched') IS 1 END";

Store::Store(const QString &path) : directory(path) {
  if (!QDir().mkpath(path)) {
    error = "Could not create the data directory.";
    return;
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner);
  db = QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
  db.setDatabaseName(path + "/practice.sqlite");
  if (!db.open()) {
    error = db.lastError().text();
    return;
  }
  QSqlQuery q(db);
  if (!q.exec("CREATE TABLE IF NOT EXISTS attempts (id INTEGER PRIMARY KEY, "
              "subject_id INTEGER NOT NULL, characters TEXT, mode TEXT, "
              "dimension TEXT, correct INTEGER, active_ms INTEGER, created_at "
              "TEXT NOT NULL, local_day TEXT NOT NULL)"))
    error = q.lastError().text();
  q.exec("CREATE INDEX IF NOT EXISTS attempts_subject ON attempts(subject_id, "
         "id)");
  q.exec("CREATE INDEX IF NOT EXISTS attempts_day ON attempts(local_day)");
  // Additive migration: old events remain intact; unavailable fields stay NULL.
  if (!db.record("attempts").contains("event_json") &&
      !q.exec("ALTER TABLE attempts ADD COLUMN event_json TEXT"))
    error = q.lastError().text();
  q.exec("CREATE INDEX IF NOT EXISTS attempts_graded ON attempts(created_at,"
         "subject_id,dimension,correct,local_day,(" +
         gradedStrength + ")) WHERE correct IS NOT NULL");
  for (const auto &sql :
       {"CREATE TABLE IF NOT EXISTS wk_latest (subject_id INTEGER PRIMARY KEY, "
        "observed_at TEXT NOT NULL, data_json TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS wk_changes (id INTEGER PRIMARY KEY, "
        "subject_id INTEGER NOT NULL, from_at TEXT NOT NULL, to_at TEXT NOT "
        "NULL, reading_errors INTEGER, meaning_errors INTEGER, reset INTEGER "
        "NOT NULL DEFAULT 0)",
        "CREATE INDEX IF NOT EXISTS wk_changes_time ON wk_changes(to_at)"})
    if (!q.exec(sql))
      error = q.lastError().text();
  QFile::setPermissions(db.databaseName(),
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
Store::~Store() {
  const auto name = db.connectionName();
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(name);
}
QJsonObject Store::cache() const {
  QFile f(directory + "/cache.json");
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}
bool Store::saveCache(const QJsonObject &value) {
  QSaveFile f(directory + "/cache.json");
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  const QByteArray bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
  return f.write(bytes) == bytes.size() && f.commit();
}
bool Store::record(const Challenge &c, std::optional<bool> correct,
                   qint64 activeMs) {
  QSqlQuery q(db);
  q.prepare("INSERT INTO "
            "attempts(subject_id,characters,mode,dimension,correct,active_ms,"
            "created_at,local_day,event_json) VALUES(?,?,?,?,?,?,?,?,?)");
  q.addBindValue(c.subject.id);
  q.addBindValue(c.subject.characters);
  const QString mode =
      c.wanikaniReview ? QString("WaniKani review") : modeName(c.mode);
  q.addBindValue(mode);
  q.addBindValue(c.reading ? "Reading" : "Meaning");
  q.addBindValue(correct ? QVariant(*correct ? 1 : 0) : QVariant());
  q.addBindValue(activeMs);
  q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  // Count the attempt on the day it was graded, matching progression.
  const QDate graded = QDate::fromString(c.event["graded_local_day"].toString(), Qt::ISODate);
  q.addBindValue((graded.isValid() ? graded : QDate::currentDate()).toString(Qt::ISODate));
  QJsonObject event = c.event;
  const auto local = QDateTime::currentDateTime();
  event["schema_version"] = 2;
  event["event_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
  event["completed_at"] = local.toUTC().toString(Qt::ISODateWithMs);
  event["local_time"] = local.toString(Qt::ISODateWithMs);
  event["utc_offset_seconds"] = local.offsetFromUtc();
  event["timezone"] = QString::fromUtf8(QTimeZone::systemTimeZoneId());
  event["subject_id"] = c.subject.id;
  event["source"] = c.subject.source;
  if (c.subject.source == "anki") {
    event["source_id"] = c.subject.sourceId;
    event["source_deck"] = c.subject.deck;
    event["source_snapshot"] = c.subject.sourceData;
    event["srs_weight_mapping"] = "anki-interval-v1";
  }
  event["subject_level"] = c.subject.level;
  event["subject_started_at"] =
      c.subject.startedAt.isValid()
          ? QJsonValue(c.subject.startedAt.toUTC().toString(Qt::ISODateWithMs))
          : QJsonValue();
  event["srs_stage"] = c.subject.stage;
  event["subject_kind"] = c.subject.kind;
  event["characters"] = c.subject.characters;
  event["accepted_meanings"] = QJsonArray::fromStringList(c.subject.meanings);
  event["accepted_readings"] = QJsonArray::fromStringList(c.subject.readings);
  event["choices"] = QJsonArray::fromStringList(c.choices);
  event["mode"] = mode;
  event["dimension"] = c.reading ? "Reading" : "Meaning";
  event["grading"] = c.mode == Mode::Recall ? "self-rated" : "checked";
  event["outcome"] =
      correct ? (*correct ? "correct" : "incorrect") : "dismissed";
  event["selection_reason"] = c.selectionReason;
  event["interaction_elapsed_ms"] = double(activeMs);
  event["app_version"] = QCoreApplication::applicationVersion();
  event["platform"] = QSysInfo::productType();
  q.addBindValue(
      QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
  if (!q.exec()) {
    error = q.lastError().text();
    return false;
  }
  return true;
}
QVector<GradedAttempt> Store::gradedAttempts(const QDateTime &after) const {
  QVector<GradedAttempt> out;
  QSqlQuery q(db);
  q.setForwardOnly(true);
  // SQLite converts timestamps; Qt's ISO parser is slower.
  q.prepare("SELECT subject_id,dimension='Reading',correct," + gradedStrength +
            ",CAST(round((julianday(created_at)-2440587.5)*86400000) AS "
            "INTEGER),CAST(julianday(local_day)+0.5 AS INTEGER) FROM attempts "
            "WHERE correct IS NOT NULL AND created_at>? ORDER BY created_at");
  q.addBindValue(after.isValid() ? after.toUTC().toString(Qt::ISODateWithMs)
                                 : QStringLiteral(""));
  q.exec();
  while (q.next())
    out.append({q.value(0).toInt(), q.value(1).toBool(), q.value(2).toBool(),
                q.value(3).toBool(),
                QDateTime::fromMSecsSinceEpoch(q.value(4).toLongLong(),
                                               QTimeZone::UTC),
                QDate::fromJulianDay(q.value(5).toLongLong())});
  return out;
}
QList<int> Store::recentSubjects() const {
  QList<int> ids;
  QSqlQuery q(db);
  q.exec("SELECT subject_id FROM attempts ORDER BY id DESC LIMIT 20");
  while (q.next())
    ids.append(q.value(0).toInt());
  return ids;
}

bool Store::exportCsv(const QString &path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;
  QTextStream out(&f);
  out << "subject_id,characters,mode,dimension,correct,active_ms,created_at,"
         "local_day,event_json\n";
  QSqlQuery q(db);
  if (!q.exec("SELECT "
              "subject_id,characters,mode,dimension,correct,active_ms,created_"
              "at,local_day,event_json FROM attempts ORDER BY id"))
    return false;
  while (q.next()) {
    for (int i = 0; i < 9; ++i) {
      if (i)
        out << ',';
      QString text = q.value(i).toString();
      bool numeric = false;
      text.toLongLong(&numeric); // Negative Anki subject IDs stay numbers.
      if (!numeric && !text.isEmpty() && QString("=+-@\t\r\n").contains(text.front()))
        text.prepend('\''); // Spreadsheet formula injection protection.
      text.replace('"', "\"\"");
      out << '"' << text << '"';
    }
    out << '\n';
  }
  out.flush();
  return out.status() == QTextStream::Ok && f.commit();
}
