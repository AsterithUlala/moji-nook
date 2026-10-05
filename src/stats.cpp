#include "core.h"
#include <algorithm>
#include <cmath>
QString Store::statsHtml(const QVector<Subject> &pool, bool detailed) const {
  auto esc = [](QString s) { return s.toHtmlEscaped(); };
  auto count = [&](QString sql) {
    QSqlQuery q(db);
    q.exec(sql);
    return q.next() ? q.value(0).toInt() : 0;
  };
  int total = count("SELECT COUNT(*) FROM attempts WHERE correct IS NOT NULL");
  int good = count("SELECT COUNT(*) FROM attempts WHERE correct=1");
  int distinct = count("SELECT COUNT(DISTINCT subject_id) FROM attempts WHERE "
                       "correct IS NOT NULL");
  int comebacks =
      count("SELECT COUNT(*) FROM attempts a WHERE a.correct=1 AND (SELECT "
            "b.correct FROM attempts b WHERE b.subject_id=a.subject_id AND "
            "b.dimension=a.dimension AND b.correct IS NOT NULL AND b.id<a.id "
            "ORDER BY b.id DESC LIMIT 1)=0");
  QMap<QDate, int> days;
  QSqlQuery q(db);
  q.exec("SELECT local_day,COUNT(*) FROM attempts WHERE correct IS NOT NULL "
         "GROUP BY local_day");
  while (q.next())
    days[QDate::fromString(q.value(0).toString(), Qt::ISODate)] =
        q.value(1).toInt();
  int streak = 0;
  QDate today = QDate::currentDate(),
        d = days.contains(today) ? today : today.addDays(-1);
  while (days.contains(d)) {
    ++streak;
    d = d.addDays(-1);
  }
  auto accuracy = [&](int hours) {
    QSqlQuery recent(db);
    recent.prepare(
        "SELECT COUNT(*),COALESCE(SUM(correct),0) FROM attempts WHERE "
        "correct IS NOT NULL AND julianday(created_at)>=julianday(?) "
        "AND julianday(created_at)<=julianday(?)");
    const auto now = QDateTime::currentDateTimeUtc();
    recent.addBindValue(now.addSecs(-hours * 3600).toString(Qt::ISODateWithMs));
    recent.addBindValue(now.toString(Qt::ISODateWithMs));
    recent.exec();
    recent.next();
    const int n = recent.value(0).toInt(), correct = recent.value(1).toInt();
    return QString("<font size='6'>%1</font><br>%2 / %3 correct")
        .arg(n ? QString::number(qRound(correct * 100.0 / n)) + "%" : "—")
        .arg(correct)
        .arg(n);
  };
  QString html = "<style>body{font-family:'Noto Sans';font-size:16px}"
                 "h1{font-size:26px}h2{font-size:20px}td{padding:14px}</style>";
  if (!detailed) {
    html += "<h1>Your practice</h1>";
    html +=
        QString(
            "<table width='100%' cellspacing='8'><tr>"
            "<td><b>Last 24 hours</b><br>%1</td>"
            "<td><b>Last 7 days</b><br>%2</td></tr></table>")
            .arg(accuracy(24), accuracy(168));
    html += QString("<p>%1 answered today &nbsp; · &nbsp; %2-day streak</p>"
                    "<p>%3 learned items available</p>"
                    "<p><small>Practice accuracy · includes self-rated "
                    "recall</small></p>")
                .arg(days.value(today))
                .arg(streak)
                .arg(pool.size());
    return html;
  }
  html +=
      QString("<h2>All-time practice</h2><p><b>%1</b> accuracy · %2 answers · "
              "%3 unique items · %4 comebacks</p>")
          .arg(total ? QString::number(qRound(good * 100.0 / total)) + "%"
                     : "—")
          .arg(total)
          .arg(distinct)
          .arg(comebacks);
  html += "<h2>How you remember</h2><table "
          "width='100%'><tr><th>Challenge</th><th>Reading / "
          "meaning</th><th>Answered</th><th>Success</th></tr>";
  q.exec("SELECT mode,dimension,COUNT(*),SUM(correct) FROM attempts WHERE "
         "correct IS NOT NULL GROUP BY mode,dimension ORDER BY mode,dimension");
  while (q.next())
    html += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4%</td></tr>")
                .arg(esc(q.value(0).toString()), esc(q.value(1).toString()))
                .arg(q.value(2).toInt())
                .arg(qRound(q.value(3).toDouble() * 100 / q.value(2).toInt()));
  html += "</table><p><small>Recall cards are self-rated; typing and choices "
          "are checked. These are practice stats, not WaniKani SRS "
          "progress.</small></p>";
  html += "<h2>Worth another look</h2>";
  q.exec("SELECT characters,dimension,SUM(correct=0),COUNT(*) FROM attempts "
         "WHERE correct IS NOT NULL GROUP BY subject_id,dimension HAVING "
         "SUM(correct=0)>0 ORDER BY (1.0*SUM(correct=0)/COUNT(*)) "
         "DESC,SUM(correct=0) DESC LIMIT 6");
  bool found = false;
  while (q.next()) {
    found = true;
    html += QString("<p><font size='5'>%1</font> &nbsp; %2 · %3 misses / %4 "
                    "attempts</p>")
                .arg(esc(q.value(0).toString()), esc(q.value(1).toString()))
                .arg(q.value(2).toInt())
                .arg(q.value(3).toInt());
  }
  if (!found)
    html += "<p>Your tricky words will appear here as you practice.</p>";
  html += "<p><small>A comeback means remembering the same reading or meaning "
          "after your last attempt was a miss. Dismissed cards are excluded "
          "from accuracy and streaks.</small></p>";
  return html;
}
