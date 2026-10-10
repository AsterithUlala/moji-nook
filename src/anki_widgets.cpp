#include "anki_widgets.h"
#include "performance.h"
#include "ui.h"
#include "ui_helpers.h"
#include <QImageReader>


class PitchDiagram : public QWidget {
public:
  QJsonObject data;
  using QWidget::QWidget;
  QSize sizeHint() const override { return {280, 65}; }

protected:
  void paintEvent(QPaintEvent *) override {
    const auto text = data["text"].toString();
    const auto high = data["high"].toArray();
    if (text.isEmpty() || high.size() != text.size())
      return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(palette().color(QPalette::WindowText), 1.8));
    QFont font("Noto Sans CJK JP");
    font.setPixelSize(22);
    p.setFont(font);
    const double step = qMin(30.0, (width() - 16.0) / text.size());
    const double start = (width() - step * text.size()) / 2;
    QPainterPath path;
    for (int i = 0; i < text.size(); ++i) {
      const double x = start + i * step, y = high[i].toBool() ? 8 : 17;
      if (!i)
        path.moveTo(x, y);
      else
        path.lineTo(x, y);
      path.lineTo(x + step, y);
      p.drawText(QRectF(x, 22, step, 36), Qt::AlignCenter, text.mid(i, 1));
    }
    if (data["final_drop"].toBool())
      path.lineTo(start + text.size() * step, 17);
    p.drawPath(path);
  }
};

AnkiPanel::AnkiPanel(AnkiSource &s, QSettings &prefs, QWidget *parent)
    : QGroupBox("Anki · local practice", parent), source(s), settings(prefs) {
  setObjectName("ankiPanel");
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(label("Practice vocabulary from your existing decks. Anki schedules stay untouched.", "muted"));
  auto *useAnki = button("Use Anki for practice", "quiet");
  useAnki->setObjectName("ankiUseForPractice");
  layout->addWidget(useAnki);
  connect(useAnki, &QPushButton::clicked, this, &AnkiPanel::practiceRequested);
  layout->addWidget(label("New, suspended and buried cards are excluded at sync.", "muted"));
  auto *buttons = new QHBoxLayout;
  refresh = button("Find decks");
  refresh->setObjectName("ankiFindDecks");
  sync = button("Sync selected decks", "primary");
  sync->setObjectName("ankiSync");
  buttons->addWidget(refresh);
  buttons->addWidget(sync);
  layout->addLayout(buttons);
  decks = new QListWidget;
  decks->setObjectName("ankiDecks");
  decks->setAccessibleName("Anki decks to include");
  decks->setStyleSheet(
      "QListWidget {font-size:16px;} QListWidget::item {padding:5px;} "
      "QListWidget::indicator {width:18px;height:18px;} "
      "QListWidget::indicator:unchecked {border:1px solid "
      "palette(text);border-radius:3px;background:transparent;}");
  decks->setMinimumHeight(120);
  decks->setMaximumHeight(220);
  layout->addWidget(decks);
  profile = settings.value("anki/profile").toString();
  const auto selected = settings.value("anki/decks").toStringList();
  const auto available =
      settings.value("anki/available_decks", selected).toStringList();
  for (const auto &name : available) {
    auto *item = new QListWidgetItem(name, decks);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(selected.contains(name) ? Qt::Checked : Qt::Unchecked);
  }
  status = label({}, "muted");
  status->setObjectName("ankiStatus");
  status->setAccessibleName("Anki connection status");
  layout->addWidget(status);
  updateSummary();
  auto *preview = button("Preview a cached card", "quiet");
  preview->setObjectName("ankiPreview");
  layout->addWidget(preview);
  connect(preview, &QPushButton::clicked, this, &AnkiPanel::previewRequested);
  layout->addWidget(label("Voice and recording preferences are in Sound & voice → Japanese voice.", "muted"));
  auto *helpToggle = new SettingsCheckBox("Connection and compatibility");
  layout->addWidget(helpToggle);
  auto *help =
      label("Open Anki with AnkiConnect on localhost:8765 to sync. Moji Nook "
            "reads in small batches, once a day while enabled or when you "
            "click Sync. Cached cards work with Anki closed; local media must "
            "still be installed.\n\nThis adapter needs Word, Word Reading and "
            "Word Meaning fields, and the first card template. Other layouts "
            "are skipped. Whole-card Anki grades inform both skills; Moji Nook "
            "tracks reading and meaning separately. WaniKani and Anki copies "
            "of a word remain separate records.",
            "muted");
  help->hide();
  layout->addWidget(help);
  connect(helpToggle, &QCheckBox::toggled, help, &QWidget::setVisible);
  connect(refresh, &QPushButton::clicked, this, [this] {
    if (source.busy())
      return;
    refresh->setEnabled(false);
    sync->setEnabled(false);
    setSourceStatus(status, "connecting", "Looking for Anki…");
    source.discover();
  });
  connect(sync, &QPushButton::clicked, this, &AnkiPanel::import);
  connect(&source, &AnkiSource::decksFound, this,
          [this](QString active, QStringList names) {
            const auto selected =
                active == profile ? settings.value("anki/decks").toStringList()
                                  : QStringList{};
            profile = active;
            decks->clear();
            settings.setValue("anki/available_decks", names);
            for (const auto &name : names) {
              auto *item = new QListWidgetItem(name, decks);
              item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
              item->setCheckState(selected.contains(name) ? Qt::Checked
                                                          : Qt::Unchecked);
            }
          });
  connect(&source, &AnkiSource::progress, this, [this](const QString &message) {
    setSourceStatus(status, "connecting", message);
  });
  connect(
      &source, &AnkiSource::finished, this, [this](bool ok, QString message) {
        refresh->setEnabled(true);
        sync->setEnabled(true);
        if (ok && importing) {
          settings.setValue("anki/profile", profile);
          settings.setValue("anki/decks", pendingDecks);
          settings.sync();
          emit changed();
        }
        importing = false;
        setSourceStatus(status, ok ? "connected" : "error", message);
        if (ok && !source.metadata().isEmpty())
          setSourceStatus(status, "connected",
              message + "\nLast sync · " +
              QDateTime::fromString(source.metadata()["synced_at"].toString(),
                                    Qt::ISODateWithMs)
                  .toLocalTime()
                  .toString("MMM d, h:mm AP"));
      });
}
void AnkiPanel::updateSummary() {
  const auto m = source.metadata();
  if (m.isEmpty())
    setSourceStatus(status, "offline", "Not connected · open Anki and find decks.");
  else
    setSourceStatus(status, "connected",
                  QString("%1 cached cards · %2\nLast sync · %3")
                        .arg(m["imported"].toInt())
                        .arg(m["profile"].toString(),
                             QDateTime::fromString(m["synced_at"].toString(),
                                                   Qt::ISODateWithMs)
                                 .toLocalTime()
                                 .toString("MMM d, h:mm AP")));
}
void AnkiPanel::import() {
  if (source.busy())
    return;
  pendingDecks.clear();
  for (int i = 0; i < decks->count(); ++i)
    if (decks->item(i)->checkState() == Qt::Checked)
      pendingDecks << decks->item(i)->text();
  if (pendingDecks.isEmpty()) {
    setSourceStatus(status, "offline", "Select at least one deck to sync.");
    return;
  }
  importing = true;
  refresh->setEnabled(false);
  sync->setEnabled(false);
  lastAttempt = QDateTime::currentDateTimeUtc();
  source.importDecks(profile, pendingDecks);
}
void AnkiPanel::syncIfDue() {
  const auto now = QDateTime::currentDateTimeUtc();
  if (settings.value("practice/source", "wanikani").toString() == "wanikani" ||
      !settings.value("anki/enabled", false).toBool() || source.busy() ||
      settings.value("anki/decks").toStringList().isEmpty())
    return;
  if (!automaticSyncDue(
          QDateTime::fromString(source.metadata()["synced_at"].toString(),
                                Qt::ISODateWithMs),
          lastAttempt, now))
    return;
  // Auto-sync saved opt-ins only, never unsaved checkbox edits/profile choices.
  lastAttempt = now;
  source.importDecks(settings.value("anki/profile").toString(),
                     settings.value("anki/decks").toStringList());
}

