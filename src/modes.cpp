#include "modes.h"
#include "style.h"
#include "theme.h"
#include "ui_helpers.h"

PracticeModes::PracticeModes(Store &s, QWidget *parent)
    : QWidget(parent), store(s) {
  setObjectName("practiceModes");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(18);
  auto label = [](QString text, QString name = {}) {
    auto *l = new QLabel(text);
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(true);
    l->setObjectName(name);
    return l;
  };
  auto *title = label("Focused practice");
  title->setStyleSheet("font-size:26px;font-weight:600");
  layout->addWidget(title);
  auto *intro = new QWidget;
  intro->setObjectName("modeIntro");
  auto *introLayout = new QVBoxLayout(intro);
  introLayout->setContentsMargins(0, 0, 0, 0);
  introLayout->setSpacing(8);
  auto *heading = label("Second Chances");
  heading->setStyleSheet("font-size:22px;font-weight:500");
  introLayout->addWidget(heading);
  introLayout->addWidget(label("Up to five recent reading misses. Optional, "
                               "separate from your regular practice."));
  layout->addWidget(intro);
  start = new QPushButton("Start Second Chances");
  start->setObjectName("startSecondChances");
  start->setProperty("role", "primary");
  start->setCursor(Qt::PointingHandCursor);
  start->setMinimumHeight(44);
  start->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  layout->addWidget(start, 0, Qt::AlignLeft);
  progress = label({});
  progress->setObjectName("modeProgress");
  layout->addWidget(progress);
  exercise = new QWidget;
  exercise->setObjectName("modeExercise");
  auto *body = new QVBoxLayout(exercise);
  body->setContentsMargins(0, 4, 0, 0);
  body->setSpacing(18);
  word = label({}, "modeWord");
  word->setAlignment(Qt::AlignCenter);
  word->setStyleSheet("font-family:'Noto Sans CJK JP';font-size:52px");
  body->addWidget(word);
  input = new QLineEdit;
  input->setObjectName("modeReading");
  input->setAccessibleName("Second Chances reading");
  input->setPlaceholderText("Type the reading · kana or romaji");
  input->setStyleSheet("font-family:'Noto Sans CJK JP';font-size:28px");
  body->addWidget(input);
  solution = label({}, "modeSolution");
  solution->setStyleSheet("font-family:'Noto Sans CJK JP';font-size:32px");
  body->addWidget(solution);
  auto *resultRow = new QWidget;
  auto *resultLayout = new QHBoxLayout(resultRow);
  resultLayout->setContentsMargins(0, 0, 0, 0);
  resultLayout->setSpacing(10);
  resultTitle = label({}, "modeResultTitle");
  resultIcon = new FeedbackIconLabel(resultTitle);
  resultIcon->setObjectName("modeResultIcon");
  resultTitle->setStyleSheet("font-weight:600");
  resultLayout->addWidget(resultIcon, 0, Qt::AlignVCenter);
  resultLayout->addWidget(resultTitle, 1, Qt::AlignVCenter);
  body->addWidget(resultRow);
  feedback = label({}, "modeFeedback");
  body->addWidget(feedback);
  check = new QPushButton("Check reading");
  check->setObjectName("modeCheck");
  check->setProperty("role", "primary");
  check->setCursor(Qt::PointingHandCursor);
  check->setMinimumHeight(44);
  check->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  body->addWidget(check, 0, Qt::AlignLeft);
  next = new QPushButton("Next");
  next->setObjectName("modeNext");
  next->setProperty("role", "primary");
  next->setCursor(Qt::PointingHandCursor);
  next->setMinimumHeight(44);
  next->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  body->addWidget(next, 0, Qt::AlignLeft);
  end = new QPushButton("End mode");
  end->setObjectName("modeEnd");
  end->setProperty("role", "control");
  end->setCursor(Qt::PointingHandCursor);
  end->setMinimumHeight(44);
  end->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  body->addWidget(end, 0, Qt::AlignLeft);
  layout->addWidget(exercise);
  layout->addStretch();
  exercise->hide();
  connect(start, &QPushButton::clicked, this, &PracticeModes::begin);
  connect(check, &QPushButton::clicked, this, &PracticeModes::grade);
  connect(input, &QLineEdit::returnPressed, this, [this] {
    if (checked)
      advance();
    else
      grade();
  });
  connect(next, &QPushButton::clicked, this, &PracticeModes::advance);
  connect(end, &QPushButton::clicked, this, [this] {
    if (checked) {
      queue.resize(position + 1);
      advance();
    } else {
      queue.clear();
      exercise->hide();
      start->show();
      progress->setText("Mode ended. Regular practice is unchanged.");
    }
  });
  QSqlQuery q(store.db);
  q.exec("CREATE TABLE IF NOT EXISTS mode_events(id TEXT PRIMARY KEY, "
         "created_at TEXT NOT NULL, event_json TEXT NOT NULL)");
}
QVector<Subject> PracticeModes::candidates() const {
  QSet<int> wanted;
  QSqlQuery q(store.db);
  q.exec("SELECT DISTINCT subject_id FROM attempts WHERE correct=0 AND "
         "dimension='Reading' AND julianday(created_at)>=julianday('now','-7 "
         "days') AND julianday(created_at)<=julianday('now')");
  while (q.next())
    wanted.insert(q.value(0).toInt());
  QVector<Subject> result;
  for (const auto &s : pool)
    if (wanted.contains(s.id) && !s.readings.isEmpty())
      result.append(s);
  return result;
}
void PracticeModes::begin() {
  queue = candidates();
  position = 0;
  correctCount = 0;
  for (int i = queue.size() - 1; i > 0; --i)
    queue.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
  if (queue.size() > 5)
    queue.resize(5);
  session = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (queue.isEmpty()) {
    progress->setText("No eligible reading misses in the last seven days.");
    return;
  }
  start->hide();
  exercise->show();
  present();
}
void PracticeModes::present() {
  if (position >= queue.size()) {
    exercise->hide();
    start->show();
    progress->setText(QString("Finished · %1 / %2 readings correct. Regular "
                              "practice is unchanged.")
                          .arg(correctCount)
                          .arg(queue.size()));
    return;
  }
  checked = false;
  input->clear();
  input->setEnabled(true);
  input->setFocus();
  word->setText(queue[position].characters);
  solution->clear();
  resultIcon->parentWidget()->hide();
  feedback->clear();
  feedback->hide();
  progress->setText(QString("%1 / %2 · %3 reading")
                        .arg(position + 1)
                        .arg(queue.size())
                        .arg(queue[position].readingKind.isEmpty()
                                 ? "Word"
                                 : queue[position].readingKind));
  check->show();
  next->hide();
  next->setText(position + 1 == queue.size() ? "Finish" : "Next");
  elapsed.start();
}
void PracticeModes::grade() {
  if (checked || position >= queue.size())
    return;
  if (input->text().trimmed().isEmpty()) {
    feedback->setText("Try a reading first.");
    feedback->show();
    return;
  }
  Challenge c;
  c.subject = queue[position];
  c.reading = true;
  c.mode = Mode::Typing;
  auto hint = retries ? c.readingRetryHint(input->text()) : QString();
  if (!hint.isEmpty()) {
    feedback->setText(hint);
    feedback->show();
    return;
  }
  correct = c.accepts(input->text());
  checked = true;
  input->setEnabled(false);
  solution->setText(c.answer());
  const int theme = qApp->property("mojiNookTheme").toInt();
  const auto palette = colors(theme);
  resultTitle->setText(correct ? "Correct" : "Incorrect");
  resultTitle->setStyleSheet(QString("color:%1;font-weight:600")
                                 .arg(correct ? palette.accent
                                              : palette.negative));
  resultIcon->setPixmap(symbolPixmap(!correct,
                                     QColor(correct ? palette.accent
                                                    : palette.negative),
                                     28, true));
  resultIcon->setAccessibleName(correct ? "Correct answer"
                                        : "Incorrect answer");
  resultIcon->setToolTip(resultIcon->accessibleName());
  resultIcon->parentWidget()->show();
  feedback->setText(c.subject.meanings.join(" / "));
  QSqlQuery q(store.db);
  q.prepare("SELECT json_extract(event_json,'$.submitted_answer') FROM "
            "attempts WHERE subject_id=? AND correct=0 AND dimension='Reading' "
            "ORDER BY id DESC LIMIT 1");
  q.addBindValue(c.subject.id);
  if (q.exec() && q.next() && !q.value(0).toString().isEmpty())
    feedback->setText((feedback->text().isEmpty() ? QString() :
                       feedback->text() + "\n") +
                      "Earlier answer: " + q.value(0).toString());
  feedback->setVisible(!feedback->text().isEmpty());
  check->hide();
  next->show();
  next->setFocus();
}
void PracticeModes::advance() {
  if (!checked || position >= queue.size())
    return;
  const auto &s = queue[position];
  QJsonObject event{
      {"schema", 1},
      {"mode", "second_chances"},
      {"session_id", session},
      {"position", position + 1},
      {"subject_id", s.id},
      {"characters", s.characters},
      {"level", s.level},
      {"srs_stage", s.stage},
      {"dimension", "Reading"},
      {"submitted_answer", input->text()},
      {"accepted_readings", QJsonArray::fromStringList(s.readings)},
      {"correct", correct},
      {"elapsed_ms", double(elapsed.elapsed())}};
  QSqlQuery q(store.db);
  q.prepare("INSERT INTO mode_events VALUES(?,?,?)");
  q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
  q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  q.addBindValue(
      QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
  if (!q.exec()) {
    feedback->setText(
        "Could not save. Your answer is still here; try Next again.");
    return;
  }
  correctCount += correct;
  ++position;
  present();
}
