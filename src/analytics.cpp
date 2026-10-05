#include "core.h"
#include <algorithm>
#include <cmath>

bool Store::observeReviews(const QJsonArray &rows, const QString &at) {
  if (!QDateTime::fromString(at, Qt::ISODate).isValid()) {
    error = "Invalid review observation timestamp.";
    return false;
  }
  if (!db.transaction()) {
    error = db.lastError().text();
    return false;
  }
  auto failed = [&](const QString &message) {
    db.rollback();
    error = message;
    return false;
  };
  for (const auto &row : rows) {
    const auto data = row.toObject()["data"].toObject();
    const int id = data["subject_id"].toInt();
    if (id <= 0 || data["hidden"].toBool())
      continue;
    for (const auto &field : {"reading_correct", "reading_incorrect",
                              "meaning_correct", "meaning_incorrect"})
      if (!data[field].isDouble() || data[field].toDouble() < 0)
        return failed(
            "Incomplete review statistics; previous observations preserved.");
    QSqlQuery previous(db);
    previous.prepare(
        "SELECT observed_at,data_json FROM wk_latest WHERE subject_id=?");
    previous.addBindValue(id);
    if (!previous.exec())
      return failed(previous.lastError().text());
    if (previous.next()) {
      const QString from = previous.value(0).toString();
      // Makes retries and importing the same sync idempotent.
      if (QDateTime::fromString(from, Qt::ISODate) >=
          QDateTime::fromString(at, Qt::ISODate))
        continue;
      const auto old =
          QJsonDocument::fromJson(previous.value(1).toByteArray()).object();
      bool reset = data["created_at"] != old["created_at"];
      for (const auto &field : {"reading_correct", "reading_incorrect",
                                "meaning_correct", "meaning_incorrect"})
        reset |= data[field].toInt() < old[field].toInt();
      const int reading =
          data["reading_incorrect"].toInt() - old["reading_incorrect"].toInt();
      const int meaning =
          data["meaning_incorrect"].toInt() - old["meaning_incorrect"].toInt();
      if (reset || reading || meaning) {
        QSqlQuery change(db);
        change.prepare("INSERT INTO "
                       "wk_changes(subject_id,from_at,to_at,reading_errors,"
                       "meaning_errors,reset) VALUES(?,?,?,?,?,?)");
        change.addBindValue(id);
        change.addBindValue(from);
        change.addBindValue(at);
        change.addBindValue(reset ? QVariant() : QVariant(reading));
        change.addBindValue(reset ? QVariant() : QVariant(meaning));
        change.addBindValue(reset ? 1 : 0);
        if (!change.exec())
          return failed(change.lastError().text());
      }
    }
    QSqlQuery update(db);
    update.prepare("INSERT OR REPLACE INTO "
                   "wk_latest(subject_id,observed_at,data_json) VALUES(?,?,?)");
    update.addBindValue(id);
    update.addBindValue(at);
    update.addBindValue(
        QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
    if (!update.exec())
      return failed(update.lastError().text());
  }
  if (!db.commit())
    return failed(db.lastError().text());
  return true;
}

Evidence Store::evidence() const {
  Evidence out;
  const auto now = QDateTime::currentDateTimeUtc();
  const QString cutoff = now.addDays(-7).toString(Qt::ISODate);
  QSqlQuery q(db);
  q.exec("SELECT subject_id,data_json FROM wk_latest");
  while (q.next()) {
    const auto data =
        QJsonDocument::fromJson(q.value(1).toByteArray()).object();
    for (bool reading : {false, true}) {
      const QString dim = reading ? "reading" : "meaning";
      double errors = data[dim + "_incorrect"].toDouble();
      double correct = data[dim + "_correct"].toDouble();
      double streak = data[dim + "_current_streak"].toDouble();
      // Shrink sparse histories; recent WK successes reduce lifetime trouble.
      out[skillKey(q.value(0).toInt(), reading)].persistent =
          errors / (errors + correct + 8) * std::log1p(errors) /
          (1 + streak / 3);
    }
  }
  q.prepare("SELECT subject_id,to_at,reading_errors,meaning_errors FROM "
            "wk_changes c WHERE reset=0 AND to_at>=? "
            "AND NOT EXISTS(SELECT 1 FROM wk_changes r WHERE "
            "r.subject_id=c.subject_id AND r.reset=1 AND r.id>c.id)");
  q.addBindValue(cutoff);
  q.exec();
  while (q.next()) {
    const double days = qMax(
        0.0,
        QDateTime::fromString(q.value(1).toString(), Qt::ISODate).secsTo(now) /
            86400.0);
    for (bool reading : {false, true})
      out[skillKey(q.value(0).toInt(), reading)].recent +=
          std::log1p(q.value(reading ? 2 : 3).toDouble()) * std::exp(-days / 3);
  }
  q.exec("SELECT subject_id,dimension,SUM(correct=0),COUNT(*) FROM attempts "
         "WHERE correct IS NOT NULL GROUP BY subject_id,dimension");
  while (q.next()) {
    const double errors = q.value(2).toDouble(), n = q.value(3).toDouble();
    out[skillKey(q.value(0).toInt(), q.value(1).toString() == "Reading")]
        .persistent += errors / (n + 8) * std::log1p(errors);
  }
  q.prepare("SELECT subject_id,dimension,correct,created_at FROM attempts "
            "WHERE correct IS NOT NULL AND created_at>=? ORDER BY id DESC");
  q.addBindValue(cutoff);
  q.exec();
  QHash<QString, int> consecutive;
  QSet<QString> stopped;
  while (q.next()) {
    const auto key =
        skillKey(q.value(0).toInt(), q.value(1).toString() == "Reading");
    const bool correct = q.value(2).toInt() == 1;
    if (!stopped.contains(key)) {
      if (correct)
        ++consecutive[key];
      else
        stopped.insert(key);
    }
    if (!correct) {
      double days = qMax(
          0.0, QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs)
                       .secsTo(now) /
                   86400.0);
      out[key].recent += std::exp(-days / 3);
    }
  }
  for (auto it = out.begin(); it != out.end(); ++it) {
    const double recovery = std::pow(0.5, qMin(5, consecutive.value(it.key())));
    it->recent *= recovery;
    it->persistent *= recovery;
  }
  return out;
}

