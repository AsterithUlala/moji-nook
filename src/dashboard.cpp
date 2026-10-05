#include "style.h"
#include "theme.h"
#include "ui.h"
#include "ui_helpers.h"
#include "print_ink.h"

class SettingsSections : public QWidget {
public:
  QGridLayout *grid = new QGridLayout(this);
  QList<QWidget *> panels;
  int selected = 0;
  void arrange() {
    if (panels.isEmpty())
      return;
    selected = qBound(0, selected, int(panels.size()) - 1);
    setProperty("columns", 1);
    for (int i = 0; i < panels.size(); ++i) {
      grid->removeWidget(panels[i]);
      panels[i]->setVisible(i == selected);
    }
    grid->addWidget(panels[selected], 0, 0, Qt::AlignTop);
    grid->setColumnStretch(0, 1);
  }
};
void App::buildDashboard() {
  auto sliderRow = [this](QVBoxLayout *layout, QSlider *slider,
                          const QString &title, const QString &suffix = "%",
                          double scale = 1.0) {
    auto *caption = label({}, "muted");
    caption->setBuddy(slider);
    slider->setAccessibleName(title);
    slider->setMinimumHeight(32);
    auto update = [caption, slider, title, suffix, scale] {
      caption->setText(title + " · " +
                       QString::number(slider->value() * scale, 'g', 3) +
                       suffix);
    };
    connect(slider, &QSlider::valueChanged, this, update);
    update();
    layout->addWidget(caption);
    layout->addWidget(slider);
  };
  auto *central = new PrintInkSurface(PrintInkSurface::Paper);
  central->setObjectName("dashboardPaper");
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(28, 24, 28, 16);
  layout->setSpacing(12);
  schedule = label({}, "muted");
  schedule->setObjectName("practiceSchedule");
  schedule->setText(QString("%1 cards per session · every %2 minutes")
                        .arg(clock.sessionSize)
                        .arg(clock.intervalMs / 60000));
  layout->addWidget(schedule);
  syncAge = label({}, "muted");
  syncAge->setObjectName("syncAge");
  layout->addWidget(syncAge);
  tabs = new QTabWidget;
  tabs->setObjectName("dashboardTabs");
  tabs->findChild<QStackedWidget *>()->setObjectName("dashboardPages");
  layout->addWidget(tabs, 1);
  stats = new QTextBrowser;
  stats->setOpenExternalLinks(false);
  tabs->addTab(stats, "Overview");
  auto *settingsBody = new SettingsSections;
  settingsPage = settingsBody;
  settingsPage->setObjectName("settingsBody");
  settingsPage->setMaximumWidth(1500);
  auto *sections = settingsBody->grid;
  sections->setSpacing(16);
  auto *appearanceBox = new QGroupBox("Appearance", settingsBody);
  auto *practiceBox = new QWidget(settingsBody);
  practiceBox->setObjectName("practiceSettingsPanel");
  auto *accountBox = new QWidget(settingsBody);
  accountBox->setObjectName("wanikaniSettingsPanel");
  ankiPanel = new AnkiPanel(anki, settings, settingsBody);
  auto *form = new QVBoxLayout(appearanceBox);
  auto *practiceForm = new QVBoxLayout(practiceBox);
  auto *accountForm = new QVBoxLayout(accountBox);
  sections->setAlignment(Qt::AlignTop);
  form->setSpacing(15);
  auto *themeLabel = label("Theme", "muted");
  form->addWidget(themeLabel);
  theme = new ScrollSafeComboBox;
  theme->setObjectName("themeSelector");
  themeLabel->setBuddy(theme);
  theme->setAccessibleName("Theme");
  const QStringList names = {"Ink · light", "Ink · dark",
                             "Paper · light", "Paper · dark"};
  for (int i = 0; i < names.size(); ++i) {
    auto c = colors(i);
    QPixmap swatch(48, 20);
    swatch.fill(Qt::transparent);
    QPainter p(&swatch);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(c.background));
    p.drawRoundedRect(0, 0, 48, 20, 5, 5);
    p.setBrush(QColor(c.accent));
    p.drawEllipse(6, 5, 10, 10);
    p.setBrush(QColor(c.warm));
    p.drawEllipse(20, 5, 10, 10);
    p.end();
    theme->addItem(QIcon(swatch), names[i]);
  }
  theme->setIconSize(QSize(48, 20));
  const int catalogVersion = settings.value("theme_catalog_version", 1).toInt();
  if (catalogVersion < 3) {
    int previous = settings.value("theme", defaultTheme).toInt();
    if (settings.contains("theme")) {
      if (catalogVersion < 2)
        previous = migratedTheme(previous);
      previous = simplifiedTheme(previous);
    }
    settings.setValue("theme", previous);
    settings.setValue("theme_catalog_version", 3);
    settings.sync();
  }
  theme->setCurrentIndex(
      qBound(0, settings.value("theme", defaultTheme).toInt(), themeCount - 1));
  form->addWidget(theme);
  connect(theme, &QComboBox::currentIndexChanged, this, &App::applyTheme);
  auto *appearanceForm = form;
  auto *voiceBox = new QWidget(settingsBody);
  form = new QVBoxLayout(voiceBox);
  form->setContentsMargins(18, 12, 18, 12);
  form->setSpacing(12);
  form->addWidget(label("Answer sounds", "controlHeading"));
  auto *soundEnabled = new SettingsCheckBox("Answer sounds");
  soundEnabled->setObjectName("soundEnabled");
  soundEnabled->setChecked(settings.value("sound_enabled", true).toBool());
  form->addWidget(soundEnabled);
  auto *volume = new ScrollSafeSlider;
  volume->setObjectName("soundVolume");
  volume->setRange(0, 100);
  volume->setAccessibleName("Answer sound level");
  volume->setValue(qBound(0, settings.value("sound_volume", 20).toInt(), 100));
  sliderRow(form, volume, "Answer volume");
  auto *soundSamples = new QHBoxLayout;
  auto *successSample = button("Try success sound", "control");
  auto *missSample = button("Try miss sound", "control");
  soundSamples->addWidget(successSample);
  soundSamples->addWidget(missSample);
  form->addLayout(soundSamples);
  connect(successSample, &QPushButton::clicked, this,
          [this] { audio.play(true); });
  connect(missSample, &QPushButton::clicked, this,
          [this] { audio.play(false); });
  auto updateSounds = [this, soundEnabled, volume, successSample, missSample] {
    settings.setValue("sound_enabled", soundEnabled->isChecked());
    settings.setValue("sound_volume", volume->value());
    volume->setEnabled(soundEnabled->isChecked());
    successSample->setEnabled(soundEnabled->isChecked() && volume->value() > 0);
    missSample->setEnabled(soundEnabled->isChecked() && volume->value() > 0);
    applyFeedbackSettings();
  };
  connect(soundEnabled, &QCheckBox::toggled, this, updateSounds);
  connect(volume, &QSlider::valueChanged, this, updateSounds);
  volume->setEnabled(soundEnabled->isChecked());
  successSample->setEnabled(soundEnabled->isChecked() && volume->value() > 0);
  missSample->setEnabled(soundEnabled->isChecked() && volume->value() > 0);
  auto *motion = new SettingsCheckBox("Reduce animations");
  motion->setObjectName("reducedMotion");
  motion->setChecked(settings.value("reduced_motion", false).toBool());
  appearanceForm->addWidget(motion);
  connect(motion, &QCheckBox::toggled, this, [this](bool reduced) {
    settings.setValue("reduced_motion", reduced);
    applyFeedbackSettings();
  });
  form->addWidget(label("New sessions stay silent.", "muted"));
  buildSpeechSettings(form);
  form = appearanceForm;
  auto *previewMode = new ScrollSafeComboBox;
  previewMode->setObjectName("previewChallengeMode");
  previewMode->addItems({"Recall", "Typed reading", "Four choices"});
  previewMode->setAccessibleName("Sample challenge type");
  auto *previewLabel = label("Sample challenge", "muted");
  previewLabel->setBuddy(previewMode);
  form->addWidget(previewLabel);
  form->addWidget(previewMode);
  auto *preview = button("Preview a sample card");
  preview->setObjectName("previewSampleCard");
  form->addWidget(preview);
  connect(preview, &QPushButton::clicked, this, [this, previewMode] {
    showCardPreview(previewMode->currentIndex());
  });
  connect(previewMode, &QComboBox::currentIndexChanged, this, [this](int mode) {
    if (dashboard.isVisible() && tabs->currentWidget() == settingsPage)
      showCardPreview(mode);
  });
  form->addWidget(label("Sample only · no stats recorded.", "muted"));
  form = practiceForm;
  form->setSpacing(12);
  auto *sourceLabel = label("Source", "controlHeading");
  form->addWidget(sourceLabel);
  practiceSource = new ScrollSafeComboBox;
  practiceSource->setObjectName("practiceSource");
  practiceSource->setAccessibleName("Practice source");
  sourceLabel->setBuddy(practiceSource);
  practiceSource->addItem("WaniKani", "wanikani");
  practiceSource->addItem("Anki", "anki");
  practiceSource->addItem("Both sources", "both");
  practiceSource->setCurrentIndex(qMax(0, practiceSource->findData(
      settings.value("practice/source", "wanikani").toString())));
  form->addWidget(practiceSource);
  auto *sourceHint = label({}, "muted");
  sourceHint->setObjectName("practiceSourceHint");
  form->addWidget(sourceHint);
  auto *sourceWarning = label(
      "Two sources can repeat the same words and divide your attention. "
      "Choose one for simpler practice. Anki and WaniKani schedules stay untouched.", "practiceSourceWarning");
  sourceWarning->setAccessibleName("Warning about using both sources");
  form->addWidget(sourceWarning);
  auto *sourceMixPanel = new QWidget;
  auto *sourceMixLayout = new QVBoxLayout(sourceMixPanel);
  sourceMixLayout->setContentsMargins(0, 0, 0, 0);
  auto *sourceMix = new ScrollSafeSlider;
  sourceMix->setObjectName("ankiShare");
  sourceMix->setRange(0, 100);
  sourceMix->setValue(qBound(0, settings.value("anki/share", 30).toInt(), 100));
  sliderRow(sourceMixLayout, sourceMix, "Anki share");
  form->addWidget(sourceMixPanel);
  connect(sourceMix, &QSlider::valueChanged, this,
          [this](int value) { settings.setValue("anki/share", value); });
  auto updateSourceDescription = [this, sourceHint, sourceWarning, sourceMixPanel] {
    const auto selected = practiceSource->currentData().toString();
    sourceWarning->setVisible(selected == "both");
    sourceMixPanel->setVisible(selected == "both");
    sourceHint->setText(selected == "anki"
        ? "Sync decks in the Anki section. Cached cards work with Anki closed."
        : selected == "both"
            ? "Using both is optional. If one is empty, practice uses the other."
            : "Learned items from WaniKani.");
  };
  connect(practiceSource, &QComboBox::currentIndexChanged, this,
          [this, updateSourceDescription] {
            setPracticeSource(practiceSource->currentData().toString());
            updateSourceDescription();
          });
  updateSourceDescription();
  auto *scheduleDivider = new QFrame;
  scheduleDivider->setObjectName("practiceSectionDivider");
  scheduleDivider->setFrameShape(QFrame::HLine);
  scheduleDivider->setFrameShadow(QFrame::Plain);
  form->addWidget(scheduleDivider);
  auto *scheduleHeading = label("Schedule", "controlHeading");
  form->addWidget(scheduleHeading);
  auto *intervalLabel = label("Minutes between sessions", "muted");
  form->addWidget(intervalLabel);
  interval = new ScrollSafeSpinBox;
  intervalLabel->setBuddy(interval);
  interval->setAccessibleName("Minutes between sessions");
  interval->setRange(1, 120);
  interval->setValue(clock.intervalMs / 60000);
  form->addWidget(interval);
  connect(interval, &QSpinBox::valueChanged, this, [this](int n) {
    clock.intervalMs = n * 60000;
    settings.setValue("interval_minutes", n);
    if (clock.timer.isActive())
      clock.timer.start(clock.intervalMs);
  });
  sessionCount = new ScrollSafeSpinBox;
  sessionCount->setObjectName("sessionCount");
  sessionCount->setRange(1, 5);
  sessionCount->setValue(clock.sessionSize);
  sessionCount->setSuffix(" cards per session");
  sessionCount->setAccessibleName("Cards per session");
  auto *countLabel = label("Cards per session", "muted");
  countLabel->setBuddy(sessionCount);
  form->addWidget(countLabel);
  form->addWidget(sessionCount);
  connect(sessionCount, &QSpinBox::valueChanged, this, [this](int n) {
    clock.sessionSize = n;
    settings.setValue("session_count", n);
  });
  auto *placement = new QFrame;
  placement->setObjectName("placementSettings");
  auto *placementLayout = new QVBoxLayout(placement);
  placementLayout->setContentsMargins(0, 0, 0, 0);
  placementLayout->setSpacing(12);
  placementLayout->addWidget(label("Card placement", "controlHeading"));
  auto *placementGrid = new QGridLayout;
  placementGrid->setHorizontalSpacing(16);
  placementGrid->setVerticalSpacing(8);
  placementGrid->setColumnStretch(0, 1);
  placementGrid->setColumnStretch(1, 2);
  placementLayout->addLayout(placementGrid);
  auto *cornerLabel = label("Corner", "muted");
  placementGrid->addWidget(cornerLabel, 0, 0);
  corner = new ScrollSafeComboBox;
  corner->setObjectName("cardCorner");
  cornerLabel->setBuddy(corner);
  corner->setAccessibleName("Card corner");
  corner->addItems({"Bottom right", "Bottom left", "Top right", "Top left"});
  corner->setCurrentIndex(qBound(0, settings.value("corner", 0).toInt(), 3));
  placementGrid->addWidget(corner, 1, 0);
  connect(corner, &QComboBox::currentIndexChanged, this, [this](int index) {
    settings.setValue("corner", index);
    card.setCorner(index);
    previewCard.setCorner(index);
    if (dashboard.isVisible() && tabs->currentWidget() == settingsPage)
      showCardPreview();
  });
  auto *monitorLabel = label("Display", "muted");
  placementGrid->addWidget(monitorLabel, 0, 1);
  monitor = new ScrollSafeComboBox;
  monitor->setObjectName("cardMonitor");
  monitor->setAccessibleName("Practice card display");
  monitorLabel->setBuddy(monitor);
  populateMonitors();
  placementGrid->addWidget(monitor, 1, 1);
  connect(qApp, &QGuiApplication::screenAdded, this, &App::populateMonitors);
  connect(qApp, &QGuiApplication::screenRemoved, this, &App::populateMonitors);
  connect(qApp, &QGuiApplication::primaryScreenChanged, this, &App::populateMonitors);
  connect(monitor, &QComboBox::currentIndexChanged, this, [this] {
    const auto name = monitor->currentData().toString();
    settings.setValue("monitor", name);
    card.setMonitor(name);
    previewCard.setMonitor(name);
    if (dashboard.isVisible() && tabs->currentWidget() == settingsPage)
      showCardPreview();
  });
  card.setMonitor(settings.value("monitor").toString());
  previewCard.setMonitor(settings.value("monitor").toString());
  auto *placementActions = new QHBoxLayout;
  auto *identify = button("Identify displays", "quiet");
  identify->setObjectName("identifyDisplays");
  auto *placementPreview = button("Preview card", "quiet");
  placementPreview->setObjectName("previewPlacement");
  placementActions->addWidget(identify);
  placementActions->addWidget(placementPreview);
  placementActions->addStretch();
  placementLayout->addLayout(placementActions);
  connect(identify, &QPushButton::clicked, this, &App::identifyDisplays);
  connect(placementPreview, &QPushButton::clicked, this,
          [this] { showCardPreview(); });
  placementLayout->addWidget(label("A sample appears as you change the display or corner. "
                        "Your practice session and history stay untouched.", "muted"));
  auto *placementDivider = new QFrame;
  placementDivider->setObjectName("practiceSectionDivider");
  placementDivider->setFrameShape(QFrame::HLine);
  placementDivider->setFrameShadow(QFrame::Plain);
  practiceForm->addWidget(placementDivider);
  practiceForm->addWidget(placement);
  form = accountForm;
  form->setSpacing(12);
  connectionStatus = label({}, "muted");
  connectionStatus->setObjectName("connectionStatus");
  connectionStatus->setAccessibleName("WaniKani connection status");
  if (readToken().isEmpty())
    setSourceStatus(connectionStatus, "offline",
                                    "Not connected · add your token to begin");
  else if (store.cache().isEmpty())
    setSourceStatus(connectionStatus, "offline",
                                    "Token saved · sync to load learned items");
  else
    setSourceStatus(connectionStatus, "connected",
                                    "Learned items cached · practice stays local");
  form->addWidget(connectionStatus);
  auto *getToken = button("Open WaniKani token settings", "quiet");
  form->addWidget(getToken);
  connect(getToken, &QPushButton::clicked, this, [] {
    QDesktopServices::openUrl(
        QUrl("https://www.wanikani.com/settings/personal_access_tokens"));
  });
  auto *tokenLabel = label("WaniKani API token", "muted");
  form->addWidget(tokenLabel);
  tokenInput = new QLineEdit;
  tokenInput->setObjectName("apiTokenInput");
  tokenLabel->setBuddy(tokenInput);
  tokenInput->setAccessibleName("WaniKani API token");
  tokenInput->setEchoMode(QLineEdit::Password);
  tokenInput->setPlaceholderText(
      readToken().isEmpty() ? "Paste your token"
                            : "Token saved · paste here only to replace it");
  form->addWidget(tokenInput);
  auto *save = button("Save token and sync", "primary");
  save->setObjectName("saveToken");
  form->addWidget(save);
  connect(save, &QPushButton::clicked, this, [this] {
    if (sync.busy()) {
      status->setText(
          "Wait for the current sync to finish before replacing the token.");
      return;
    }
    QString key = tokenInput->text().trimmed();
    if (key.isEmpty() && readToken().isEmpty()) {
      setSourceStatus(connectionStatus, "offline",
                                      "Paste your WaniKani token before saving.");
      tokenInput->setFocus();
      return;
    }
    if (!key.isEmpty() && !saveToken(key)) {
      status->setText("Could not save your token.");
      setSourceStatus(connectionStatus, "error",
                                      "Could not save your token. Please try again.");
      return;
    }
    tokenInput->clear();
    tokenInput->setPlaceholderText(
        "Token saved · paste here only to replace it");
    syncNow();
  });
  form->addWidget(label(
      "Read-only access · token and practice history stay local.", "muted"));
  form = practiceForm;
  auto *mixSummary = label({}, "muted");
  auto *advancedToggle =
      new SettingsCheckBox("Fine-tune practice (optional)");
  advancedToggle->setObjectName("advancedPracticeToggle");
  practiceForm->addWidget(advancedToggle);
  auto *advancedPanel = new QWidget;
  advancedPanel->setObjectName("advancedPractice");
  form = new QVBoxLayout(advancedPanel);
  form->setContentsMargins(0, 8, 0, 0);
  form->setSpacing(14);
  form->addWidget(mixSummary);
  practiceForm->addWidget(advancedPanel);
  advancedPanel->hide();
  connect(advancedToggle, &QCheckBox::toggled, advancedPanel,
          &QWidget::setVisible);
  // Adopt the requested four-way mix once; later user adjustments persist.
  if (!settings.contains("newest_share")) {
    settings.setValue("previous_recent_share",
                      settings.value("recent_share", 40));
    settings.setValue("previous_persistent_share",
                      settings.value("persistent_share", 20));
    settings.setValue("newest_share", 30);
    settings.setValue("recent_share", 25);
    settings.setValue("persistent_share", 20);
  }
  newestPercent = new ScrollSafeSlider;
  newestPercent->setObjectName("newestShare");
  newestPercent->setRange(0, 100);
  newestPercent->setValue(settings.value("newest_share", 30).toInt());
  recentPercent = new ScrollSafeSlider;
  persistentPercent = new ScrollSafeSlider;
  recentPercent->setObjectName("recentShare");
  persistentPercent->setObjectName("persistentShare");
  recentPercent->setRange(0, 100 - newestPercent->value());
  recentPercent->setValue(qBound(0, settings.value("recent_share", 25).toInt(),
                                 recentPercent->maximum()));
  persistentPercent->setRange(0, 100 - newestPercent->value() -
                                     recentPercent->value());
  persistentPercent->setValue(
      qBound(0, settings.value("persistent_share", 20).toInt(),
             persistentPercent->maximum()));
  recentPercent->setAccessibleName("Recent struggles percentage");
  persistentPercent->setAccessibleName("Persistent trouble percentage");
  form->addWidget(label("Which items?", "muted"));
  sliderRow(form, newestPercent, "Newest 30 learned items");
  form->addWidget(label("Latest 30 learned items · equal odds.", "muted"));
  sliderRow(form, recentPercent, "Recent troubles");
  sliderRow(form, persistentPercent, "Persistent trouble");
  auto *broadShare = label({}, "muted");
  form->addWidget(broadShare);
  auto updateMix = [this, broadShare, mixSummary] {
    recentPercent->setMaximum(100 - newestPercent->value());
    persistentPercent->setMaximum(100 - newestPercent->value() -
                                  recentPercent->value());
    settings.setValue("newest_share", newestPercent->value());
    settings.setValue("recent_share", recentPercent->value());
    settings.setValue("persistent_share", persistentPercent->value());
    broadShare->setText(QString("Broad recall · %1%")
                            .arg(100 - newestPercent->value() -
                                 recentPercent->value() -
                                 persistentPercent->value()));
    mixSummary->setText(
        QString("Mix: %1% newest · %2% recent · %3% persistent · %4% broad")
            .arg(newestPercent->value())
            .arg(recentPercent->value())
            .arg(persistentPercent->value())
            .arg(100 - newestPercent->value() - recentPercent->value() -
                 persistentPercent->value()));
  };
  connect(newestPercent, &QSlider::valueChanged, this, updateMix);
  connect(recentPercent, &QSlider::valueChanged, this, updateMix);
  connect(persistentPercent, &QSlider::valueChanged, this, updateMix);
  updateMix();
  form->addWidget(label("Challenge formats", "muted"));
  auto *readingRetries = new SettingsCheckBox("Retry alternate kanji readings");
  readingRetries->setObjectName("readingRetriesEnabled");
  readingRetries->setChecked(
      settings.value("reading_retry_enabled", true).toBool());
  form->addWidget(readingRetries);
  form->addWidget(
      label("Known alternate reading → retry, not a miss.", "muted"));
  connect(readingRetries, &QCheckBox::toggled, this, [this](bool enabled) {
    settings.setValue("reading_retry_enabled", enabled);
    applyFeedbackSettings();
  });
  recallPercent = new ScrollSafeSlider;
  typedPercent = new ScrollSafeSlider;
  recallPercent->setObjectName("recallShare");
  typedPercent->setObjectName("typedShare");
  recallPercent->setRange(0, 100);
  recallPercent->setValue(settings.value("recall_share", 50).toInt());
  typedPercent->setRange(0, 100 - recallPercent->value());
  typedPercent->setValue(settings.value("typed_share", 25).toInt());
  sliderRow(form, recallPercent, "Self-rated recall");
  sliderRow(form, typedPercent, "Typed reading");
  auto *choiceShare = label({}, "muted");
  form->addWidget(choiceShare);
  auto updateFormats = [this, choiceShare] {
    typedPercent->setMaximum(100 - recallPercent->value());
    settings.setValue("recall_share", recallPercent->value());
    settings.setValue("typed_share", typedPercent->value());
    choiceShare->setText(
        QString("Four choices · %1%")
            .arg(100 - recallPercent->value() - typedPercent->value()));
  };
  connect(recallPercent, &QSlider::valueChanged, this, updateFormats);
  connect(typedPercent, &QSlider::valueChanged, this, updateFormats);
  updateFormats();
  auto *weightsToggle =
      new SettingsCheckBox("Adjust broad-recall mastery weights");
  form->addWidget(weightsToggle);
  auto *weightsPanel = new QWidget;
  auto *weightsLayout = new QVBoxLayout(weightsPanel);
  weightsLayout->setContentsMargins(0, 0, 0, 0);
  weightsLayout->addWidget(
      label("Relative odds per item · broad recall only.", "muted"));
  const QStringList stages = {"Apprentice", "Guru", "Master", "Enlightened",
                              "Burned"};
  const QList<int> defaults = {40, 30, 20, 10, 5};
  auto *resetWeights = button("Restore default weights", "quiet");
  for (int i = 0; i < stages.size(); ++i) {
    auto *weight = new ScrollSafeSlider;
    const QString key = QString("mastery_weight_%1").arg(i);
    weight->setObjectName(key);
    weight->setRange(1, 100);
    weight->setValue(settings.value(key, defaults[i]).toInt());
    sliderRow(weightsLayout, weight, stages[i], "×", .1);
    connect(weight, &QSlider::valueChanged, this,
            [this, key](int n) { settings.setValue(key, n); });
    connect(resetWeights, &QPushButton::clicked, weight,
            [weight, n = defaults[i]] { weight->setValue(n); });
  }
  weightsLayout->addWidget(resetWeights);
  form->addWidget(weightsPanel);
  weightsPanel->hide();
  connect(weightsToggle, &QCheckBox::toggled, weightsPanel,
          &QWidget::setVisible);
  form->addWidget(label(
      "Keyboard: Tab to move · Enter to submit · Esc to dismiss.", "muted"));
  form = accountForm;
  auto *exportButton = button("Export practice history as CSV");
  connect(exportButton, &QPushButton::clicked, this, [this] {
    QString path = QFileDialog::getSaveFileName(
        &dashboard, "Export practice history",
        QDir::homePath() + "/moji-nook-practice.csv", "CSV files (*.csv)");
    if (!path.isEmpty())
      status->setText(store.exportCsv(path)
                          ? "Practice history exported."
                          : "Could not export practice history.");
  });
  form->addStretch();
  auto *settingsScroll = new QScrollArea;
  settingsScroll->viewport()->setObjectName("settingsViewport");
  settingsScroll->setWidgetResizable(true);
  settingsScroll->setFrameShape(QFrame::NoFrame);
  settingsScroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  settingsScroll->setWidget(settingsPage);
  settingsPage = settingsScroll;
  insights = new InsightsBrowser;
  insights->setOpenExternalLinks(false);
  insights->setAccessibleName("Practice insights");
  tabs->addTab(insights, "Insights");
  auto *historyPanel = new QWidget;
  historyPanel->setObjectName("historyPanel");
  auto *historyLayout = new QVBoxLayout(historyPanel);
  history = new QTextBrowser;
  history->setAccessibleName("Practice event history");
  historyLayout->addWidget(history);
  auto *paging = new QHBoxLayout;
  historyPrevious = button("Newer", "quiet");
  historyNext = button("Older", "quiet");
  historyPageLabel = label({});
  paging->addWidget(historyPrevious);
  paging->addWidget(historyPageLabel, 1);
  paging->addWidget(historyNext);
  paging->setAlignment(historyNext, Qt::AlignRight);
  historyLayout->addLayout(paging);
  exportButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  historyLayout->addWidget(exportButton, 0, Qt::AlignLeft);
  connect(historyPrevious, &QPushButton::clicked, this, [this] {
    --historyPage;
    refreshStats();
  });
  connect(historyNext, &QPushButton::clicked, this, [this] {
    ++historyPage;
    refreshStats();
  });
  tabs->addTab(historyPanel, "History");
  milestones = new ProgressionPage;
  tabs->addTab(milestones, "Word tiles");
  modes = new PracticeModes(store);
  tabs->addTab(modes, "Modes");
  auto *settingsHub = new QWidget;
  settingsHub->setObjectName("settingsHub");
  auto *hubLayout = new QVBoxLayout(settingsHub);
  auto *sectionButtons = new QHBoxLayout;
  sectionButtons->setSpacing(0);
  auto *sectionGroup = new QButtonGroup(settingsHub);
  sectionGroup->setExclusive(true);
  settingsBody->panels = {practiceBox, appearanceBox, accountBox, ankiPanel, voiceBox};
  settingsBody->setMaximumWidth(980);
  const QStringList sectionsNames = {"Practice", "Appearance",
                                     "WaniKani", "Anki", "Sound && voice"};
  for (int i = 0; i < sectionsNames.size(); ++i) {
    auto *section = new QPushButton(sectionsNames[i]);
    section->setObjectName(QString("studioSettings%1").arg(i));
    section->setCheckable(true);
    section->setProperty("role", "section");
    section->setProperty("lastSection", i == sectionsNames.size() - 1);
    sectionGroup->addButton(section);
    sectionButtons->addWidget(section);
    connect(section, &QPushButton::clicked, this, [settingsBody, settingsScroll, i] {
      settingsBody->selected = i;
      settingsBody->arrange();
      settingsScroll->verticalScrollBar()->setValue(0);
    });
  }
  sectionGroup->buttons().first()->setChecked(true);
  settingsBody->selected = 0;
  settingsBody->arrange();
  settingsPage->setObjectName("studioSettingsScroll");
  hubLayout->addLayout(sectionButtons);
  hubLayout->addWidget(settingsPage);
  settingsPage = settingsHub;
  tabs->addTab(settingsPage, "Settings");
  auto *bottom = new QHBoxLayout;
  status = label({}, "muted");
  bottom->addWidget(status, 1);
  syncButton = button(practiceSource->currentData().toString() == "anki"
                          ? "Anki sync settings" : "Sync WaniKani", "control");
  bottom->addWidget(syncButton);
  layout->addLayout(bottom);
  connect(syncButton, &QPushButton::clicked, this, [this] {
    if (practiceSource->currentData().toString() == "anki") {
      showSettings();
      dashboard.findChild<QPushButton *>("studioSettings3")->click();
    } else
      syncNow();
  });
  // Keep logical page indexes stable; navigation expresses the learner's tasks.
  auto *shell = new QWidget;
  shell->setObjectName("studioShell");
  auto *shellLayout = new QVBoxLayout(shell);
  shellLayout->setContentsMargins(0, 0, 0, 0);
  shellLayout->setSpacing(0);
  auto *header = new QWidget;
  header->setObjectName("studioHeader");
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(24, 14, 24, 14);
  headerLayout->setSpacing(8);
  headerMark = new BrandMarkLabel;
  headerMark->setObjectName("studioBrandMark");
  headerMark->setFixedSize(24, 24);
  headerMark->setAccessibleName("Moji Nook mark");
  headerLayout->addWidget(headerMark);
  auto *brand = label("Moji Nook", "studioBrand");
  brand->setWordWrap(false);
  headerLayout->addWidget(brand);
  headerLayout->addSpacing(20);
  tabs->tabBar()->hide();
  auto *navigation = new QButtonGroup(shell);
  navigation->setExclusive(false);
  const QStringList destinations = {"Practice", "Progress", "History",
                                    "Words", "Focus", "Settings"};
  auto addNav = [&](int i) {
    auto *nav = new QPushButton(destinations[i]);
    nav->setCheckable(true);
    nav->setObjectName(QString("studioNav%1").arg(i));
    navigation->addButton(nav, i);
    headerLayout->addWidget(nav);
    connect(nav, &QPushButton::clicked, this, [this, nav, i] {
      tabs->setCurrentIndex(i);
      nav->setChecked(true);
    });
  };
  for (int i : {0, 3, 1, 4}) addNav(i);
  headerLayout->addStretch();
  addNav(5);
  auto *more = new QPushButton("More");
  more->setObjectName("studioMore");
  more->setCheckable(true);
  more->setAccessibleName("History and app controls");
  auto *moreMenu = new QMenu(more);
  auto *historyAction = moreMenu->addAction("Practice history");
  historyAction->setObjectName("studioNav2");
  connect(historyAction, &QAction::triggered, this, [this] { tabs->setCurrentIndex(2); });
  moreMenu->addSeparator();
  moreMenu->addAction("Quit Moji Nook", qApp, &QApplication::quit);
  more->setMenu(moreMenu);
  connect(moreMenu, &QMenu::aboutToHide, this, [this, more] {
    more->setChecked(tabs->currentIndex() == 2);
  });
  headerLayout->addWidget(more);
  connect(tabs, &QTabWidget::currentChanged, this, [navigation, more](int i) {
    for (auto *nav : navigation->buttons())
      nav->setChecked(navigation->id(nav) == i);
    more->setChecked(i == 2);
  });
  auto *nightToggle = new SettingsCheckBox("Dark");
  nightToggle->setObjectName("nightMode");
  nightToggle->setAccessibleName("Dark mode");
  nightToggle->setToolTip("Switch between light and dark appearance");
  headerLayout->addWidget(nightToggle);
  connect(nightToggle, &QCheckBox::toggled, this, [this](bool night) {
    theme->setCurrentIndex(pairedTheme(theme->currentIndex(), night));
  });
  shellLayout->addWidget(header);
  shellLayout->addWidget(central, 1);

  auto *home = new QWidget;
  home->setObjectName("studioHome");
  auto *homeLayout = new QVBoxLayout(home);
  homeLayout->setContentsMargins(0, 8, 0, 12);
  homeLayout->setSpacing(20);
  homeLayout->addWidget(label("Practice", "homeHeading"));
  auto *workspace = new QHBoxLayout;
  workspace->setSpacing(28);
  auto *session = new PrintInkSurface(PrintInkSurface::Session);
  session->setObjectName("practiceSession");
  session->setMinimumWidth(280);
  session->setMaximumWidth(340);
  auto *sessionLayout = new QVBoxLayout(session);
  sessionLayout->setContentsMargins(24, 28, 24, 24);
  sessionLayout->setSpacing(10);
  homeState = label("Ready", "studioHero");
  sessionLayout->addWidget(homeState);
  layout->removeWidget(schedule);
  sessionLayout->addWidget(schedule);
  homeCadence = nullptr;
  sessionLayout->addStretch();
  homeBegin = button("Practice now", "primary");
  homeBegin->setObjectName("studioBegin");
  homeBegin->setMinimumHeight(48);
  connect(homeBegin, &QPushButton::clicked, this, [this] {
    if (!pool.isEmpty()) { requestPractice(); return; }
    showSettings();
    const int sourcePage = practiceSource->currentData().toString() == "anki" ? 3 : 2;
    dashboard.findChild<QPushButton *>(QString("studioSettings%1").arg(sourcePage))->click();
  });
  sessionLayout->addWidget(homeBegin);
  auto *pause = button("Pause reminders", "control");
  pause->setObjectName("pauseReminders");
  connect(pause, &QPushButton::clicked, this, [this] {
    pauseAction->setChecked(!pauseAction->isChecked());
    refreshHomeState();
  });
  sessionLayout->addWidget(pause);
  auto *snooze = button("Snooze 30 minutes", "control");
  snooze->setObjectName("homeSnooze");
  connect(snooze, &QPushButton::clicked, this, &App::snoozePractice);
  sessionLayout->addWidget(snooze);
  auto *sourceSummary = label("Choose a source in Settings", "practiceSourceSummary");
  sessionLayout->addWidget(sourceSummary);
  auto *manage = button("Manage sources", "control");
  manage->setObjectName("manageSources");
  connect(manage, &QPushButton::clicked, this, [this] {
    showSettings();
    dashboard.findChild<QPushButton *>(practiceSource->currentData().toString() == "anki"
        ? "studioSettings3" : "studioSettings2")->click();
  });
  sessionLayout->addWidget(manage);
  workspace->addWidget(session, 2);

  auto *words = new QWidget;
  words->setObjectName("homeWordCollection");
  auto *wordsLayout = new QVBoxLayout(words);
  wordsLayout->setContentsMargins(0, 0, 0, 0);
  wordsLayout->setSpacing(10);
  auto *wordHeading = new QHBoxLayout;
  wordHeading->addWidget(label("Learned words", "collectionHeading"), 1);
  auto *openWords = button("All words", "quiet");
  openWords->setObjectName("homeWords");
  connect(openWords, &QPushButton::clicked, this, [this] { tabs->setCurrentIndex(3); });
  wordHeading->addWidget(openWords);
  wordsLayout->addLayout(wordHeading);
  homeWordCount = label({}, "homeWordCount");
  wordsLayout->addWidget(homeWordCount);
  homeTiles = new WordTileCanvas;
  homeTiles->setObjectName("homeWordTiles");
  wordsLayout->addWidget(homeTiles);
  homeEmpty = label("言葉\n\nNo practiced words yet.", "homeWordsEmpty");
  homeEmpty->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  homeEmpty->setMinimumHeight(190);
  homeEmpty->setContentsMargins(20, 18, 20, 18);
  wordsLayout->addWidget(homeEmpty);
  homeInspection = label("Select a word to inspect it.", "homeInspection");
  connect(homeTiles, &WordTileCanvas::tileFocused, homeInspection, &QLabel::setText);
  wordsLayout->addWidget(homeInspection);
  wordsLayout->addStretch();
  homeFocus = button("Review difficult readings", "quiet");
  homeFocus->setObjectName("homeFocus");
  homeFocus->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  connect(homeFocus, &QPushButton::clicked, this, [this] { tabs->setCurrentIndex(4); });
  wordsLayout->addWidget(homeFocus, 0, Qt::AlignLeft);
  workspace->addWidget(words, 3);
  homeLayout->addLayout(workspace, 1);

  auto *recent = new QFrame;
  recent->setObjectName("recentPractice");
  auto *recentLayout = new QHBoxLayout(recent);
  recentLayout->setContentsMargins(0, 12, 0, 0);
  auto *recentHeading = new QVBoxLayout;
  recentHeading->addWidget(label("Recent practice", "controlHeading"));
  auto *progressLink = button("Explore your progress", "quiet");
  connect(progressLink, &QPushButton::clicked, this, [this] { tabs->setCurrentIndex(1); });
  recentHeading->addWidget(progressLink);
  recentHeading->addStretch();
  recentLayout->addLayout(recentHeading, 1);
  stats->setObjectName("homeStats");
  stats->setFixedHeight(96);
  stats->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recentLayout->addWidget(stats, 3);
  stats->show();
  homeLayout->addWidget(recent);
  auto *tools = new QHBoxLayout;
  auto *adjust = button("Reminder and placement settings", "quiet");
  adjust->setObjectName("adjustPractice");
  connect(adjust, &QPushButton::clicked, this, [this] {
    showSettings();
    dashboard.findChild<QPushButton *>("studioSettings0")->click();
  });
  tools->addWidget(adjust);
  auto *homePreview = button("Preview a card", "quiet");
  homePreview->setObjectName("homeCardPreview");
  connect(homePreview, &QPushButton::clicked, this, [this] { showCardPreview(); });
  tools->addWidget(homePreview);
  tools->addStretch();
  homeLayout->addLayout(tools);
  layout->removeWidget(syncAge);
  homeLayout->addWidget(syncAge);
  auto *homeScroll = new QScrollArea;
  homeScroll->setObjectName("homeScroll");
  homeScroll->viewport()->setObjectName("homeViewport");
  homeScroll->setWidgetResizable(true);
  homeScroll->setFrameShape(QFrame::NoFrame);
  homeScroll->setWidget(home);
  tabs->insertTab(0, homeScroll, "Practice");
  tabs->setCurrentIndex(0);
  navigation->button(0)->setChecked(true);
  dashboard.setCentralWidget(shell);
}
