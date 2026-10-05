#include "core.h"
#include <cmath>
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
  q.addBindValue(modeName(c.mode));
  q.addBindValue(c.reading ? "Reading" : "Meaning");
  q.addBindValue(correct ? QVariant(*correct ? 1 : 0) : QVariant());
  q.addBindValue(activeMs);
  q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  q.addBindValue(QDate::currentDate().toString(Qt::ISODate));
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
  event["mode"] = modeName(c.mode);
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
QHash<int, double> Store::missBoosts() const {
  QHash<int, double> result;
  QSqlQuery q(db);
  q.exec(
      "SELECT subject_id,correct,created_at FROM attempts WHERE id IN (SELECT "
      "MAX(id) FROM attempts WHERE correct IS NOT NULL GROUP BY subject_id)");
  while (q.next())
    if (q.value(1).toInt() == 0) {
      double days =
          QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs)
              .secsTo(QDateTime::currentDateTimeUtc()) /
          86400.0;
      result[q.value(0).toInt()] = 1 + std::exp(-qMax(0.0, days) / 3.0);
    }
  return result;
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
      if (!text.isEmpty() && QString("=+-@\t\r\n").contains(text.front()))
        text.prepend('\''); // Spreadsheet formula injection protection.
      text.replace('"', "\"\"");
      out << '"' << text << '"';
    }
    out << '\n';
  }
  out.flush();
  return out.status() == QTextStream::Ok && f.commit();
}
