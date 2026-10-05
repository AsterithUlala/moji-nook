#pragma once
#include "core.h"
#include <QtWidgets>

class PracticeModes : public QWidget {
public:
  explicit PracticeModes(Store &store, QWidget *parent = nullptr);
  void setPool(const QVector<Subject> &subjects) { pool = subjects; }
  void setReadingRetries(bool enabled) { retries = enabled; }
  QVector<Subject> candidates() const;

private:
  Store &store;
  QVector<Subject> pool, queue;
  int position = 0, correctCount = 0;
  bool checked = false, correct = false, retries = true;
  QString session;
  QElapsedTimer elapsed;
  QLabel *progress, *word, *solution, *resultIcon, *resultTitle, *feedback;
  QLineEdit *input;
  QPushButton *start, *check, *next, *end;
  QWidget *exercise;
  void begin();
  void present();
  void grade();
  void advance();
};
