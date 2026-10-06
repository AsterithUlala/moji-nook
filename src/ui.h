#pragma once
#include "anki_widgets.h"
#include "charts.h"
#include "core.h"
#include "feedback.h"
#include "modes.h"
#include "sync.h"
#include "speech.h"
#include "progression_popup.h"
#include "theme.h"
#include <QtWidgets>

class BrandMarkLabel;

class PracticeClock : public QObject {
  Q_OBJECT
public:
  explicit PracticeClock(QObject *parent = nullptr);
  QTimer timer;
  static constexpr int maxSessionSize = 25;
  int intervalMs = 300000;
  int sessionSize = 2, sessionTotal = 2, position = 1;
  QString sessionId;
  bool pending = false, paused = false;
  void request();
  void complete();
  void advance();
  void setPaused(bool value);
signals:
  void due();
};

class Card : public QWidget {
  Q_OBJECT
public:
  explicit Card(QWidget *parent = nullptr);
  void present(const Challenge &challenge, int minutes);
  void setCorner(int index);
  void setMonitor(const QString &screenName);
  void applyTheme(int index);
  void setReducedMotion(bool reduced);
  void setReadingRetries(bool enabled) { readingRetries = enabled; }
  void setAnkiAudio(bool enabled, int volume, bool autoplay = true);
  void setSpeech(JapaneseSpeech *engine, bool enabled, bool autoplay,
                 bool preferRecorded);
  void setProgressTile(const std::optional<ProgressTile> &tile);
  Challenge challenge;
signals:
  void completed(Challenge challenge, int outcome, qint64 activeMs);
  void dashboardRequested();
  void graded(bool correct);

protected:
  void closeEvent(QCloseEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  bool eventFilter(QObject *object, QEvent *event) override;

private:
  QLabel *meta, *challengeType, *supplement, *characters, *frontContext,
      *prompt, *answer, *context, *feedback, *hint, *kana, *submitted,
      *answerCaption, *missIcon;
  QLineEdit *input;
  QPushButton *reveal, *check, *remember, *miss, *done, *dismiss,
      *exampleToggle, *listen;
  QLabel *voiceCaption;
  QLabel *wordProgress, *wordKind;
  ChallengeMark *challengeMark;
  QScrollArea *cardScroll;
  QWidget *cardContent, *submittedRow;
  QWidget *choiceArea;
  QVBoxLayout *choiceLayout;
  QElapsedTimer active;
  bool engaged = false, resolved = false, pending = false;
  bool grantingClickFocus = false;
  int result = -1, cornerIndex = 0, themeIndex = defaultTheme;
  QString monitorName;
  QPointer<QScreen> placementScreen;
  QMetaObject::Connection screenGeometryConnection;
  CardAccent *accent;
  bool reducedMotion = true;
  bool readingRetries = true;
  QLabel *readingWarning;
  AnkiDetails *ankiDetails;
  QPointer<JapaneseSpeech> speech;
  bool speechEnabled = false, speechAutoplay = true, preferRecording = false;
  QString speechKey;
  bool answerVisible = false;
  int motionEpoch = 0;
  void showAnswer();
  void speakAnswer();
  void grade(bool correct, const QString &response);
  void fitCard();
  QScreen *displayScreen() const;
  void finish(bool endSession = false);
};

class App : public QObject {
  Q_OBJECT
public:
  App(Store &store, const QString &dataDir, QObject *parent = nullptr);
  void start(bool demo, bool trayOnly);
  void showDashboard();
  bool savePreview(const QString &directory);
  bool saveAnkiPreview(const QString &directory);
  bool saveProgressionPreview(const QString &directory);
  void showSettings();

private:
  Store &store;
  Progression progression;
  QString directory;
  QSettings settings;
  Sync sync;
  AnkiSource anki;
  AnkiPanel *ankiPanel;
  Picker picker;
  QVector<Subject> pool;
  PracticeClock clock;
  Card card;
  Card previewCard;
  ProgressRecap recap;
  QVector<ProgressNotice> pendingProgressNotices;
  int recapEpoch = 0;
  QMainWindow dashboard;
  BrandMarkLabel *headerMark = nullptr;
  QSystemTrayIcon tray;
  QMenu trayMenu;
  QAction *pauseAction;
  QTextBrowser *stats;
  QTextBrowser *history;
  ProgressionPage *milestones;
  QTabWidget *tabs;
  QWidget *settingsPage;
  QLabel *historyPageLabel;
  QPushButton *historyPrevious, *historyNext;
  int historyPage = 0;
  int transitionOutcome = -1;
  InsightsBrowser *insights;
  PracticeModes *modes;
  FeedbackAudio audio;
  JapaneseSpeech speech;
  QLabel *status, *schedule, *connectionStatus;
  QLabel *syncAge;
  QLabel *homeState, *homeCadence, *homeWordCount, *homeEmpty, *homeInspection;
  QPushButton *homeBegin, *homeFocus;
  WordTileCanvas *homeTiles;
  void refreshHomeState();
  void snoozePractice();
  void refreshSnoozeLabels();
  int snoozeMinutes = 30;
  QAction *snoozeAction = nullptr;
  QDateTime lastSyncAt, lastAutoAttempt;
  QLineEdit *tokenInput;
  QSpinBox *interval;
  QSpinBox *sessionCount;
  QSpinBox *snoozeLength;
  QSlider *newestPercent, *recentPercent, *persistentPercent, *recallPercent,
      *typedPercent;
  QComboBox *corner, *monitor, *theme, *practiceSource;
  QPushButton *syncButton;
  QTimer heartbeat, autoSync;
  QTimer statsRefresh;
  QVector<bool> dirtyTabs = QVector<bool>(6, true);
  bool demoMode = false;
  QString readToken() const;
  bool saveToken(const QString &token);
  void refreshPool();
  void refreshStats();
  void invalidateStats();
  void refreshVisibleStats();
  void syncNow();
  void syncIfDue();
  void showCard();
  void requestPractice();
  void buildDashboard();
  void buildSpeechSettings(QVBoxLayout *layout);
  void applySpeechSettings();
  void showCardPreview(int mode = -1);
  void populateMonitors();
  void identifyDisplays();
  void applyTheme(int index);
  void applyFeedbackSettings();
  void setPracticeSource(const QString &source);
};

// Ignore wheel edits even while focused; let the containing page scroll.
class ScrollSafeSlider : public QSlider {
public:
  explicit ScrollSafeSlider(QWidget *parent = nullptr)
      : QSlider(Qt::Horizontal, parent) {}

protected:
  void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};
class ScrollSafeComboBox : public QComboBox {
public:
  using QComboBox::QComboBox;

protected:
  void wheelEvent(QWheelEvent *event) override { event->ignore(); }
  void paintEvent(QPaintEvent *event) override {
    QComboBox::paintEvent(event);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QRect arrow = style()->subControlRect(
        QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);
    if (arrow.isEmpty())
      return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = isEnabled()
        ? palette().color(QPalette::Text)
        : palette().color(QPalette::Disabled, QPalette::Text);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap,
                         Qt::RoundJoin));
    const QPointF center = arrow.center();
    QPainterPath chevron;
    chevron.moveTo(center.x() - 3.5, center.y() - 1.5);
    chevron.lineTo(center.x(), center.y() + 2);
    chevron.lineTo(center.x() + 3.5, center.y() - 1.5);
    painter.drawPath(chevron);
  }
};
class ScrollSafeSpinBox : public QSpinBox {
public:
  using QSpinBox::QSpinBox;

protected:
  void wheelEvent(QWheelEvent *event) override { event->ignore(); }
  void paintEvent(QPaintEvent *event) override {
    QSpinBox::paintEvent(event);
    QStyleOptionSpinBox option;
    initStyleOption(&option);
    const QRect up = style()->subControlRect(
        QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, this);
    const QRect down = style()->subControlRect(
        QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = isEnabled()
        ? palette().color(QPalette::Text)
        : palette().color(QPalette::Disabled, QPalette::Text);
    painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap,
                         Qt::RoundJoin));
    auto chevron = [&painter](const QRect &button, bool upward) {
      if (button.isEmpty())
        return;
      const QPointF center = button.center();
      const qreal direction = upward ? 1 : -1;
      QPainterPath path;
      path.moveTo(center.x() - 3, center.y() + direction * 1.5);
      path.lineTo(center.x(), center.y() - direction * 1.5);
      path.lineTo(center.x() + 3, center.y() + direction * 1.5);
      painter.drawPath(path);
    };
    chevron(up, true);
    chevron(down, false);
  }
};

QVector<Subject> demoSubjects();
QString appStyle(int theme = defaultTheme);
QIcon appIcon();
QIcon brandMark(int theme = defaultTheme);
QString syncAgeText(const QDateTime &last,
                    const QDateTime &now = QDateTime::currentDateTimeUtc());
bool automaticSyncDue(const QDateTime &lastSuccess,
                      const QDateTime &lastAttempt, const QDateTime &now);