QString Store::insightsHtml(const QVector<Subject> &pool) const {
  auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
  QString html =
      "<h1>Your learning, in more detail</h1><h2>Sessions · last 28 days</h2>";
  QSqlQuery sessions(db);
  sessions.prepare(
      "SELECT COUNT(*),COALESCE(SUM(answered=total),0) FROM "
      "(SELECT COUNT(correct) AS "
      "answered,MAX(json_extract(event_json,'$.session_total')) AS total "
      "FROM attempts WHERE local_day>=? AND "
      "json_extract(event_json,'$.session_id') IS NOT NULL "
      "GROUP BY json_extract(event_json,'$.session_id'))");
  sessions.addBindValue(
      QDate::currentDate().addDays(-27).toString(Qt::ISODate));
  if (sessions.exec() && sessions.next())
    html += QString("<p>%1 fully answered / %2 recorded sessions</p>")
                .arg(sessions.value(1).toInt())
                .arg(sessions.value(0).toInt());
  html += "<h2>Checked recall · first attempt of each day</h2>"
          "<p><small>Only the first checked attempt for each item and skill on "
          "a local day counts here. "
          "Self-rated recall is excluded. This is practice success, not a "
          "prediction of retention.</small></p>"
          "<table width='100%'><tr><th>Period</th><th>Skill</th><th>Correct / "
          "checked</th><th>Success</th></tr>";
  QSqlQuery q(db);
  for (int days : {7, 28}) {
    q.prepare(
        "SELECT dimension,SUM(correct),COUNT(*) FROM attempts WHERE id IN "
        "(SELECT MIN(id) FROM attempts WHERE correct IS NOT NULL AND "
        "mode<>'Recall' "
        "AND local_day>=? GROUP BY subject_id,dimension,local_day) GROUP BY "
        "dimension");
    q.addBindValue(
        QDate::currentDate().addDays(1 - days).toString(Qt::ISODate));
    q.exec();
    bool any = false;
    while (q.next()) {
      any = true;
      html +=
          QString("<tr><td>%1 days</td><td>%2</td><td>%3 / "
                  "%4</td><td>%5%</td></tr>")
              .arg(days)
              .arg(esc(q.value(0).toString()))
              .arg(q.value(1).toInt())
              .arg(q.value(2).toInt())
              .arg(qRound(q.value(1).toDouble() * 100 / q.value(2).toInt()));
    }
    if (!any)
      html += QString("<tr><td>%1 days</td><td colspan='3'>No checked answers "
                      "yet</td></tr>")
                  .arg(days);
  }
  html += "</table><h2>Practice balance · last 28 days</h2><table width='100%'>"
          "<tr><th>Selection</th><th>Answered</th><th>Unique items</th></tr>";
  q.prepare(
      "SELECT COALESCE(json_extract(event_json,'$.selection_reason'),'Legacy / "
      "unknown'),COUNT(*),COUNT(DISTINCT subject_id) "
      "FROM attempts WHERE correct IS NOT NULL AND local_day>=? GROUP BY 1");
  q.addBindValue(QDate::currentDate().addDays(-27).toString(Qt::ISODate));
  q.exec();
  while (q.next())
    html += QString("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                .arg(esc(q.value(0).toString()))
                .arg(q.value(1).toInt())
                .arg(q.value(2).toInt());
  html += "</table><h2>WaniKani · cumulative history</h2>";
  QHash<int, QString> names;
  for (const auto &s : pool)
    names[s.id] = s.characters;
  struct Trouble {
    QString name, dim;
    int errors, correct;
    double score;
  };
  QVector<Trouble> trouble;
  q.exec("SELECT subject_id,data_json FROM wk_latest");
  while (q.next()) {
    if (!names.contains(q.value(0).toInt()))
      continue;
    const auto d = QJsonDocument::fromJson(q.value(1).toByteArray()).object();
    for (const auto &dim : {QString("reading"), QString("meaning")}) {
      const int errors = d[dim + "_incorrect"].toInt(),
                correct = d[dim + "_correct"].toInt();
      if (errors)
        trouble.append(
            {names.value(q.value(0).toInt()), dim, errors, correct,
             errors / double(errors + correct + 8) * std::log1p(errors)});
    }
  }
  std::sort(trouble.begin(), trouble.end(),
            [](auto a, auto b) { return a.score > b.score; });
  if (trouble.isEmpty())
    html += "<p>No mistake history available yet. Sync WaniKani to begin.</p>";
  for (int i = 0; i < qMin(6, int(trouble.size())); ++i) {
    const auto &t = trouble[i];
    html +=
        QString(
            "<p><font size='5'>%1</font> · %2 · %3 incorrect / %4 answers</p>")
            .arg(esc(t.name), t.dim)
            .arg(t.errors)
            .arg(t.errors + t.correct);
  }
  html += "<h2>WaniKani · changes observed in the last 7 days</h2>"
          "<p><small>These are counter increases between syncs, not dated "
          "review events. "
          "An interval can extend before this week. The first sync establishes "
          "a baseline; resets establish a new one.</small></p>";
  q.prepare(
      "SELECT subject_id,from_at,to_at,reading_errors,meaning_errors FROM "
      "wk_changes WHERE reset=0 AND to_at>=? ORDER BY to_at DESC LIMIT 12");
  q.addBindValue(
      QDateTime::currentDateTimeUtc().addDays(-7).toString(Qt::ISODate));
  q.exec();
  bool changes = false;
  while (q.next()) {
    if (!names.contains(q.value(0).toInt()))
      continue;
    changes = true;
    html += QString("<p>%1 · reading +%2, meaning +%3<br><small>%4 → %5 "
                    "(UTC)</small></p>")
                .arg(esc(names.value(q.value(0).toInt())))
                .arg(q.value(3).toInt())
                .arg(q.value(4).toInt())
                .arg(esc(q.value(1).toString()), esc(q.value(2).toString()));
  }
  if (!changes)
    html += "<p>No new mistake increases observed yet.</p>";
  return html + statsHtml(pool, true);
}

QString Store::historyHtml(int page) const {
  QString html = "<h1>History</h1><p>Newest first · local time</p>";
  QSqlQuery q(db);
  q.prepare(
      "SELECT created_at,characters,dimension,mode,correct,event_json FROM "
      "attempts ORDER BY id DESC LIMIT 50 OFFSET ?");
  q.addBindValue(qMax(0, page) * 50);
  q.exec();
  bool any = false;
  QString previousDay;
  while (q.next()) {
    const auto timestamp =
        QDateTime::fromString(q.value(0).toString(), Qt::ISODateWithMs)
            .toLocalTime();
    const auto day = timestamp.date();
    const QString heading = !timestamp.isValid()          ? "Date unavailable"
                            : day == QDate::currentDate() ? "Today"
                            : day == QDate::currentDate().addDays(-1)
                                ? "Yesterday"
                                : day.toString("ddd, MMM d, yyyy");
    if (heading != previousDay) {
      if (any)
        html += "</table>";
      html += "<h2>" + heading +
              "</h2><table width='99%' cellspacing='0' cellpadding='12'>"
              "<tr><th align='center'>Time</th><th align='center'>Word</th>"
              "<th align='center'>Practice</th><th align='center'>Source</th>"
              "<th align='center'>Result</th></tr>";
      previousDay = heading;
    }
    any = true;
    const auto event =
        QJsonDocument::fromJson(q.value(5).toByteArray()).object();
    const bool skipped = q.value(4).isNull(), correct = q.value(4).toInt() == 1;
    const QString color = skipped ? "#a9bcb0" : correct ? "#a6e2bf" : "#f0bba0";
    const QString outcome = skipped   ? "— Skipped"
                            : correct ? "✓ Correct"
                                      : "× Miss";
    QString reason = event["selection_reason"].toString();
    reason.replace("Recent struggles", "Recent trouble");
    const QString source = event["source"].toString() == "anki"
        ? "Anki" : "WaniKani";
    const QString deck = event["source_deck"].toString();
    html += QString("<tr><td width='90' align='center' valign='middle'><nobr>%1</nobr></td>"
                    "<td width='220' align='center' valign='middle'><nobr><span style='font-size:24px'>%2</span></nobr></td>"
                    "<td align='center' valign='middle'>%3 · %4%8</td>"
                    "<td align='center' valign='middle'>%5%9</td>"
                    "<td width='110' align='center' valign='middle'><span "
                    "style='color:%6;font-weight:600'><nobr>%7</nobr></span></td></tr>")
                .arg(timestamp.isValid() ? timestamp.toString("h:mm AP") : "—",
                     q.value(1).toString().toHtmlEscaped(),
                     q.value(2).toString().toHtmlEscaped(),
                     q.value(3).toString().toHtmlEscaped(),
                     source, color, outcome,
                     reason.isEmpty() ? QString() : "<br><small>" + reason.toHtmlEscaped() + "</small>",
                     deck.isEmpty() ? QString() : "<br><small>" + deck.toHtmlEscaped() + "</small>");
  }
  if (any)
    html += "</table>";
  else
    html += "<p>Your completed and dismissed cards will appear here.</p>";
  return html;
}