AnkiDetails::AnkiDetails(QWidget *parent) : QWidget(parent) {
  setObjectName("ankiDetails");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  word = button("Listen to word", "quiet");
  word->setObjectName("ankiWordAudio");
  layout->addWidget(word);
  audioStatus = label({}, "muted");
  layout->addWidget(audioStatus);
  audioStatus->hide();
  toggle = button("Show details", "quiet");
  toggle->setCheckable(true);
  toggle->setObjectName("ankiShowDetails");
  layout->addWidget(toggle);
  details = new QWidget;
  auto *body = new QVBoxLayout(details);
  body->setContentsMargins(0, 0, 0, 0);
  pitch = new PitchDiagram;
  pitch->setFixedHeight(65);
  body->addWidget(pitch);
  pitch->setAccessibleName("Pitch accent from Anki");
  japanese = label({});
  japanese->setStyleSheet("font-family:'Noto Sans CJK JP';font-size:21px;");
  body->addWidget(japanese);
  translation = label({});
  translation->setStyleSheet("font-size:16px;");
  body->addWidget(translation);
  text = label({});
  text->setStyleSheet("font-size:15px;");
  body->addWidget(text);
  sentence = button("Listen to sentence", "quiet");
  body->addWidget(sentence);
  picture = label({});
  picture->setAlignment(Qt::AlignCenter);
  body->addWidget(picture);
  layout->addWidget(details);
  details->hide();
  hide();
  connect(word, &QPushButton::clicked, this,
          [this] { play(subject.wordAudio); });
  connect(sentence, &QPushButton::clicked, this,
          [this] { play(subject.sentenceAudio); });
  connect(toggle, &QPushButton::toggled, this, [this](bool on) {
    if (on) {
      // Decode only after an explicit click; cap dimensions and allocation.
      picture->clear();
      picture->hide();
      QImageReader reader(subject.picture);
      const auto size = reader.size();
      if (size.isValid() && size.width() <= 8192 && size.height() <= 8192 &&
          qint64(size.width()) * size.height() <= 16000000) {
        reader.setScaledSize(size.scaled(180, 120, Qt::KeepAspectRatio));
        const auto image = reader.read();
        if (!image.isNull()) {
          picture->setPixmap(QPixmap::fromImage(image));
          picture->show();
        }
      }
    }
    details->setVisible(on);
    toggle->setText(on ? "Hide details" : "Show details");
    emit layoutChanged();
  });
}
void AnkiDetails::configure(bool on, int value, bool autoPlay) {
  enabled = on;
  autoplay = autoPlay;
  volume = qBound(0, value, 100);
  if (output)
    output->setVolume(volume / 100.0);
  if (!on || !volume)
    stop();
  word->setEnabled(on && volume > 0);
  sentence->setEnabled(on && volume > 0);
  word->setVisible(on && !subject.wordAudio.isEmpty());
  sentence->setVisible(on && !subject.sentenceAudio.isEmpty());
  if (on && volume > 0 && !player)
    QTimer::singleShot(0, this, &AnkiDetails::prepareAudio);
}
void AnkiDetails::setSubject(const Subject &s) {
  stop();
  subject = s;
  toggle->setChecked(false);
  details->hide();
  hide();
  audioStatus->clear();
  audioStatus->hide();
  QStringList lines;
  japanese->setText(s.contextJa);
  japanese->setVisible(!s.contextJa.isEmpty());
  const auto furigana = s.sourceData["sentence_furigana"].toString();
  // Tooltips auto-detect rich text; escape deck text so it shows literally.
  japanese->setToolTip(furigana.isEmpty() ? QString() : "<p>" + furigana.toHtmlEscaped() + "</p>");
  translation->setText(s.contextEn);
  translation->setVisible(!s.contextEn.isEmpty());
  static_cast<PitchDiagram *>(pitch)->data = s.sourceData["pitch"].toObject();
  pitch->setVisible(!s.sourceData["pitch"].toObject().isEmpty());
  const auto p = s.sourceData["pitch"].toObject();
  QStringList contour;
  for (const auto &v : p["high"].toArray())
    contour << (v.toBool() ? "high" : "low");
  pitch->setAccessibleDescription(p["text"].toString() + " · " +
                                  contour.join(" / "));
  pitch->update();
  if (!s.notes.isEmpty())
    lines << s.notes;
  if (!s.sourceData["pitch_notes"].toString().isEmpty())
    lines << s.sourceData["pitch_notes"].toString();
  if (!s.sourceData["frequency"].toString().isEmpty())
    lines << "Frequency rank · " + s.sourceData["frequency"].toString();
  text->setText(lines.join("\n\n"));
  word->setVisible(enabled && !s.wordAudio.isEmpty());
  sentence->setVisible(enabled && !s.sentenceAudio.isEmpty());
  toggle->setVisible(!lines.isEmpty() || !s.picture.isEmpty() ||
                     !s.contextJa.isEmpty() || !p.isEmpty());
}
void AnkiDetails::reveal() {
  if (subject.source != "anki")
    return;
  show();
  // Yield until after grading paints. Never block answer advancement on media.
  const QString id = subject.sourceId;
  const int epoch = audioEpoch;
  QTimer::singleShot(0, this, [this, id, epoch] {
    if (epoch == audioEpoch && isVisible() && subject.sourceId == id &&
        enabled && autoplay && volume)
      play(subject.wordAudio);
  });
}
void AnkiDetails::stop() {
  ++audioEpoch;
  if (player)
    player->stop();
}
void AnkiDetails::hideEvent(QHideEvent *event) {
  stop();
  QWidget::hideEvent(event);
}
void AnkiDetails::play(const QString &path) {
  if (!enabled || volume <= 0 || path.isEmpty() || !QFileInfo::exists(path) ||
      QGuiApplication::platformName() == "offscreen")
    return;
  prepareAudio();
  if (!player)
    return;
  output->setVolume(volume / 100.0);
  player->stop();
  player->setSource(QUrl::fromLocalFile(path));
  player->play();
}
void AnkiDetails::prepareAudio() {
  if (!enabled || volume <= 0 || QGuiApplication::platformName() == "offscreen")
    return;
  if (!player) {
    PerformanceSpan timing("prepare-anki-audio");
    output = new QAudioOutput(this);
    player = new QMediaPlayer(this);
    player->setAudioOutput(output);
    connect(player, &QMediaPlayer::errorOccurred, this, [this] {
      audioStatus->setText("Audio unavailable · practice still works");
      audioStatus->show();
      emit layoutChanged();
    });
  }
}
