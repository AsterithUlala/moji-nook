#include "style.h"
#include "theme.h"
#include "ui.h"
#include "ui_helpers.h"
#include "display_preview.h"

void App::populateMonitors() {
  const QSignalBlocker blocker(monitor);
  const auto saved = settings.value("monitor").toString();
  const auto screens = orderedDisplays();
  const int primary = screens.indexOf(QGuiApplication::primaryScreen()) + 1;
  monitor->clear();
  monitor->addItem(QString("Primary display · Display %1").arg(primary), QString());
  for (int i = 0; i < screens.size(); ++i) {
    auto *screen = screens[i];
    monitor->addItem(displayLabel(screen, i + 1), screen->name());
    monitor->setItemData(monitor->count() - 1, screen->name() +
        (screen == QGuiApplication::primaryScreen() ? " · primary display" : ""),
        Qt::ToolTipRole);
  }
  if (!saved.isEmpty() && monitor->findData(saved) < 0)
    monitor->addItem("Saved display disconnected · using primary", saved);
  monitor->setCurrentIndex(qMax(0, monitor->findData(saved)));
}

void App::showCardPreview(int mode) {
  recap.dismiss();
  if (mode < 0) {
    const auto *selection = dashboard.findChild<QComboBox *>("previewChallengeMode");
    mode = selection ? selection->currentIndex() : 0;
  }
  Challenge c;
  c.subject = demoSubjects().first();
  c.subject.contextJa = "日本語を勉強しています。";
  c.subject.contextEn = "I’m studying Japanese.";
  c.mode = static_cast<Mode>(qBound(0, mode, 2));
  c.reading = true;
  if (c.mode == Mode::Choice)
    c.choices = {"にほん", "ほんじつ", "にちよう", "にっき"};
  previewCard.setWindowTitle("Moji Nook · Settings preview");
  previewCard.setMonitor(settings.value("monitor").toString());
  previewCard.setCorner(corner->currentIndex());
  previewCard.present(c, interval->value());
  previewCard.findChild<QPushButton *>("cardBrand")->setText("Moji Nook · Preview");
  const auto screens = orderedDisplays();
  const int number = screens.indexOf(previewCard.screen()) + 1;
  previewCard.findChild<QLabel *>("cardHint")->setText(
      QString("Display %1 · %2 · sample only")
          .arg(number).arg(corner->currentText().toLower()));
  previewCard.findChild<QLabel *>("cardHint")->show();
  previewCard.findChild<QPushButton *>("dismiss")->setToolTip("Close preview");
}

