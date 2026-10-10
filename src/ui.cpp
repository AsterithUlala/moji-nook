#include "ui.h"
#include "performance.h"
#include "style.h"
#include "theme.h"
#include "ui_helpers.h"

App::App(Store &s, const QString &path, QObject *parent)
    : QObject(parent), store(s), progression(s.db), directory(path),
      settings(path + "/settings.ini", QSettings::IniFormat), sync(s, this),
      anki(s, this) {
  // Honor an existing explicit mute/volume preference when consolidating audio.
  if (!settings.contains("speech/enabled") && settings.contains("anki/audio_enabled"))
    settings.setValue("speech/enabled", settings.value("anki/audio_enabled"));
  if (!settings.contains("speech/volume") && settings.contains("anki/audio_volume"))
    settings.setValue("speech/volume", settings.value("anki/audio_volume"));
  if (!settings.contains("practice/source")) {
    const bool hasAnki = settings.value("anki/enabled", false).toBool();
    const bool hasWaniKani = !readToken().isEmpty() || !store.cache().isEmpty();
    settings.setValue("practice/source", hasAnki ? (hasWaniKani ? "both" : "anki")
                                               : "wanikani");
    settings.sync();
  }
  clock.intervalMs =
      qBound(1, settings.value("interval_minutes", 5).toInt(), 120) * 60000;
  clock.sessionSize = qBound(1, settings.value("session_count", 2).toInt(),
             PracticeClock::maxSessionSize);
  snoozeMinutes = qBound(1, settings.value("snooze_minutes", 30).toInt(), 240);
  dashboard.setWindowTitle("Moji Nook · Japanese practice");
  dashboard.setWindowIcon(appIcon());
  const auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
  dashboard.resize(qMin(1360, qMax(640, available.width() - 48)),
                   qMin(820, qMax(480, available.height() - 48)));
  buildDashboard();
  progression.refresh({}, false);
  statsRefresh.setSingleShot(true);
  connect(&statsRefresh, &QTimer::timeout, this, &App::refreshVisibleStats);
  connect(tabs, &QTabWidget::currentChanged, this, &App::refreshVisibleStats);
  applyTheme(theme->currentIndex());
  applyFeedbackSettings();
  speech.prepare();
  connect(ankiPanel, &AnkiPanel::practiceRequested, this, [this] {
    practiceSource->setCurrentIndex(practiceSource->findData("anki"));
  });
  connect(ankiPanel, &AnkiPanel::changed, this, [this] {
    applyFeedbackSettings();
    invalidateStats();
  });
  connect(&anki, &AnkiSource::finished, this, [this](bool ok, const QString &) {
    if (ok) {
      refreshPool();
      invalidateStats();
      // Anki-only first use starts with an empty pool and no running interval.
      if (!pool.isEmpty() && !clock.pending && !clock.timer.isActive())
        clock.request();
    }
  });
  connect(ankiPanel, &AnkiPanel::previewRequested, this, [this] {
    const auto cards =
        anki.subjects(settings.value("anki/profile").toString(),
                      settings.value("anki/decks").toStringList());
    if (cards.isEmpty()) {
      status->setText("Sync a compatible Anki deck first.");
      return;
    }
    Challenge c;
    c.subject = cards[QRandomGenerator::global()->bounded(int(cards.size()))];
    c.mode = Mode::Recall;
    c.reading = true;
    previewCard.present(c, clock.intervalMs / 60000);
    previewCard.setCorner(corner->currentIndex());
  });
  connect(&card, &Card::graded, &audio, &FeedbackAudio::play);
  connect(&previewCard, &Card::graded, &audio, &FeedbackAudio::play);
  connect(&audio, &FeedbackAudio::unavailable, this, [this] {
    status->setText("Sound output unavailable · practice still works. Check "
                    "your audio device or disable sounds in Settings.");
  });
  tray.setIcon(appIcon());
  tray.setToolTip("Moji Nook · extra Japanese practice");
  trayMenu.addAction("Practice now", this, &App::requestPractice);
  trayMenu.addAction("Overview", this, &App::showDashboard);
  trayMenu.addAction("Settings", this, &App::showSettings);
  pauseAction = trayMenu.addAction("Pause practice");
  pauseAction->setCheckable(true);
  connect(pauseAction, &QAction::toggled, this, [this](bool paused) {
    clock.setPaused(paused);
    if (auto *toggle = dashboard.findChild<QPushButton *>("pauseReminders"))
      toggle->setText(paused ? "Resume reminders" : "Pause reminders");
    if (paused)
      card.hide();
    else if (clock.pending)
      card.show();
    if (paused) {
      ++recapEpoch;
      recap.dismiss();
    }
  });
  snoozeAction = trayMenu.addAction({}, this, &App::snoozePractice);
  refreshSnoozeLabels();
  trayMenu.addSeparator();
  trayMenu.addAction("Sync WaniKani", this, &App::syncNow);
  trayMenu.addSeparator();
  trayMenu.addAction("Quit Moji Nook", qApp, &QApplication::quit);
  tray.setContextMenu(&trayMenu);
  connect(&tray, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger)
              showDashboard();
          });
  connect(&card, &Card::dashboardRequested, this, &App::showDashboard);
  connect(&clock, &PracticeClock::due, this, &App::showCard);
  connect(&card, &Card::completed, this,
          [this](Challenge c, int outcome, qint64 ms) {
            PerformanceSpan timing("complete-answer");
            if (!store.record(c,
                              outcome < 0 ? std::nullopt
                                          : std::optional<bool>(outcome == 1),
                              ms)) {
              status->setText("Could not save this answer: " + store.error);
              pendingProgressNotices.clear();
              ++recapEpoch;
              recap.dismiss();
              clock.complete();
              pauseAction->setChecked(true);
              showDashboard();
              return;
            }
            if (progression.refresh(pool, true))
              pendingProgressNotices += progression.takeNotices();
            else
              status->setText("Answer saved · word tiles could not update: " + progression.error);
            const bool ordinaryFinish = outcome >= 0 &&
                !c.event["end_session"].toBool() &&
                clock.position >= clock.sessionTotal;
            if (outcome < 0 || c.event["end_session"].toBool())
              clock.complete();
            else {
              transitionOutcome =
                  c.mode == Mode::Recall && clock.position < clock.sessionTotal
                      ? outcome
                      : -1;
              clock.advance();
            }
            if (!clock.pending) {
              const auto notices = std::exchange(pendingProgressNotices, {});
              const int epoch = ++recapEpoch;
              if (ordinaryFinish && !notices.isEmpty())
                QTimer::singleShot(200, this, [this, notices, epoch] {
                  if (epoch == recapEpoch && !clock.pending && !clock.paused) {
                    recap.setPlacement(settings.value("monitor").toString(),
                                       corner->currentIndex());
                    recap.present(notices, progression.state());
                  }
                });
            }
            invalidateStats();
          });
  connect(&sync, &Sync::progress, status, &QLabel::setText);
  connect(&sync, &Sync::progress, this, [this](const QString &message) {
    setSourceStatus(connectionStatus, "connecting", message);
  });
  connect(&sync, &Sync::finished, this,
          [this](bool ok, const QString &message) {
            status->setText(message);
            setSourceStatus(connectionStatus,
                                           ok ? "connected" : "error", message);
            syncButton->setEnabled(true);
            if (ok) {
              refreshPool();
              invalidateStats();
              if (!clock.pending && !clock.timer.isActive())
                clock.request();
            }
          });
  connect(&heartbeat, &QTimer::timeout, this, [this] {
    const auto age = syncAgeText(lastSyncAt);
    if (syncAge->text() != age)
      syncAge->setText(age);
    QString next;
    if (clock.paused)
      next = "Practice is paused";
    else if (clock.pending)
      next = QString("Practice session · card %1 of %2 · timer "
                     "starts after the session")
                 .arg(clock.position)
                 .arg(clock.sessionTotal);
    else if (clock.timer.isActive()) {
      int secs = qMax(0, (clock.timer.remainingTime() + 999) / 1000);
      next = QString("Next practice in %1:%2")
                 .arg(secs / 60)
                 .arg(secs % 60, 2, 10, QChar('0'));
    } else
      next = pool.isEmpty() ? "Sync your chosen source to begin"
                            : "Ready to practice";
    if (schedule->text() != next)
      schedule->setText(next);
    refreshHomeState();
  });
  heartbeat.setTimerType(Qt::PreciseTimer);
  heartbeat.start(250);
  connect(&autoSync, &QTimer::timeout, this, [this] {
    syncIfDue();
    if (!demoMode)
      ankiPanel->syncIfDue();
    if (dashboard.isVisible())
      invalidateStats();
  });
  autoSync.start(60 * 1000);
}
QString App::readToken() const {
  QFile f(directory + "/token");
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QString::fromUtf8(f.readAll()).trimmed();
}
bool App::saveToken(const QString &token) {
  QSaveFile f(directory + "/token");
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  auto bytes = token.trimmed().toUtf8();
  return f.write(bytes) == bytes.size() && f.commit();
}
void App::refreshPool() {
  if (demoMode) {
    pool = demoSubjects();
    progression.refresh(pool, false);
    if (auto *summary = dashboard.findChild<QLabel *>("practiceSourceSummary"))
      summary->setText("Sample words · demo practice");
    syncAge->hide();
    return;
  }
  auto c = store.cache();
  lastSyncAt = QDateTime::fromString(c["synced_at"].toString(), Qt::ISODate);
  syncAge->setText(syncAgeText(lastSyncAt));
  syncAge->setToolTip(lastSyncAt.isValid() ? lastSyncAt.toLocalTime().toString(
                                                 "dddd, MMMM d, yyyy · h:mm AP")
                                           : "No successful sync yet");
  int maxLevel = c["user"]
                     .toObject()["subscription"]
                     .toObject()["max_level_granted"]
                     .toInt(60);
  const auto selectedSource = settings.value("practice/source", "wanikani").toString();
  pool.clear();
  if (selectedSource != "anki")
    pool = parseSubjects(c["subjects"].toArray(), c["assignments"].toArray(),
                         c["study_materials"].toArray(), maxLevel);
  if (selectedSource != "wanikani" && settings.value("anki/enabled", false).toBool())
    pool += anki.subjects(settings.value("anki/profile").toString(),
                          settings.value("anki/decks").toStringList());
  progression.refresh(pool, false);
  syncAge->setVisible(selectedSource != "anki");
  if (auto *summary = dashboard.findChild<QLabel *>("practiceSourceSummary")) {
    summary->setText(selectedSource == "anki" ? "Practicing with Anki"
                       : selectedSource == "both" ? "WaniKani + Anki · overlapping words may repeat"
                                                  : "Practicing with WaniKani");
    summary->setToolTip(selectedSource == "both"
        ? "Choose one source in Settings → Practice for simpler practice."
        : QString());
  }
}
void App::setPracticeSource(const QString &source) {
  if (source != "wanikani" && source != "anki" && source != "both")
    return;
  settings.setValue("practice/source", source);
  if (source != "wanikani")
    settings.setValue("anki/enabled", true);
  settings.sync();
  syncButton->setText(source == "anki" ? "Anki sync settings" : "Sync WaniKani");
  refreshPool();
  applyFeedbackSettings();
  invalidateStats();
  if (!demoMode && pool.isEmpty())
    status->setText(source == "anki" ? "Sync an Anki deck to begin practice."
                                    : "Connect and sync your chosen source to begin practice.");
}
void App::invalidateStats() {
  dirtyTabs.fill(true);
  // Complete/persist first. Never rebuild rich documents during a card
  // transition.
  if (!clock.pending)
    statsRefresh.start(0);
}
void App::refreshStats() {
  dirtyTabs.fill(true);
  refreshVisibleStats();
}
void App::refreshVisibleStats() {
  if (!dashboard.isVisible())
    return;
  const int page = tabs->currentIndex();
  if (page < 0 || page >= dirtyTabs.size() || !dirtyTabs[page])
    return;
  PerformanceSpan timing("refresh-visible-page");
  dirtyTabs[page] = false;
  dashboard.setProperty("statsRenderCount",
                        dashboard.property("statsRenderCount").toInt() + 1);
  if (page == 0) {
    refreshHomeState();
    const auto wordState = progression.state();
    homeTiles->setTiles(wordState.tiles.mid(0, 3));
    homeTiles->setVisible(!wordState.tiles.isEmpty());
    homeEmpty->setVisible(wordState.tiles.isEmpty());
    homeInspection->setVisible(!wordState.tiles.isEmpty());
    homeWordCount->setText(QString("%1 settled · %2 practiced words")
        .arg(wordState.settled).arg(wordState.tiles.size()));
    modes->setPool(pool);
    homeFocus->setVisible(!modes->candidates().isEmpty());
    auto overview = recolor(store.statsHtml(pool), theme->currentIndex());
    overview.remove("<h1>Your practice</h1>");
    const int tableEnd = overview.indexOf("</table>");
    if (tableEnd >= 0)
      overview = overview.left(tableEnd + 8);
    overview.replace("td{padding:14px}", "td{padding:4px}");
    overview.replace("cellspacing='8'", "cellspacing='0'");
    overview.replace("<font size='6'>", "<font size='4'>");
    stats->setHtml(overview);
    stats->setToolTip("Rolling 24-hour and 7-day practice accuracy, including "
                      "self-rated recall. Skips excluded.");
  } else if (page == 1) {
    insights->setInsights(
        store, recolor(store.insightsHtml(pool), theme->currentIndex()));
  } else if (page == 2) {
    QSqlQuery count(store.db);
    count.exec("SELECT COUNT(*) FROM attempts");
    const int total = count.next() ? count.value(0).toInt() : 0;
    const int pages = qMax(1, (total + 49) / 50);
    historyPage = qBound(0, historyPage, pages - 1);
    history->setHtml(
        recolor(store.historyHtml(historyPage), theme->currentIndex()));
    historyPageLabel->setText(QString("Page %1 of %2 · %3 events")
                                  .arg(historyPage + 1)
                                  .arg(pages)
                                  .arg(total));
    historyPrevious->setEnabled(historyPage > 0);
    historyNext->setEnabled(historyPage + 1 < pages);
  } else if (page == 3) {
    milestones->setState(progression.state());
  } else if (page == 4) {
    modes->setPool(pool);
    modes->setReadingRetries(
        settings.value("reading_retry_enabled", true).toBool());
  }
}
void App::applyFeedbackSettings() {
  audio.configure(settings.value("sound_enabled", true).toBool(),
                  settings.value("sound_volume", 20).toInt());
  const bool reduced = settings.value("reduced_motion", false).toBool();
  card.setReducedMotion(reduced);
  previewCard.setReducedMotion(reduced);
  recap.setReducedMotion(reduced);
  const bool retries = settings.value("reading_retry_enabled", true).toBool();
  card.setReadingRetries(retries);
  previewCard.setReadingRetries(retries);
  applySpeechSettings();
}
void App::snoozePractice() {
  ++recapEpoch;
  recap.dismiss();
  pauseAction->setChecked(false);
  card.close();
  clock.timer.start(snoozeMinutes * 60000);
  clock.snoozed = true;
  refreshHomeState();
}
void App::refreshSnoozeLabels() {
  const QString text = QString("Snooze %1 minutes").arg(snoozeMinutes);
  if (snoozeAction)
    snoozeAction->setText(text);
  if (auto *snooze = dashboard.findChild<QPushButton *>("homeSnooze"))
    snooze->setText(text);
}
void App::refreshHomeState() {
  const bool ready = !pool.isEmpty();
  homeState->setText(!ready ? "No words available"
      : clock.paused ? "Paused"
      : clock.pending ? "Session in progress"
      : "Ready");
  homeBegin->setText(!ready ? "Choose a practice source"
      : clock.pending ? "Continue session" : "Practice now");
  if (auto *pause = dashboard.findChild<QPushButton *>("pauseReminders")) {
    pause->setText(clock.paused ? "Resume reminders" : "Pause reminders");
    pause->setVisible(ready);
  }
  dashboard.findChild<QPushButton *>("homeSnooze")->setVisible(ready);
}
void App::requestPractice() {
  ++recapEpoch;
  recap.dismiss();
  pauseAction->setChecked(false);
  if (clock.pending)
    card.show();
  else
    clock.request();
}
void App::showSettings() {
  tabs->setCurrentWidget(settingsPage);
  showDashboard();
}
void App::applyTheme(int index) {
  qApp->setProperty("mojiNookTheme", index);
  settings.setValue("theme", index);
  settings.sync();
  const auto c = colors(index);
  QPalette palette;
  palette.setColor(QPalette::Window, c.background);
  palette.setColor(QPalette::WindowText, c.text);
  palette.setColor(QPalette::Base, c.background);
  palette.setColor(QPalette::AlternateBase, c.surface);
  palette.setColor(QPalette::Text, c.text);
  palette.setColor(QPalette::Button, c.surface);
  palette.setColor(QPalette::ButtonText, c.text);
  palette.setColor(QPalette::PlaceholderText, c.muted);
  palette.setColor(QPalette::Highlight, c.action);
  palette.setColor(QPalette::HighlightedText, c.actionInk);
  palette.setColor(QPalette::Link, c.accent);
  palette.setColor(QPalette::LinkVisited, c.accent);
  qApp->setPalette(palette);
  qApp->setStyleSheet(appStyle(index));
  if (headerMark)
    headerMark->setMark(brandMark(index));
  const bool night = QColor(c.background).lightness() < 128;
  if (auto *toggle = dashboard.findChild<QCheckBox *>("nightMode")) {
    const QSignalBlocker blocker(toggle);
    toggle->setChecked(night);
  }
  card.applyTheme(index);
  previewCard.applyTheme(index);
  recap.setTheme(index);
  milestones->setTheme(index);
  homeTiles->setTheme(index);
  if (dashboard.isVisible() && tabs->currentWidget() == settingsPage) {
    auto *mode = dashboard.findChild<QComboBox *>("previewChallengeMode");
    showCardPreview(mode ? mode->currentIndex() : 0);
  }
  refreshStats();
}
void App::syncNow() {
  if (demoMode) {
    status->setText(
        "Demo mode · sample words, separate local stats, no API requests");
    return;
  }
  if (sync.busy())
    return;
  syncButton->setEnabled(false);
  setSourceStatus(connectionStatus, "connecting",
                                  "Syncing WaniKani…");
  sync.start(readToken());
}
QString syncAgeText(const QDateTime &last, const QDateTime &now) {
  if (!last.isValid())
    return "WaniKani · not synced yet";
  const auto seconds = qMax(qint64(0), last.secsTo(now));
  QString age = seconds < 60      ? "just now"
                : seconds < 3600  ? QString("%1 min ago").arg(seconds / 60)
                : seconds < 86400 ? QString("%1 hr ago").arg(seconds / 3600)
                                  : QString("%1 days ago").arg(seconds / 86400);
  return "WaniKani · synced " + age + " · daily auto-sync";
}
bool automaticSyncDue(const QDateTime &lastSuccess,
                      const QDateTime &lastAttempt, const QDateTime &now) {
  return (!lastSuccess.isValid() || lastSuccess.secsTo(now) >= 86400) &&
         (!lastAttempt.isValid() || lastAttempt.secsTo(now) >= 3600);
}
void App::syncIfDue() {
  const auto now = QDateTime::currentDateTimeUtc();
  if (demoMode || settings.value("practice/source", "wanikani").toString() == "anki" ||
      sync.busy() || readToken().isEmpty())
    return;
  if (!automaticSyncDue(lastSyncAt, lastAutoAttempt, now))
    return;
  lastAutoAttempt = now;
  syncNow();
}
void App::showCard() {
  ++recapEpoch;
  recap.dismiss();
  previewCard.hide(); // The real card takes the sample's place.
  PerformanceSpan timing("select-and-present-card");
  if (pool.isEmpty()) {
    // Keep the interval running so reminders resume once words arrive.
    clock.complete();
    card.hide();
    status->setText("No learned items cached for your chosen source. "
                    "Open Settings to connect and sync it.");
    return;
  }
  QVector<double> weights;
  const QList<int> defaults = {40, 30, 20, 10, 5};
  for (int i = 0; i < defaults.size(); ++i)
    weights.append(
        qBound(1,
               settings.value(QString("mastery_weight_%1").arg(i), defaults[i])
                   .toInt(),
               100) /
        10.0);
  QVector<Subject> ankiPool, wkPool;
  for (const auto &s : pool)
    (s.source == "anki" ? ankiPool : wkPool).append(s);
  const int ankiShare =
      qBound(0, settings.value("anki/share", 30).toInt(), 100);
  const bool useAnki = !ankiPool.isEmpty() &&
                       (wkPool.isEmpty() ||
                        QRandomGenerator::global()->bounded(100) < ankiShare);
  auto evidence = store.evidence();
  const auto ankiEvidence = anki.evidence(ankiPool);
  for (auto it = ankiEvidence.begin(); it != ankiEvidence.end(); ++it) {
    evidence[it.key()].recent += it->recent;
    evidence[it.key()].persistent += it->persistent;
  }
  auto challenge =
      picker.next(useAnki ? ankiPool : wkPool, {}, store.recentSubjects(),
                  evidence, recentPercent->value(), persistentPercent->value(),
                  recallPercent->value(), typedPercent->value(), weights,
                  newestPercent->value());
  challenge.event["session_id"] = clock.sessionId;
  challenge.event["session_position"] = clock.position;
  challenge.event["session_total"] = clock.sessionTotal;
  challenge.event["interval_minutes"] = clock.intervalMs / 60000;
  challenge.event["recent_share"] = recentPercent->value();
  challenge.event["persistent_share"] = persistentPercent->value();
  challenge.event["recall_share"] = recallPercent->value();
  challenge.event["typed_share"] = typedPercent->value();
  QJsonArray weightSnapshot;
  for (double weight : weights)
    weightSnapshot.append(weight);
  challenge.event["mastery_weights"] = weightSnapshot;
  challenge.event["newest_share"] = newestPercent->value();
  challenge.event["newest_count"] = 30;
  challenge.event["anki_share"] = ankiShare;
  challenge.event["picker_version"] = 4;
  challenge.event["demo"] = demoMode;
  challenge.event["transition_outcome"] = transitionOutcome;
  transitionOutcome = -1;
  QSqlQuery observation(store.db);
  observation.prepare(
      "SELECT observed_at,data_json FROM wk_latest WHERE subject_id=?");
  observation.addBindValue(challenge.subject.id);
  if (observation.exec() && observation.next()) {
    challenge.event["wk_observed_at"] = observation.value(0).toString();
    challenge.event["wk_statistics"] =
        QJsonDocument::fromJson(observation.value(1).toByteArray()).object();
  }
  card.present(challenge, clock.intervalMs / 60000);
  card.setProgressTile(progression.tile(challenge.subject.id));
  card.setCorner(corner->currentIndex());
}
void App::showDashboard() {
  dashboard.show();
  refreshStats();
  dashboard.raise();
  dashboard.activateWindow();
}
void App::start(bool demo, bool trayOnly) {
  demoMode = demo;
  refreshPool();
  if (!demo && pool.isEmpty())
    tabs->setCurrentWidget(settingsPage);
  refreshStats();
  tray.show();
  if (!trayOnly || pool.isEmpty())
    showDashboard();
  if (!pool.isEmpty())
    QTimer::singleShot(800, &clock, [this] {
      if (!clock.pending && !clock.timer.isActive())
        clock.request();
    });
  const auto synced =
      QDateTime::fromString(store.cache()["synced_at"].toString(), Qt::ISODate);
  status->setText(demo ? "Demo mode · sample words and separate stats"
                  : synced.isValid() && settings.value("practice/source").toString() != "anki"
                      ? "Last synced " +
                            synced.toLocalTime().toString("MMM d, h:mm ap") +
                            " · practice stays local"
                      : pool.isEmpty() ? "Connect your chosen source in Settings to begin"
                                       : "Your cached words are ready · practice stays local");
  if (!demo)
    QTimer::singleShot(200, this, &App::syncIfDue);
  if (!demo)
    QTimer::singleShot(1000, ankiPanel, &AnkiPanel::syncIfDue);
}
