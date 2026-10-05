#pragma once
#include "core.h"
#include <QtWidgets>

struct ChartDay {
  QDate date;
  int answered = 0, readingCorrect = 0, readingTotal = 0;
  int meaningCorrect = 0, meaningTotal = 0;
};
struct ChartData {
  QVector<ChartDay> days;
  QVector<int> mix = {0, 0, 0, 0, 0};
  QVector<int> hourTotal = QVector<int>(6), hourCorrect = QVector<int>(6);
  QVector<int> formatTotal = QVector<int>(3), formatCorrect = QVector<int>(3);
  QVector<int> stageTotal = QVector<int>(5), stageCorrect = QVector<int>(5);
};
ChartData loadChartData(const Store &store, QDate today = QDate::currentDate());

class InsightsBrowser : public QTextBrowser {
public:
  explicit InsightsBrowser(QWidget *parent = nullptr) : QTextBrowser(parent) {
    setOpenLinks(false);
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
      if (url.toString() == "moji-nook:details") {
        expanded = !expanded;
        render();
      }
      if (url.toString() == "moji-nook:help") {
        help = !help;
        render();
      }
    });
  }
  void setInsights(const Store &store, const QString &details);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  ChartData data;
  QString details;
  bool queued = false;
  bool expanded = false, help = false;
  void render();
};
