#include "charts.h"
#include "theme.h"
#include <algorithm>

ChartData loadChartData(const Store &store, QDate today) {
  ChartData data;
  for (int i = 27; i >= 0; --i)
    data.days.append({today.addDays(-i)});
  const QString start = today.addDays(-27).toString(Qt::ISODate);
  const QString end = today.toString(Qt::ISODate);
  QSqlQuery q(store.db);
  q.prepare("SELECT local_day,COUNT(*) FROM attempts WHERE correct IS NOT NULL "
            "AND local_day BETWEEN ? AND ? GROUP BY local_day");
  q.addBindValue(start);
  q.addBindValue(end);
  q.exec();
  while (q.next()) {
    int i = today.addDays(-27).daysTo(
        QDate::fromString(q.value(0).toString(), Qt::ISODate));
    if (i >= 0 && i < 28)
      data.days[i].answered = q.value(1).toInt();
  }
  q.prepare(
      "SELECT local_day,dimension,SUM(correct),COUNT(*) FROM attempts WHERE id "
      "IN "
      "(SELECT MIN(id) FROM attempts WHERE correct IS NOT NULL AND "
      "mode<>'Recall' AND local_day BETWEEN ? AND ? "
      "GROUP BY subject_id,dimension,local_day) GROUP BY local_day,dimension");
  q.addBindValue(start);
  q.addBindValue(end);
  q.exec();
  while (q.next()) {
    int i = today.addDays(-27).daysTo(
        QDate::fromString(q.value(0).toString(), Qt::ISODate));
    if (i < 0 || i >= 28)
      continue;
    auto &day = data.days[i];
    if (q.value(1).toString() == "Reading") {
      day.readingCorrect = q.value(2).toInt();
      day.readingTotal = q.value(3).toInt();
    } else if (q.value(1).toString() == "Meaning") {
      day.meaningCorrect = q.value(2).toInt();
      day.meaningTotal = q.value(3).toInt();
    }
  }
  q.prepare(
      "SELECT json_extract(event_json,'$.selection_reason'),COUNT(*) FROM "
      "attempts "
      "WHERE correct IS NOT NULL AND local_day BETWEEN ? AND ? GROUP BY 1");
  q.addBindValue(start);
  q.addBindValue(end);
  q.exec();
  while (q.next()) {
    const QString reason = q.value(0).toString();
    int i = reason == "Recent struggles"     ? 0
            : reason == "Persistent trouble" ? 1
            : reason == "Broad recall"       ? 2
            : reason == "Newest 30"          ? 4
                                             : 3;
    data.mix[i] += q.value(1).toInt();
  }
  q.prepare("SELECT created_at,mode,correct,event_json FROM attempts WHERE "
            "correct IS NOT NULL AND local_day BETWEEN ? AND ?");
  q.addBindValue(start);
  q.addBindValue(end);
  q.exec();
  while (q.next()) {
    const auto event =
        QJsonDocument::fromJson(q.value(3).toByteArray()).object();
    auto time = QDateTime::fromString(event["local_time"].toString(),
                                      Qt::ISODateWithMs);
    if (!time.isValid())
      time = QDateTime::fromString(q.value(0).toString(), Qt::ISODateWithMs)
                 .toLocalTime();
    const int correct = q.value(2).toInt();
    if (time.isValid()) {
      const int hour = time.time().hour() / 4;
      ++data.hourTotal[hour];
      data.hourCorrect[hour] += correct;
    }
    const auto mode = q.value(1).toString();
    const int format = mode == "Recall"          ? 0
                       : mode == "Typed reading" ? 1
                       : mode == "Four choices"  ? 2
                                                 : -1;
    if (format >= 0) {
      ++data.formatTotal[format];
      data.formatCorrect[format] += correct;
    }
    const int stage = event["srs_stage"].toInt();
    if (event["source"].toString() != "anki" && stage >= 1 && stage <= 9) {
      const int group = stage <= 4 ? 0 : stage <= 6 ? 1 : stage - 5;
      ++data.stageTotal[group];
      data.stageCorrect[group] += correct;
    }
  }
  return data;
}