bool App::saveProgressionPreview(const QString &path) {
  // Synthetic visual states only, in the CLI's isolated demo profile. No grades.
  QDir().mkpath(path);
  demoMode = true;
  audio.configure(false, 0);
  speech.configure(false);
  card.setReducedMotion(true);
  recap.setReducedMotion(true);
  ProgressState state;
  auto subjects = demoSubjects();
  const QStringList extras = {
      "学校|がっこう|School", "猫|ねこ|Cat", "風|かぜ|Wind", "時間|じかん|Time",
      "友達|ともだち|Friend", "朝|あさ|Morning", "電車|でんしゃ|Train",
      "雨|あめ|Rain", "勉強|べんきょう|Study", "星|ほし|Star",
      "料理|りょうり|Cooking", "家|いえ|Home", "散歩|さんぽ|A walk",
      "音楽|おんがく|Music", "言葉|ことば|Word", "静か|しずか|Quiet",
      "気持ち|きもち|Feeling", "道|みち|Path", "花|はな|Flower",
      "お疲れさまでした|おつかれさまでした|Thank you for your work"};
  for (const auto &entry : extras) {
    const auto parts = entry.split('|');
    Subject s;
    s.id = subjects.size() + 1;
    s.characters = parts[0]; s.readings = {parts[1]}; s.meanings = {parts[2]};
    s.kind = s.characters.size() == 1 ? "kanji" : "vocabulary";
    subjects.append(s);
  }
  for (int i = 0; i < subjects.size(); ++i) {
    const auto &s = subjects[i];
    ProgressTile tile;
    tile.id = s.id; tile.characters = s.characters; tile.reading = s.reading();
    tile.meaning = s.meaning(); tile.source = "wanikani"; tile.subjectKind = s.kind;
    tile.eligible = true;
    tile.phase = i % 5 == 0 ? TilePhase::Seen : i % 3 == 0 ? TilePhase::Growing : TilePhase::Settled;
    tile.settled = tile.phase == TilePhase::Settled;
    tile.progress = tile.settled ? 1 : tile.phase == TilePhase::Growing ? .6 : .1;
    tile.readingSuccesses = tile.settled ? 3 : i % 3;
    tile.meaningSuccesses = tile.settled ? 3 : i % 2;
    tile.readingChallenge = i == 8 || i == 12;
    tile.lastAttempt = i + 1;
    state.settled += tile.settled;
    state.tiles.append(tile);
  }
  state.total = state.tiles.size();
  state.practiceDays = 20;
  dashboard.resize(1360, 820);
  dashboard.show();
  tabs->setCurrentIndex(3);
  bool ok = true;
  for (int i = 0; i < themeCount; ++i) {
    applyTheme(i);
    milestones->setState(state);
    qApp->processEvents();
    ok = dashboard.grab().save(path + QString("/composition-%1.png").arg(i)) && ok;
    if (auto *canvas = milestones->findChild<WordTileCanvas *>()) {
      canvas->focusTile(1);
      qApp->processEvents();
      ok = dashboard.grab().save(path + QString("/tile-details-%1.png").arg(i)) && ok;
    }
    recap.setPlacement({}, 0);
    recap.present({{ProgressNotice::Completed, state.tiles[1].id, state.tiles[1].characters, 0},
                   {ProgressNotice::Developed, state.tiles[3].id, state.tiles[3].characters, 0}}, state);
    qApp->processEvents();
    ok = recap.grab().save(path + QString("/recap-%1.png").arg(i)) && ok;
    recap.dismiss();
    Challenge c;
    c.subject = subjects[8]; c.mode = Mode::Recall; c.reading = true;
    card.present(c, 5);
    card.setProgressTile(state.tiles[8]);
    qApp->processEvents();
    ok = card.grab().save(path + QString("/challenge-%1.png").arg(i)) && ok;
    c.subject = subjects[9]; // One-character kanji fixture; show both subject hues.
    card.present(c, 5);
    card.setProgressTile(state.tiles[8]);
    qApp->processEvents();
    ok = card.grab().save(path + QString("/challenge-kanji-%1.png").arg(i)) && ok;
    card.hide();
  }
  applyTheme(defaultTheme);
  milestones->setState(state);
  if (auto *search = milestones->findChild<QLineEdit *>("wordSearch")) {
    search->setText("か");
    qApp->processEvents();
    ok = dashboard.grab().save(path + "/word-search.png") && ok;
    search->setText("chu");
    qApp->processEvents();
    ok = dashboard.grab().save(path + "/word-search-romaji.png") && ok;
    search->setText("kanji");
    qApp->processEvents();
    ok = dashboard.grab().save(path + "/word-search-kanji.png") && ok;
    search->setText("vocab");
    qApp->processEvents();
    ok = dashboard.grab().save(path + "/word-search-vocab.png") && ok;
    search->setText("no matching word");
    qApp->processEvents();
    ok = dashboard.grab().save(path + "/word-search-empty.png") && ok;
    search->clear();
  }
  auto cleared = state;
  cleared.tiles[8].readingChallenge = false;
  recap.present({{ProgressNotice::ChallengeCleared, cleared.tiles[8].id,
                   cleared.tiles[8].characters, 0}}, cleared);
  qApp->processEvents();
  ok = recap.grab().save(path + "/challenge-cleared.png") && ok;
  recap.dismiss();
  milestones->setState({});
  qApp->processEvents();
  ok = dashboard.grab().save(path + "/empty.png") && ok;
  dashboard.hide();
  return ok;
}

void App::identifyDisplays() {
  showDisplayIdentifiers(theme->currentIndex());
}