void InsightsBrowser::setInsights(const Store &store, const QString &html) {
  data = loadChartData(store);
  details = html;
  render();
}
void InsightsBrowser::resizeEvent(QResizeEvent *event) {
  QTextBrowser::resizeEvent(event);
  if (queued)
    return;
  queued = true;
  QTimer::singleShot(0, this, [this] {
    queued = false;
    render();
  });
}
void InsightsBrowser::render() {
  if (data.days.isEmpty())
    return;
  const int scroll = verticalScrollBar()->value();
  const int available = qMax(260, viewport()->width() - 36);
  // Keep two useful chart columns on a compact Insights view, then collapse to
  // one when labels would stop fitting. A wide dashboard still uses three.
  const int columns = available >= 1200 ? 3 : available >= 760 ? 2 : 1;
  setProperty("chartColumns", columns);
  const int width = qMin(620, (available - (columns - 1) * 20) / columns);
  const auto themeColors = colors(qApp->property("mojiNookTheme").toInt());
  const QColor text(themeColors.text);
  QColor surface(themeColors.background);
  surface.setAlpha(26);
  const QColor green(themeColors.series1);
  document()->setDefaultStyleSheet(
      QString("body {font-family:'Noto Sans'; font-size:14px;} "
              "h1 {font-size:26px; margin-bottom:12px;} "
              "h2 {font-size:20px; margin-top:24px; margin-bottom:8px;} "
              "a { color:%1; }").arg(green.name()));
  const QColor purple(themeColors.series2);
  const QColor gold(themeColors.series3);
  const QColor muted(themeColors.muted);
  const bool monochrome = qApp->property("mojiNookTheme").toInt() / 2 == 1;
  // Patterns distinguish the series across every palette, including grayscale.
  auto seriesBar = [&](QPainter &p, QRectF rect, QColor color, int series) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(rect, 3, 3);
    if (series > 0) {
      QPainterPath clip;
      clip.addRoundedRect(rect, 3, 3);
      p.setClipPath(clip);
      const QColor ink =
          color.lightness() > 128 ? QColor("#202020") : QColor("#ffffff");
      p.setPen(QPen(ink, 1.5));
      // Logical-pixel spacing remains legible on HiDPI screens.
      for (double x = rect.left() - rect.height(); x < rect.right(); x += 8) {
        if (series == 1 || series == 4)
          p.drawLine(QPointF(x, rect.bottom()),
                     QPointF(x + rect.height(), rect.top()));
        if (series == 3)
          p.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        if (series == 4)
          p.drawLine(QPointF(x, rect.top()),
                     QPointF(x + rect.height(), rect.bottom()));
      }
      if (series == 2)
        p.drawLine(QPointF(rect.left(), rect.center().y()),
                   QPointF(rect.right(), rect.center().y()));
    }
    p.restore();
  };
  QColor grid = text;
  grid.setAlpha(45);
  QString html = "<h1>Insights</h1><p>Last 28 days &nbsp; · &nbsp; <a "
                 "href='moji-nook:help'>About these stats</a></p>";
  QString panel;
  QStringList panels;
  auto chart = [&](const QString &name, int height, auto draw,
                   const QString &altText = QString()) {
    QImage image(QSize(width * 2, height * 2),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(surface);
    p.drawRoundedRect(QRectF(0, 0, width, height), 14, 14);
    QFont font("Noto Sans");
    font.setPixelSize(14);
    p.setFont(font);
    draw(p);
    p.end();
    const QUrl url("moji-nook-chart:" + name);
    document()->addResource(QTextDocument::ImageResource, url, image);
    panel += QString("<p><img src='%1' width='%2' height='%3' alt='%4'></p>")
                 .arg(url.toString())
                 .arg(width)
                 .arg(height)
                 .arg((altText.isEmpty() ? name : altText).toHtmlEscaped());
    panels.append(panel);
    panel.clear();
  };
  auto label = [&](QPainter &p, QRectF rect, const QString &s,
                   int flags = Qt::AlignLeft | Qt::AlignVCenter) {
    p.setPen(text);
    p.drawText(rect, flags, s);
  };
  int total = 0, activeDays = 0, peak = 0;
  for (const auto &d : data.days) {
    total += d.answered;
    activeDays += d.answered > 0;
    peak = qMax(peak, d.answered);
  }
  panel = QString("<h2>Daily practice</h2><p>%1 – %2 · %3 answers · %4 active days</p>")
              .arg(data.days.first().date.toString("MMM d"))
              .arg(data.days.last().date.toString("MMM d"))
              .arg(total)
              .arg(activeDays);
  QString rhythmAlt =
      QString("Daily practice answers from %1 through %2: ")
          .arg(data.days.first().date.toString("MMMM d, yyyy"))
          .arg(data.days.last().date.toString("MMMM d, yyyy"));
  for (int i = 0; i < data.days.size(); ++i) {
    if (i)
      rhythmAlt += "; ";
    rhythmAlt += QString("%1: %2 answers")
                     .arg(data.days[i].date.toString("MMMM d"))
                     .arg(data.days[i].answered);
  }
  rhythmAlt += QString(". %1 answers across %2 active days.")
                   .arg(total)
                   .arg(activeDays);
  chart("daily-activity", 196, [&](QPainter &p) {
    const double left = 62, right = 12, top = 22, bottom = 42;
    const double cellWidth = (width - left - right) / 7.0;
    const double rowHeight = (196 - top - bottom) / 4.0;
    const QColor zero(themeColors.surface);
    QColor outline(themeColors.border);
    outline.setAlpha(130);
    for (int col = 0; col < 7; ++col) {
      const QString weekday = data.days[col].date.toString("ddd").left(2);
      label(p, QRectF(left + col * cellWidth, 0, cellWidth, 19), weekday,
            Qt::AlignCenter);
    }
    for (int row = 0; row < 4; ++row) {
      const int first = row * 7;
      const auto &firstDay = data.days[first];
      const QString range = firstDay.date.toString("MMM d");
      const double y = top + row * rowHeight;
      label(p, QRectF(0, y, left - 7, rowHeight), range,
            Qt::AlignRight | Qt::AlignVCenter);
      for (int col = 0; col < 7; ++col) {
        const int i = first + col;
        const auto &day = data.days[i];
        const QRectF cell(left + col * cellWidth + 2, y + 2,
                          cellWidth - 5, rowHeight - 5);
        QColor cellColor = zero;
        p.setPen(QPen(outline, 1));
        if (day.answered > 0) {
          const double amount = .18 + .82 * day.answered / qMax(1, peak);
          cellColor = QColor::fromRgbF(
              surface.redF() * (1 - amount) + green.redF() * amount,
              surface.greenF() * (1 - amount) + green.greenF() * amount,
              surface.blueF() * (1 - amount) + green.blueF() * amount);
        }
        p.setBrush(cellColor);
        p.drawRoundedRect(cell, 4, 4);
        p.setPen(cellColor.lightness() > 128 ? QColor("#202020")
                                             : QColor("#ffffff"));
        p.drawText(cell, Qt::AlignCenter,
                   QString::number(day.date.day()));
      }
    }
    const double legendY = 172;
    label(p, QRectF(12, legendY, 60, 18), "Answers");
    QVector<int> legendValues;
    if (peak == 0)
      legendValues = {0};
    else if (peak == 1)
      legendValues = {0, 1};
    else
      legendValues = {0, 1, peak};
    for (int i = 0; i < legendValues.size(); ++i) {
      const double x = 80 + i * 58;
      QRectF swatch(x, legendY + 2, 14, 14);
      const int count = legendValues[i];
      QColor swatchColor = zero;
      p.setPen(QPen(outline, 1));
      if (count > 0) {
        const double amount = .18 + .82 * count / qMax(1, peak);
        swatchColor = QColor::fromRgbF(
            surface.redF() * (1 - amount) + green.redF() * amount,
            surface.greenF() * (1 - amount) + green.greenF() * amount,
            surface.blueF() * (1 - amount) + green.blueF() * amount);
      }
      p.setBrush(swatchColor);
      p.drawRoundedRect(swatch, 3, 3);
      label(p, QRectF(x + 18, legendY,
                      peak == 0 ? width - x - 18 : 34, 18),
            peak == 0 ? "No answers" : QString::number(count));
    }
  }, rhythmAlt);
  panel = "<h2>Reading &amp; meaning</h2><p>Checked accuracy · weekly</p>";
  chart("checked-performance", 260, [&](QPainter &p) {
    const QRectF plot(42, 38, width - 60, 150);
    for (int tick = 0; tick <= 2; ++tick) {
      double y = plot.bottom() - plot.height() * tick / 2;
      p.setPen(grid);
      p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
      label(p, QRectF(0, y - 9, 36, 18), QString::number(tick * 50) + "%",
            Qt::AlignRight | Qt::AlignVCenter);
    }
    label(p, QRectF(18, 4, width - 36, 24),
          monochrome ? "R  Reading · solid     M  Meaning · striped"
                     : "R  Reading     M  Meaning");
    double step = plot.width() / 4;
    for (int week = 0; week < 4; ++week) {
      int correct[2] = {}, counts[2] = {};
      for (int day = week * 7; day < (week + 1) * 7; ++day) {
        correct[0] += data.days[day].readingCorrect;
        counts[0] += data.days[day].readingTotal;
        correct[1] += data.days[day].meaningCorrect;
        counts[1] += data.days[day].meaningTotal;
      }
      for (int dim = 0; dim < 2; ++dim) {
        const double x = plot.left() + week * step + step * (dim ? 0.56 : 0.1),
                     bar = step * 0.28;
        if (counts[dim] && correct[dim]) {
          double h = plot.height() * correct[dim] / counts[dim];
          seriesBar(p, QRectF(x, plot.bottom() - h, bar, qMax(1.0, h)),
                    dim ? purple : green, dim);
        }
        label(p, QRectF(x - 5, 192, bar + 10, 20), dim ? "M" : "R",
              Qt::AlignCenter);
        if (width >= 540)
          label(p, QRectF(x - 12, 210, bar + 24, 20),
                counts[dim]
                    ? QString("%1/%2").arg(correct[dim]).arg(counts[dim])
                    : "—",
                Qt::AlignCenter);
        else
          label(p, QRectF(x - 12, 210, bar + 24, 20),
                counts[dim] ? QString::number(
                                  qRound(correct[dim] * 100.0 / counts[dim])) +
                                  "%"
                            : "—",
                Qt::AlignCenter);
      }
      label(p, QRectF(plot.left() + week * step, 235, step, 20),
            data.days[week * 7].date.toString("MMM d"), Qt::AlignCenter);
    }
  });
  QString checkedSummary;
  for (const auto &day : data.days)
    if (day.readingTotal || day.meaningTotal)
      checkedSummary += QString("%1: reading %2/%3, meaning %4/%5. ")
                            .arg(day.date.toString("MMM d"))
                            .arg(day.readingCorrect)
                            .arg(day.readingTotal)
                            .arg(day.meaningCorrect)
                            .arg(day.meaningTotal);
  panel = "<h2>Your practice mix</h2><p>Actual answered cards</p>";
  const QStringList names = {"Recent struggles", "Persistent trouble",
                             "Broad recall", "Legacy / unknown", "Newest 30"};
  // The selection categories share pigment; labels and print patterns distinguish them.
  const QList<QColor> colors = {green, green, green, green, green};
  chart("selection-balance", 290, [&](QPainter &p) {
    for (int i = 0; i < names.size(); ++i) {
      int y = 12 + i * 54;
      label(p, QRectF(16, y, width - 32, 22), names[i]);
      label(p, QRectF(16, y, width - 32, 22),
            QString("%1 · %2%")
                .arg(data.mix[i])
                .arg(total ? qRound(data.mix[i] * 100.0 / total) : 0),
            Qt::AlignRight | Qt::AlignVCenter);
      const QRectF track(16, y + 28, width - 32, 9);
      p.setPen(Qt::NoPen);
      p.setBrush(grid);
      p.drawRoundedRect(track, 4, 4);
      if (total && data.mix[i]) {
        seriesBar(p,
                  QRectF(track.left(), track.top(),
                         track.width() * data.mix[i] / total, 9),
                  colors[i], i);
      }
    }
  });

  auto accuracyPanel = [&](const QString &id, const QString &title,
                           const QString &subtitle, const QStringList &labels,
                           const QVector<int> &counts,
                           const QVector<int> &correct) {
    panel = "<h2>" + title + "</h2><p>" + subtitle + "</p>";
    chart(id, 290, [&](QPainter &p) {
      const double step = 258.0 / labels.size();
      for (int i = 0; i < labels.size(); ++i) {
        const double y = 12 + i * step;
        label(p, QRectF(16, y, width - 32, 22), labels[i]);
        label(p, QRectF(16, y, width - 32, 22),
              counts[i] ? QString("%1% · %2/%3")
                              .arg(qRound(correct[i] * 100.0 / counts[i]))
                              .arg(correct[i])
                              .arg(counts[i])
                        : "—",
              Qt::AlignRight | Qt::AlignVCenter);
        QRectF track(16, y + 28, width - 32, 7);
        p.setPen(Qt::NoPen);
        p.setBrush(grid);
        p.drawRoundedRect(track, 3, 3);
        if (counts[i]) {
          p.setBrush(green);
          p.drawRoundedRect(QRectF(track.left(), track.top(),
                                   track.width() * correct[i] / counts[i],
                                   track.height()),
                            3, 3);
        }
      }
    });
  };
  accuracyPanel("time-of-day", "When you practice",
                "Accuracy · local time · correct / answered",
                {"00–04", "04–08", "08–12", "12–16", "16–20", "20–24"},
                data.hourTotal, data.hourCorrect);
  accuracyPanel("answer-formats", "How you answer",
                "Accuracy · correct / answered",
                {"Self-rated", "Typed reading", "Four choices"},
                data.formatTotal, data.formatCorrect);
  accuracyPanel("mastery-recall", "Recall by mastery",
                "WaniKani stage at practice · correct / answered",
                {"Apprentice", "Guru", "Master", "Enlightened", "Burned"},
                data.stageTotal, data.stageCorrect);
  html += "<table cellspacing='12' cellpadding='0'>";
  for (int i = 0; i < panels.size(); ++i) {
    if (i % columns == 0)
      html += "<tr>";
    html += QString("<td width='%1' valign='top'>%2</td>")
                .arg(width)
                .arg(panels[i]);
    if (i % columns == columns - 1 || i == panels.size() - 1)
      html += "</tr>";
  }
  html += "</table>";
  if (help)
    html += "<h2>About these stats</h2><p>Reading &amp; meaning counts the "
            "first checked "
            "attempt per item and skill each local day. Other charts include "
            "all answered "
            "cards, including self-rated recall. Skips are excluded. Bar "
            "labels show sample "
            "sizes; small samples are noisy. Mastery uses the recorded stage, "
            "not today's stage.</p>"
            "<p>" +
            checkedSummary + "</p>";
  html +=
      "<p><a href='moji-nook:details'>" +
      QString(expanded ? "Hide details" : "All-time &amp; WaniKani details") +
      "</a></p>";
  if (expanded)
    html += details;
  setHtml(html);
  horizontalScrollBar()->setValue(0);
  verticalScrollBar()->setValue(scroll);
}