bool App::saveAnkiPreview(const QString &path) {
  QDir().mkpath(path);
  const auto cards = anki.subjects(settings.value("anki/profile").toString(),
                                   settings.value("anki/decks").toStringList());
  if (cards.isEmpty())
    return false;
  card.setReducedMotion(true);
  recap.setReducedMotion(true);
  card.setAnkiAudio(false, 0);
  speech.configure(false);
  card.setSpeech(&speech, true, false, false);
  previewCard.setSpeech(&speech, true, false, false);
  demoMode = true; // No background sync; do not run the practice clock.
  showSettings();
  dashboard.findChild<QPushButton *>("studioSettings3")->click();
  qApp->processEvents();
  bool ok = dashboard.grab().save(path + "/anki-settings.png");
  Challenge c;
  c.subject = cards.first();
  c.mode = Mode::Recall;
  c.reading = true;
  card.present(c, 5);
  qApp->processEvents();
  ok = card.grab().save(path + "/anki-front.png") && ok;
  card.findChild<QPushButton *>("revealAnswer")->click();
  qApp->processEvents();
  ok = card.grab().save(path + "/anki-revealed.png") && ok;
  card.findChild<QPushButton *>("ankiShowDetails")->click();
  qApp->processEvents();
  ok = card.grab().save(path + "/anki-details.png") && ok;
  c.subject = cards[qMin(96, int(cards.size()) - 1)];
  c.mode = Mode::Typing;
  card.present(c, 5);
  qApp->processEvents();
  ok = card.grab().save(path + "/anki-typing.png") && ok;
  card.hide();
  dashboard.hide();
  return ok;
}
bool App::savePreview(const QString &path) {
  // CLI-only fixtures: main.cpp routes --preview into an isolated demo profile.
  // Do not finish cards, discover decks, sync, or write synthetic attempts.
  QDir().mkpath(path);
  QMap<QString, QVariant> savedSettings;
  for (const auto &key : settings.allKeys())
    savedSettings.insert(key, settings.value(key));
  const int savedTheme = theme->currentIndex();
  const int savedTab = tabs->currentIndex();
  dashboard.resize(1360, 820);
  const QSize savedSize = dashboard.size();
  const bool savedDemoMode = demoMode;
  const bool savedVisible = dashboard.isVisible();
  const bool savedHeartbeat = heartbeat.isActive();
  const bool savedAutoSync = autoSync.isActive();
  const QString savedSchedule = schedule->text();
  const QString savedConnection = connectionStatus->text();
  const auto savedConnectionState = connectionStatus->property("status");
  const QString savedTokenPlaceholder = tokenInput->placeholderText();
  auto *ankiStatus = ankiPanel->findChild<QLabel *>("ankiStatus");
  const QString savedAnkiStatus = ankiStatus->text();
  const auto savedAnkiState = ankiStatus->property("status");
  auto *modeProgress = modes->findChild<QLabel *>("modeProgress");
  const QString savedModeProgress = modeProgress->text();
  heartbeat.stop();
  autoSync.stop();
  demoMode = true;
  card.setReducedMotion(true);
  recap.setReducedMotion(true);
  previewCard.setReducedMotion(true);
  audio.configure(false, 0);
  speech.configure(false);
  card.setAnkiAudio(false, 0);
  previewCard.setAnkiAudio(false, 0);
  card.setSpeech(&speech, true, false, false);
  previewCard.setSpeech(&speech, true, false, false);
  refreshPool();
  dashboard.show();
  refreshStats();
  Challenge c;
  c.subject = pool.first();
  c.mode = Mode::Recall;
  c.reading = true;
  card.present(c, 5);
  qApp->processEvents();
  bool ok = dashboard.grab().save(path + "/dashboard.png") &&
            card.grab().save(path + "/card.png");
  auto capture = [&](QWidget &widget, const QString &name) {
    // Let deferred scroll-area geometry and backing-store updates settle.
    QEventLoop settle;
    QTimer::singleShot(40, &settle, &QEventLoop::quit);
    settle.exec();
    ok = widget.grab().save(path + "/" + name + ".png") && ok;
  };
  auto clickChoice = [&](const QString &answer) {
    for (auto *button : card.findChildren<QPushButton *>()) {
      if (button->property("answer").toString() == answer) {
        button->click();
        return;
      }
    }
    ok = false;
  };
  QPixmap sheet(themeCount * 400, 650);
  sheet.fill(QColor("#d5d8d3"));
  QPainter painter(&sheet);
  for (int i = 0; i < themeCount; ++i) {
    theme->setCurrentIndex(i);
    applyTheme(i);
    tabs->setCurrentIndex(0);
    qApp->processEvents();
    ok = dashboard.grab().save(path + QString("/theme-%1-home.png").arg(i)) &&
         ok;
    tabs->setCurrentWidget(insights);
    qApp->processEvents();
    ok = dashboard.grab().save(path +
                               QString("/theme-%1-insights.png").arg(i)) &&
         ok;
    c.mode = Mode::Recall;
    card.present(c, 5);
    qApp->processEvents();
    ok = card.grab().save(path + QString("/theme-%1-reveal.png").arg(i)) && ok;
    card.findChild<QPushButton *>("revealAnswer")->click();
    capture(card, QString("theme-%1-recall-revealed").arg(i));
    c.mode = Mode::Typing;
    c.subject.contextJa = "日本語を勉強しています。";
    c.subject.contextEn = "I’m studying Japanese.";
    card.present(c, 5);
    capture(card, QString("theme-%1-typing-unanswered").arg(i));
    card.findChild<QLineEdit *>("readingInput")->setText(c.subject.reading());
    card.findChild<QPushButton *>("checkReading")->click();
    capture(card, QString("theme-%1-correct").arg(i));
    painter.drawPixmap(i * 400 + 10, 10, card.grab());
    card.present(c, 5);
    card.findChild<QLineEdit *>("readingInput")->setText("にちほん");
    card.findChild<QPushButton *>("checkReading")->click();
    capture(card, QString("theme-%1-missed").arg(i));

    Challenge choice = c;
    choice.mode = Mode::Choice;
    choice.choices = {c.subject.reading(), "ほんじつ", "にちよう", "にっき"};
    card.present(choice, 5);
    capture(card, QString("theme-%1-choice-prompt").arg(i));
    clickChoice(choice.subject.reading());
    capture(card, QString("theme-%1-choice-correct").arg(i));
    card.present(choice, 5);
    clickChoice("にっき");
    capture(card, QString("theme-%1-choice-missed").arg(i));

    Challenge retry = c;
    retry.subject.kind = "kanji";
    retry.subject.characters = "日";
    retry.subject.readings = {"にち"};
    retry.subject.meanings = {"Sun", "Day"};
    retry.subject.readingKind = "onyomi";
    retry.subject.kanjiReadingTypes = {{"にち", "onyomi"}, {"ひ", "kunyomi"}};
    card.setReadingRetries(true);
    card.present(retry, 5);
    card.findChild<QLineEdit *>("readingInput")->setText("ひ");
    card.findChild<QPushButton *>("checkReading")->click();
    capture(card, QString("theme-%1-reading-retry").arg(i));

    Challenge local = c;
    local.mode = Mode::Recall;
    local.subject.id = -1;
    local.subject.source = "anki";
    local.subject.sourceId = "preview-synthetic-1";
    local.subject.deck = "Sample Japanese";
    local.subject.notes = "Sample vocabulary note · synthetic preview fixture";
    local.subject.sourceData = {
        {"pitch", QJsonObject{{"text", "にほん"},
                              {"high", QJsonArray{false, true, false}},
                              {"final_drop", false}}},
        {"frequency", "250"}};
    card.present(local, 5);
    capture(card, QString("theme-%1-anki-front").arg(i));
    card.findChild<QPushButton *>("revealAnswer")->click();
    capture(card, QString("theme-%1-anki-revealed").arg(i));
    card.findChild<QPushButton *>("ankiShowDetails")->click();
    capture(card, QString("theme-%1-anki-details").arg(i));
    card.hide();

    tabs->setCurrentWidget(history->parentWidget());
    capture(dashboard, QString("theme-%1-history").arg(i));
    tabs->setCurrentWidget(modes);
    modes->setPool({});
    modes->findChild<QPushButton *>("startSecondChances")->click();
    capture(dashboard, QString("theme-%1-focused-empty").arg(i));
    modes->setPool(pool);
    modeProgress->setText(savedModeProgress);
    tabs->setCurrentWidget(milestones);
    milestones->setState({});
    capture(dashboard, QString("theme-%1-word-tiles-empty").arg(i));
    milestones->setState(progression.state());

    tabs->setCurrentIndex(0);
    const QString overview = stats->toHtml();
    auto emptyOverview = recolor(store.statsHtml({}), i);
    emptyOverview.remove("<h1>Your practice</h1>");
    const int emptyTableEnd = emptyOverview.indexOf("</table>");
    if (emptyTableEnd >= 0)
      emptyOverview = emptyOverview.left(emptyTableEnd + 8);
    emptyOverview.replace("td{padding:14px}", "td{padding:4px}");
    emptyOverview.replace("cellspacing='8'", "cellspacing='0'");
    emptyOverview.replace("<font size='6'>", "<font size='4'>");
    stats->setHtml(emptyOverview);
    auto *sourceSummary = dashboard.findChild<QLabel *>("practiceSourceSummary");
    const QString summary = sourceSummary ? sourceSummary->text() : QString();
    const QString originalStatus = status->text();
    if (sourceSummary)
      sourceSummary->setText("Choose a practice source · simulated first-use preview");
    status->setText("Choose WaniKani or Anki in Settings to begin · simulated preview");
    schedule->setText("Connect your chosen source to begin · simulated first-use preview");
    const auto savedPool = pool;
    pool.clear();
    refreshHomeState();
    capture(dashboard, QString("theme-%1-first-use-home").arg(i));
    pool = savedPool;
    refreshHomeState();
    stats->setHtml(overview);
    if (sourceSummary)
      sourceSummary->setText(summary);
    status->setText(originalStatus);
    schedule->setText(savedSchedule);

    tabs->setCurrentWidget(settingsPage);
    auto *scroll =
        settingsPage->findChild<QScrollArea *>("studioSettingsScroll");
    auto *advanced =
        settingsPage->findChild<QCheckBox *>("advancedPracticeToggle");
    settingsPage->findChild<QPushButton *>("studioSettings0")->click();
    advanced->setChecked(false);
    scroll->verticalScrollBar()->setValue(0);
    qApp->processEvents();
    ok = dashboard.grab().save(path +
                               QString("/theme-%1-settings.png").arg(i)) && ok;
    scroll->ensureWidgetVisible(
        settingsPage->findChild<QWidget *>("placementSettings"), 0, 12);
    qApp->processEvents();
    ok = dashboard.grab().save(path +
                               QString("/theme-%1-placement.png").arg(i)) && ok;
    advanced->setChecked(true);
    qApp->processEvents();
    scroll->ensureWidgetVisible(recentPercent, 0, 80);
    qApp->processEvents();
    ok = dashboard.grab().save(path + QString("/theme-%1-mix.png").arg(i)) && ok;
    advanced->setChecked(false);
    settingsPage->findChild<QPushButton *>("studioSettings1")->click();
    scroll->verticalScrollBar()->setValue(0);
    qApp->processEvents();
    ok = dashboard.grab().save(path +
                               QString("/theme-%1-appearance.png").arg(i)) && ok;
    if (auto *voiceSection = settingsPage->findChild<QPushButton *>("studioSettings4"))
      voiceSection->click();
    scroll->ensureWidgetVisible(
        settingsPage->findChild<QSlider *>("soundVolume"), 0, 80);
    qApp->processEvents();
    ok = dashboard.grab().save(path + QString("/theme-%1-volume.png").arg(i)) && ok;
    scroll->ensureWidgetVisible(
        settingsPage->findChild<QWidget *>("speechSettings"), 0, 12);
    qApp->processEvents();
    ok = dashboard.grab().save(path + QString("/theme-%1-speech.png").arg(i)) && ok;
    settingsPage->findChild<QPushButton *>("studioSettings2")->click();
    scroll->verticalScrollBar()->setValue(0);
    setSourceStatus(connectionStatus, "offline", "Not connected · add your token to begin");
    tokenInput->setPlaceholderText("Paste your token");
    capture(dashboard, QString("theme-%1-wanikani-disconnected").arg(i));
    setSourceStatus(connectionStatus, "connecting", "Syncing learned items… · simulated preview");
    capture(dashboard, QString("theme-%1-wanikani-loading-simulated").arg(i));
    setSourceStatus(connectionStatus, "error", "Could not connect. Check your connection and try again. · simulated preview");
    capture(dashboard, QString("theme-%1-wanikani-error-simulated").arg(i));
    connectionStatus->setProperty("status", savedConnectionState);
    connectionStatus->setText(savedConnection);
    tokenInput->setPlaceholderText(savedTokenPlaceholder);
    settingsPage->findChild<QPushButton *>("studioSettings3")->click();
    scroll->verticalScrollBar()->setValue(0);
    setSourceStatus(ankiStatus, "offline", "Not connected · open Anki and find decks.");
    capture(dashboard, QString("theme-%1-anki-disconnected").arg(i));
    setSourceStatus(ankiStatus, "connecting", "Looking for Anki… · simulated preview");
    capture(dashboard, QString("theme-%1-anki-loading-simulated").arg(i));
    setSourceStatus(ankiStatus, "error", "Could not connect to Anki. Open Anki and try again. · simulated preview");
    capture(dashboard, QString("theme-%1-anki-error-simulated").arg(i));
    ankiStatus->setProperty("status", savedAnkiState);
    ankiStatus->setText(savedAnkiStatus);
    settingsPage->findChild<QPushButton *>("studioSettings0")->click();
    scroll->verticalScrollBar()->setValue(0);
  }
  painter.end();
  ok = sheet.save(path + "/themes.png") && ok;

  // Populated fixtures live only in a disposable store. The primary demo
  // database never receives their synthetic grades or mode-session events.
  {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
      ok = false;
    } else {
      Store syntheticStore(temporary.path());
      if (!syntheticStore.ready()) {
        ok = false;
      } else {
        const auto subjects = demoSubjects();
        const QDate anchor = QDate::currentDate();
        auto addAttempt = [&](int subjectIndex, bool reading, bool correct,
                              int daysAgo, Mode mode = Mode::Typing) {
          const auto &subject = subjects[subjectIndex];
          const QDate day = anchor.addDays(-daysAgo);
          const QString at = QDateTime(day, QTime(12, 0), QTimeZone("UTC"))
                                 .toString(Qt::ISODateWithMs);
          const QJsonObject event{
              {"schema_version", 2}, {"source", "wanikani"},
              {"subject_id", subject.id}, {"characters", subject.characters},
              {"accepted_readings", QJsonArray::fromStringList(subject.readings)},
              {"accepted_meanings", QJsonArray::fromStringList(subject.meanings)},
              {"submitted_answer", correct ? (reading ? subject.reading() : subject.meaning())
                                            : QString("incorrect")},
              {"graded_at", at}, {"graded_local_day", day.toString(Qt::ISODate)},
              {"timezone", "UTC"}, {"grading", mode == Mode::Recall ? "self-rated" : "checked"},
              {"outcome", correct ? "correct" : "incorrect"}};
          QSqlQuery query(syntheticStore.db);
          query.prepare("INSERT INTO attempts(subject_id,characters,mode,dimension,"
                        "correct,active_ms,created_at,local_day,event_json) "
                        "VALUES(?,?,?,?,?,?,?,?,?)");
          query.addBindValue(subject.id);
          query.addBindValue(subject.characters);
          query.addBindValue(modeName(mode));
          query.addBindValue(reading ? "Reading" : "Meaning");
          query.addBindValue(correct ? 1 : 0);
          query.addBindValue(2400);
          query.addBindValue(at);
          query.addBindValue(day.toString(Qt::ISODate));
          query.addBindValue(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
          ok = query.exec() && ok;
        };
        // Exactly one candidate keeps Second Chances' shuffled queue stable.
        for (int day : {3, 2, 1})
          addAttempt(0, true, false, day);
        for (int day : {21, 7, 1}) {
          addAttempt(1, true, true, day);
          addAttempt(1, false, true, day, Mode::Recall);
        }
        addAttempt(2, true, true, 3);
        addAttempt(2, true, true, 1);
        addAttempt(2, false, true, 1, Mode::Recall);
        addAttempt(3, false, false, 1, Mode::Choice);

        App populated(syntheticStore, temporary.path());
        populated.demoMode = true;
        populated.heartbeat.stop();
        populated.autoSync.stop();
        populated.audio.configure(false, 0);
        populated.speech.configure(false);
        populated.refreshPool();
        populated.dashboard.resize(savedSize);
        populated.dashboard.show();
        populated.status->setText("Sample data only · no real practice records");
        for (int i = 0; i < themeCount; ++i) {
          const QSignalBlocker blocker(populated.theme);
          populated.theme->setCurrentIndex(i);
          populated.applyTheme(i);
          populated.tabs->setCurrentIndex(0);
          capture(populated.dashboard, QString("theme-%1-home-populated").arg(i));
          populated.tabs->setCurrentWidget(populated.insights);
          capture(populated.dashboard, QString("theme-%1-insights-populated").arg(i));
          populated.tabs->setCurrentWidget(populated.history->parentWidget());
          capture(populated.dashboard, QString("theme-%1-history-populated").arg(i));
          populated.tabs->setCurrentWidget(populated.milestones);
          capture(populated.dashboard, QString("theme-%1-word-tiles-populated").arg(i));
          if (auto *canvas = populated.milestones->findChild<WordTileCanvas *>()) {
            canvas->focusTile(0);
            capture(populated.dashboard, QString("theme-%1-word-tile-inspected").arg(i));
          }
          populated.tabs->setCurrentWidget(populated.modes);
          auto *start = populated.modes->findChild<QPushButton *>("startSecondChances");
          auto *input = populated.modes->findChild<QLineEdit *>("modeReading");
          auto *check = populated.modes->findChild<QPushButton *>("modeCheck");
          auto *end = populated.modes->findChild<QPushButton *>("modeEnd");
          start->click();
          capture(populated.dashboard, QString("theme-%1-focused-unanswered").arg(i));
          input->setText(subjects.first().reading());
          check->click();
          capture(populated.dashboard, QString("theme-%1-focused-correct").arg(i));
          end->click(); // Only the disposable store receives this mode event.
          capture(populated.dashboard, QString("theme-%1-focused-completed").arg(i));
          start->click();
          input->setText("にちほん");
          check->click();
          capture(populated.dashboard, QString("theme-%1-focused-missed").arg(i));
          end->click();
        }
        populated.card.hide();
        populated.previewCard.hide();
        populated.dashboard.hide();
      }
    }
  }

  theme->setCurrentIndex(0);
  applyTheme(0);
  c.subject.characters = "貝";
  c.subject.meanings = {"Shell"};
  c.subject.readings = {"かい"};
  c.mode = Mode::Choice;
  c.reading = true;
  c.choices = {"かい", "がい", "はい", "ばい"};
  card.present(c, 5);
  qApp->processEvents();
  ok = card.grab().save(path + "/reading-options.png") && ok;
  tabs->setCurrentWidget(insights);
  qApp->processEvents();
  ok = dashboard.grab().save(path + "/insights.png") && ok;
  dashboard.resize(1600, 1000);
  qApp->processEvents();
  qApp->processEvents();
  ok = dashboard.grab().save(path + "/insights-wide.png") && ok;
  tabs->setCurrentWidget(settingsPage);
  qApp->processEvents();
  ok = dashboard.grab().save(path + "/settings-wide.png") && ok;
  tabs->setCurrentWidget(history->parentWidget());
  qApp->processEvents();
  ok = dashboard.grab().save(path + "/history.png") && ok;
  tabs->setCurrentWidget(milestones);
  qApp->processEvents();
  ok = dashboard.grab().save(path + "/milestones.png") && ok;

  // Restore the demo profile so repeating the same harness keeps its fixtures.
  card.hide();
  previewCard.hide();
  dashboard.hide();
  const QSignalBlocker themeBlocker(theme);
  theme->setCurrentIndex(savedTheme);
  applyTheme(savedTheme);
  dashboard.resize(savedSize);
  tabs->setCurrentIndex(savedTab);
  modes->setPool(pool);
  modeProgress->setText(savedModeProgress);
  milestones->setState(progression.state());
  connectionStatus->setText(savedConnection);
  tokenInput->setPlaceholderText(savedTokenPlaceholder);
  ankiStatus->setText(savedAnkiStatus);
  schedule->setText(savedSchedule);
  settings.clear();
  for (auto it = savedSettings.cbegin(); it != savedSettings.cend(); ++it)
    settings.setValue(it.key(), it.value());
  settings.sync();
  ok = settings.status() == QSettings::NoError && ok;
  applyFeedbackSettings();
  demoMode = savedDemoMode;
  if (savedVisible)
    dashboard.show();
  if (savedHeartbeat)
    heartbeat.start();
  if (savedAutoSync)
    autoSync.start();
  return ok;
}
