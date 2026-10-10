#include "style.h"
#include "theme.h"
#include "ui.h"
#include "print_ink.h"
#include "ui_helpers.h"
#ifdef MOJI_NOOK_LAYER_SHELL
#include <LayerShellQt/Window>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif
Card::Card(QWidget *parent)
    : QWidget(parent,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
  setWindowTitle("Moji Nook · Practice");
  setObjectName("practiceCard");
  setFixedWidth(qMin(352, screen()->availableGeometry().width() - 32));
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_AlwaysShowToolTips);
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  cardScroll = new QScrollArea;
  cardScroll->setObjectName("cardScroll");
  cardScroll->viewport()->setObjectName("cardViewport");
  cardScroll->setFrameShape(QFrame::NoFrame);
  cardScroll->setWidgetResizable(true);
  cardScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  cardScroll->setFocusPolicy(Qt::NoFocus);
  cardContent = new QWidget;
  cardContent->setObjectName("cardContent");
  auto *layout = new QVBoxLayout(cardContent);
  cardScroll->setWidget(cardContent);
  outer->addWidget(cardScroll);
  layout->setContentsMargins(20, 16, 20, 16);
  layout->setSpacing(9);
  auto *top = new QHBoxLayout;
  auto *brand = button("Moji Nook", "quiet");
  brand->setObjectName("cardBrand");
  brand->setIconSize(QSize(16, 16));
  brand->setFocusPolicy(Qt::TabFocus);
  brand->setToolTip("Open your practice overview");
  dismiss = button({}, "quiet");
  dismiss->setObjectName("dismiss");
  dismiss->setToolTip("Dismiss · does not count as a miss");
  dismiss->setAccessibleName("Dismiss card");
  dismiss->setFixedSize(32, 32);
  dismiss->setIconSize(QSize(28, 28));
  top->addWidget(brand);
  top->addStretch();
  top->addWidget(dismiss);
  layout->addLayout(top);
  auto *details = new QHBoxLayout;
  details->setSpacing(6);
  meta = label({}, "cardMeta");
  meta->setWordWrap(true);
  challengeType = label({}, "challengeType");
  challengeType->setWordWrap(true);
  challengeType->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  details->addWidget(meta);
  details->addStretch();
  details->addWidget(challengeType);
  // The task leads; source metadata moves below the answer controls.
  details->removeWidget(challengeType);
  auto *taskRow = new QHBoxLayout;
  taskRow->setSpacing(6);
  taskRow->addWidget(challengeType);
  taskRow->addStretch();
  layout->addLayout(taskRow);
  characters = label({}, "word");
  characters->setAlignment(Qt::AlignCenter);
  auto *wordStage = new PrintInkSurface(PrintInkSurface::Word);
  wordStage->setObjectName("wordStage");
  auto *wordRow = new QHBoxLayout(wordStage);
  wordRow->setContentsMargins(12, 18, 12, 18);
  wordRow->setSpacing(6);
  wordRow->addWidget(characters, 1);
  challengeMark = new ChallengeMark;
  challengeMark->setObjectName("wordChallengeMark");
  challengeMark->hide();
  wordKind = label({}, "wordKind");
  wordKind->setWordWrap(false);
  wordKind->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  wordKind->hide();
  taskRow->addWidget(wordKind, 0, Qt::AlignRight | Qt::AlignVCenter);
  taskRow->addWidget(challengeMark, 0, Qt::AlignRight | Qt::AlignVCenter);
  layout->addWidget(wordStage);
  wordProgress = label({}, "muted");
  wordProgress->setObjectName("wordProgress");
  wordProgress->setAlignment(Qt::AlignCenter);
  wordProgress->hide();
  layout->addWidget(wordProgress);
  frontContext = label({}, "ankiFrontContext");
  frontContext->setAlignment(Qt::AlignCenter);
  frontContext->setWordWrap(true);
  frontContext->setStyleSheet(
      "font-family:'Noto Sans CJK JP'; font-size:20px;");
  layout->addWidget(frontContext);
  prompt = label({});
  prompt->setAlignment(Qt::AlignCenter);
  layout->addWidget(prompt);
  input = new QLineEdit;
  input->setObjectName("readingInput");
  input->setPlaceholderText("Romaji or kana");
  input->setStyleSheet("font-family:'Noto Sans CJK JP'; font-size:23px;");
  input->setFocusPolicy(Qt::StrongFocus);
  input->setAccessibleName("Japanese reading");
  input->setAccessibleDescription(
      "Enter romaji or kana. Enter checks the answer.");
  layout->addWidget(input);
  kana = label({}, "muted");
  kana->setObjectName("kanaPreview");
  kana->setAlignment(Qt::AlignCenter);
  layout->addWidget(kana);
  readingWarning = label({});
  readingWarning->setObjectName("readingWarning");
  readingWarning->setAlignment(Qt::AlignCenter);
  readingWarning->setWordWrap(true);
  layout->addWidget(readingWarning);
  readingWarning->hide();
  check = button("Check reading", "primary");
  check->setObjectName("checkReading");
  layout->addWidget(check);
  choiceArea = new QWidget;
  choiceLayout = new QVBoxLayout(choiceArea);
  choiceLayout->setContentsMargins(0, 0, 0, 0);
  choiceLayout->setSpacing(7);
  layout->addWidget(choiceArea);
  reveal = button("Reveal answer", "primary");
  reveal->setObjectName("revealAnswer");
  reveal->setMinimumHeight(40);
  layout->addWidget(reveal);
  submittedRow = new QWidget;
  submittedRow->setObjectName("submittedRow");
  auto *submittedGroup = new QVBoxLayout(submittedRow);
  submittedGroup->setContentsMargins(0, 0, 0, 0);
  submittedGroup->setSpacing(4);
  auto *submittedCaption = label("Incorrect", "muted");
  submittedCaption->setAlignment(Qt::AlignCenter);
  submittedGroup->addWidget(submittedCaption);
  // Centered like the correct-answer row below so the two read as a pair.
  auto *submittedLayout = new QHBoxLayout;
  submittedGroup->addLayout(submittedLayout);
  submitted = label({}, "submittedAnswer");
  missIcon = new FeedbackIconLabel(submitted);
  missIcon->setAccessibleName("Incorrect answer");
  submittedLayout->addStretch();
  submittedLayout->addWidget(missIcon, 0, Qt::AlignVCenter);
  submittedLayout->addWidget(submitted);
  submittedLayout->addItem(new QSpacerItem(28, 0, QSizePolicy::Fixed,
                                           QSizePolicy::Minimum));
  submittedLayout->addStretch();
  layout->addWidget(submittedRow);
  answerCaption = label("Correct answer", "muted");
  answerCaption->setAlignment(Qt::AlignCenter);
  layout->addWidget(answerCaption);
  answer = label({}, "answer");
  answer->setAlignment(Qt::AlignCenter);
  answer->setWordWrap(true);
  auto *answerGroup = new QWidget;
  answerGroup->setObjectName("answerRow");
  auto *answerRow = new QHBoxLayout(answerGroup);
  answerRow->setContentsMargins(0, 0, 0, 0);
  answerRow->setSpacing(10);
  feedback = new FeedbackIconLabel(answer);
  feedback->setObjectName("resultIcon");
  auto feedbackPolicy = feedback->sizePolicy();
  feedbackPolicy.setRetainSizeWhenHidden(true);
  feedback->setSizePolicy(feedbackPolicy);
  answerRow->addStretch();
  answerRow->addWidget(feedback, 0, Qt::AlignVCenter);
  answerRow->addWidget(answer);
  answerRow->addItem(new QSpacerItem(28, 0, QSizePolicy::Fixed,
                                     QSizePolicy::Minimum));
  answerRow->addStretch();
  layout->addWidget(answerGroup);
  auto *speechRow = new QHBoxLayout;
  speechRow->setSpacing(6);
  listen = button("Listen", "quiet");
  listen->setObjectName("listenPronunciation");
  listen->setAccessibleName("Listen to the Japanese pronunciation");
  voiceCaption = label({}, "voiceCaption");
  voiceCaption->setStyleSheet("font-size:12px;");
  speechRow->addStretch();
  speechRow->addWidget(listen);
  speechRow->addWidget(voiceCaption);
  speechRow->addStretch();
  layout->addLayout(speechRow);
  connect(listen, &QPushButton::clicked, this, &Card::speakAnswer);
  listen->hide();
  voiceCaption->hide();
  supplement = label({}, "supplement");
  supplement->setAlignment(Qt::AlignCenter);
  layout->addWidget(supplement);
  exampleToggle = button("Show example", "quiet");
  exampleToggle->setObjectName("exampleToggle");
  exampleToggle->setCheckable(true);
  layout->addWidget(exampleToggle);
  context = label({}, "example");
  context->setAlignment(Qt::AlignCenter);
  layout->addWidget(context);
  ankiDetails = new AnkiDetails;
  layout->addWidget(ankiDetails);
  connect(ankiDetails, &AnkiDetails::layoutChanged, this, &Card::fitCard);
  connect(exampleToggle, &QPushButton::toggled, this, [this](bool expanded) {
    if (expanded)
      challenge.event["example_opened"] = true;
    context->setVisible(expanded);
    exampleToggle->setText(expanded ? "Hide example" : "Show example");
    fitCard();
  });
  auto *grades = new QHBoxLayout;
  miss = button("Missed", "miss");
  miss->setObjectName("markMissed");
  miss->setMinimumHeight(40);
  remember = button("Remembered", "primary");
  remember->setObjectName("markRemembered");
  remember->setMinimumHeight(40);
  grades->addWidget(miss);
  grades->addWidget(remember);
  layout->addLayout(grades);
  done = button("Done", "primary");
  done->setObjectName("finishCard");
  done->setMinimumHeight(40);
  layout->addWidget(done);
  layout->addLayout(details);
  hint = label({}, "cardHint");
  details->addWidget(hint, 1);
  hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  connect(brand, &QPushButton::clicked, this, &Card::dashboardRequested);
  connect(dismiss, &QPushButton::clicked, this, [this] { finish(true); });
  connect(reveal, &QPushButton::clicked, this, &Card::showAnswer);
  connect(remember, &QPushButton::clicked, this, [this] {
    if (!pending || resolved) return;
    result = 1;
    resolved = true;
    emit graded(true);
    finish();
  });
  connect(miss, &QPushButton::clicked, this, [this] {
    if (!pending || resolved) return;
    result = 0;
    resolved = true;
    emit graded(false);
    finish();
  });
  connect(done, &QPushButton::clicked, this, [this] { finish(); });
  auto submit = [this] {
    if (!resolved && !input->text().trimmed().isEmpty())
      grade(challenge.accepts(input->text()), input->text());
  };
  connect(check, &QPushButton::clicked, this, submit);
  connect(input, &QLineEdit::returnPressed, this, submit);
  connect(input, &QLineEdit::textChanged, this, [this](const QString &text) {
    kana->setText(toKana(text));
    kana->setVisible(input->isVisible() && !text.trimmed().isEmpty());
    check->setEnabled(!text.trimmed().isEmpty());
  });
  for (auto *child : findChildren<QWidget *>())
    child->installEventFilter(this);
  installEventFilter(this);
  accent = new CardAccent(this);
  applyTheme(defaultTheme);
  // Qt >= 6.5 selects layer-shell only for this QWindow; the dashboard stays a
  // normal window.
  winId();
#ifdef MOJI_NOOK_LAYER_SHELL
  if (QGuiApplication::platformName() == "wayland") {
    auto *layer = LayerShellQt::Window::get(windowHandle());
    layer->setScope("moji-nook-practice");
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(false);
    layer->setCloseOnDismissed(false); // Reposition on monitor removal.
    layer->setMargins(QMargins(16, 16, 16, 16));
    layer->setAnchors(
        LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorRight) |
        LayerShellQt::Window::AnchorBottom);
  }
#endif
  connect(qApp, &QGuiApplication::screenAdded, this,
          [this] { setMonitor(monitorName); });
  connect(qApp, &QGuiApplication::screenRemoved, this,
          [this] { setMonitor(monitorName); });
  connect(qApp, &QGuiApplication::primaryScreenChanged, this,
          [this] { setMonitor(monitorName); });
  setMonitor({});
}
void Card::setAnkiAudio(bool enabled, int volume, bool autoplay) {
  ankiDetails->configure(enabled, volume, autoplay);
}
void Card::setSpeech(JapaneseSpeech *engine, bool enabled, bool autoplay,
                     bool preferRecorded) {
  if (speech != engine) {
    if (speech)
      disconnect(speech, nullptr, this, nullptr);
    speech = engine;
    if (speech) {
      connect(speech, &JapaneseSpeech::generated, this,
              [this](const QString &, const QString &voice) {
        if (speech->utteranceKey() == speechKey && isVisible() && answerVisible && speechEnabled &&
            !speechKey.isEmpty()) {
          voiceCaption->setText(JapaneseSpeech::voiceName(voice));
          voiceCaption->show();
          fitCard();
        }
      });
      connect(speech, &JapaneseSpeech::error, this, [this](const QString &message) {
        if (speech->utteranceKey() == speechKey && isVisible() && answerVisible && speechEnabled) {
          voiceCaption->setText("Audio unavailable");
          voiceCaption->setToolTip(message);
          voiceCaption->show();
          fitCard();
        }
      });
    }
  }
  speechEnabled = enabled;
  speechAutoplay = autoplay;
  preferRecording = preferRecorded;
  const bool hasRecording = challenge.subject.source == "anki" &&
      !challenge.subject.wordAudio.isEmpty() &&
      QFileInfo::exists(challenge.subject.wordAudio);
  listen->setVisible(answerVisible && speechEnabled &&
      !(preferRecording && hasRecording) && !challenge.subject.reading().isEmpty());
  if (!speechEnabled) {
    voiceCaption->hide();
    if (speech)
      speech->stop();
  }
  fitCard();
}
void Card::speakAnswer() {
  if (!speech || !speechEnabled || !answerVisible)
    return;
  const bool recorded = preferRecording && challenge.subject.source == "anki" &&
      !challenge.subject.wordAudio.isEmpty() && QFileInfo::exists(challenge.subject.wordAudio);
  if (recorded || challenge.subject.reading().isEmpty())
    return;
  voiceCaption->setText("Preparing audio…");
  voiceCaption->setToolTip("Local Japanese voice · source reading");
  voiceCaption->show();
  // The primary accepted kana reading is authoritative; never guess from kanji.
  speech->say(challenge.subject.reading(), speechKey);
  fitCard();
}
void Card::hideEvent(QHideEvent *event) {
  if (speech && speech->utteranceKey() == speechKey)
    speech->stop();
  QWidget::hideEvent(event);
}
QScreen *Card::displayScreen() const {
  for (auto *candidate : QGuiApplication::screens())
    if (!monitorName.isEmpty() && candidate->name() == monitorName)
      return candidate;
  return QGuiApplication::primaryScreen();
}
void Card::setMonitor(const QString &screenName) {
  monitorName = screenName;
  auto *target = displayScreen();
  if (!target)
    return;
  if (placementScreen != target) {
    disconnect(screenGeometryConnection);
    placementScreen = target;
    screenGeometryConnection =
        connect(target, &QScreen::availableGeometryChanged, this,
                [this] { fitCard(); });
    if (windowHandle())
      windowHandle()->setScreen(target);
#ifdef MOJI_NOOK_LAYER_SHELL
    if (QGuiApplication::platformName() == "wayland")
      LayerShellQt::Window::get(windowHandle())->setScreen(target);
#endif
  }
  fitCard();
}
void Card::setCorner(int index) {
  cornerIndex = qBound(0, index, 3);
  const bool right = cornerIndex == 0 || cornerIndex == 2,
             bottom = cornerIndex < 2;
#ifdef MOJI_NOOK_LAYER_SHELL
  if (QGuiApplication::platformName() == "wayland") {
    auto *layer = LayerShellQt::Window::get(windowHandle());
    layer->setAnchors(LayerShellQt::Window::Anchors(
                          right ? LayerShellQt::Window::AnchorRight
                                : LayerShellQt::Window::AnchorLeft) |
                      (bottom ? LayerShellQt::Window::AnchorBottom
                              : LayerShellQt::Window::AnchorTop));
    return;
  }
#endif
  if (auto *target = displayScreen()) {
    const QRect area = target->availableGeometry();
    move(right ? area.x() + area.width() - width() - 16 : area.left() + 16,
         bottom ? area.y() + area.height() - height() - 16 : area.top() + 16);
  }
}
void Card::present(const Challenge &c, int minutes) {
  // Only a card replacing a visible one sits under a still-clicking pointer.
  if (isVisible())
    presented.start();
  else
    presented.invalidate();
  if (speech)
    speech->stop();
  answerVisible = false;
  speechKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
  listen->hide();
  voiceCaption->hide();
  ++motionEpoch;
  accent->stop();
  const bool continuing =
      isVisible() && isActiveWindow() &&
      !c.event["session_id"].toString().isEmpty() &&
      c.event["session_id"] == challenge.event["session_id"] &&
      c.event["session_position"].toInt() > 1;
  grantingClickFocus = false;
#ifdef MOJI_NOOK_LAYER_SHELL
  if (QGuiApplication::platformName() == "wayland") {
    LayerShellQt::Window::get(windowHandle())
        ->setKeyboardInteractivity(
            continuing ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                       : LayerShellQt::Window::KeyboardInteractivityNone);
  }
#endif
  challenge = c;
  const QString kind = c.subject.source == "wanikani" ? c.subject.kind : QString();
  const bool hasWordKind = kind == "kanji" || kind == "vocabulary" ||
                           kind == "kana_vocabulary";
  wordKind->setText(kind == "kanji" ? "Kanji" : "Vocab");
  wordKind->setAccessibleName(kind == "kanji" ? "WaniKani kanji" : "WaniKani vocabulary");
  wordKind->setVisible(hasWordKind);
  wordKind->setProperty("subjectKind", hasWordKind ? kind : QString());
  wordKind->style()->unpolish(wordKind);
  wordKind->style()->polish(wordKind);
  if (auto *stage = findChild<QFrame *>("wordStage")) {
    stage->setProperty("subjectKind", hasWordKind ? kind : QString());
    stage->update();
  }
  setProgressTile(std::nullopt);
  ankiDetails->setSubject(c.subject);
  challenge.event["reading_retry_enabled"] = readingRetries;
  readingWarning->clear();
  readingWarning->hide();
  challenge.event["presented_at"] =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  result = -1;
  engaged = resolved = false;
  pending = true;
  input->clear();
  dismiss->setToolTip("Dismiss without scoring · Esc");
  dismiss->setAccessibleName("Dismiss without scoring");
  input->setEnabled(true);
  answer->setStyleSheet(c.reading
                            ? "font-family:'Noto Sans CJK JP'; font-size:32px;"
                            : "font-size:23px;");
  submitted->setStyleSheet(
      c.reading ? "font-family:'Noto Sans CJK JP'; font-size:26px;"
                : "font-size:16px;");
  supplement->setStyleSheet(
      !c.reading && !c.subject.readings.isEmpty()
          ? "font-family:'Noto Sans CJK JP'; font-size:26px;"
      : c.subject.source == "anki" ? "font-size:18px;"
                                   : "font-size:17px;");
  meta->setText(
      c.subject.source == "anki"
          ? QString("Anki · %1").arg(c.subject.deck.section("::", -1).left(22))
          : QString("%1 · L%2")
                .arg(c.subject.kind == "kanji" ? "Kanji" : "Vocab")
                .arg(c.subject.level));
  meta->setToolTip(c.subject.source == "anki"
                       ? c.subject.deck
                       : "WaniKani · " + stageName(c.subject.stage));
  challengeType->setText(c.mode == Mode::Typing ? QString("Reading (Romaji/Kana)") : QString("%1 · %2").arg(
      c.reading ? "Reading" : "Meaning", c.mode == Mode::Typing   ? "Type"
                                         : c.mode == Mode::Choice ? "Choose"
                                                                  : "Recall"));
  characters->setText(c.subject.characters);
  // Keep longer vocabulary within the compact card.
  characters->setStyleSheet(QString("font-size:%1px")
                                .arg(c.subject.characters.size() > 6   ? 34
                                     : c.subject.characters.size() > 4 ? 44
                                                                       : 58));
  // Kaishi vocabulary can be ambiguous without the sentence shown on the
  // original card front. Keep answer-bearing translation and furigana hidden.
  frontContext->setText(c.subject.source == "anki" ? c.subject.contextJa
                                                    : QString());
  frontContext->setVisible(!frontContext->text().isEmpty());
  QString dimension = c.reading ? "reading" : "meaning";
  QString readingType =
      c.subject.kind == "kanji" && c.reading && !c.subject.readingKind.isEmpty()
          ? QString(" (%1)").arg(c.subject.readingKind == "onyomi" ? "on’yomi"
                                 : c.subject.readingKind == "kunyomi"
                                     ? "kun’yomi"
                                     : c.subject.readingKind)
          : "";
  prompt->setText(readingType.trimmed());
  prompt->setVisible(!readingType.isEmpty());
  input->setVisible(c.mode == Mode::Typing);
  kana->hide();
  check->setVisible(c.mode == Mode::Typing);
  check->setEnabled(false);
  reveal->setVisible(c.mode == Mode::Recall);
  choiceArea->setVisible(c.mode == Mode::Choice);
  while (auto *item = choiceLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  for (const auto &option : c.choices) {
    auto *b = button({});
    b->setProperty("role", "choice");
    b->setAccessibleName(option);
    b->setProperty("answer", option);
    auto *optionLayout = new QVBoxLayout(b);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    auto *optionLabel =
        label(option, c.reading ? "readingChoice" : "meaningChoice");
    b->setMinimumHeight(c.reading ? 50 : 44);
    optionLabel->setAlignment(Qt::AlignCenter);
    optionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    optionLayout->addWidget(optionLabel);
    b->installEventFilter(this);
    choiceLayout->addWidget(b);
    connect(b, &QPushButton::clicked, this, [this, option] {
      if (!resolved)
        grade(challenge.accepts(option), option);
    });
  }
  for (QWidget *w :
       QList<QWidget *>{answer, feedback, supplement, context, miss, remember,
                        done, submittedRow, answerCaption, exampleToggle})
    w->hide();
  answer->parentWidget()->hide();
  exampleToggle->setChecked(false);
  Q_UNUSED(minutes);
  hint->clear();
  hint->hide();
  hint->setToolTip("Esc dismisses without scoring. " + c.selectionReason);
  const int position = c.event["session_position"].toInt(1);
  const int total = c.event["session_total"].toInt(1);
  if (c.event.contains("session_id")) {
    hint->setText(QString("Card %1 of %2").arg(position).arg(total));
    hint->show();
    hint->setToolTip("Esc ends the session. " + c.selectionReason);
    done->setText(position < total
                      ? QString("Next · %1 of %2").arg(position + 1).arg(total)
                      : "Finish session");
    remember->setText(position < total ? "Remembered · next"
                                       : "Remembered · finish");
    miss->setText(position < total ? "Missed it · next" : "Missed it · finish");
    dismiss->setToolTip("End session without scoring this card · Esc");
    dismiss->setAccessibleName("End practice session");
  } else {
    done->setText("Done");
    remember->setText("Remembered");
    miss->setText("Missed it");
  }
  fitCard();
  show();
  if (!reducedMotion) {
    const int outcome = c.event["transition_outcome"].toInt(-1);
    accent->pulse(outcome < 0 ? colors(themeIndex).muted
                  : outcome   ? colors(themeIndex).accent
                              : colors(themeIndex).negative);
  }
  if (continuing) {
    engaged = true;
    active.start();
    challenge.event["first_interaction_at"] = challenge.event["presented_at"];
    if (c.mode == Mode::Typing)
      input->setFocus(Qt::OtherFocusReason);
    else if (c.mode == Mode::Recall)
      reveal->setFocus(Qt::OtherFocusReason);
    else if (auto *choice = choiceArea->findChild<QPushButton *>())
      choice->setFocus(Qt::OtherFocusReason);
  }
  QTimer::singleShot(0, this, &Card::fitCard);
#ifdef Q_OS_WIN
  SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
#endif
}
bool Card::eventFilter(QObject *watched, QEvent *event) {
  if (watched == cardContent && event->type() == QEvent::LayoutRequest) {
    QTimer::singleShot(0, this, &Card::fitCard);
  }
  // A double-click on "Next" must not answer the card that replaces it.
  if ((event->type() == QEvent::MouseButtonPress ||
       event->type() == QEvent::MouseButtonDblClick) &&
      event->spontaneous() && watched != dismiss &&
      qobject_cast<QAbstractButton *>(watched) && presented.isValid() &&
      presented.elapsed() < 350)
    return true;
  if (pending && event->type() == QEvent::MouseButtonPress &&
      watched != dismiss && watched->objectName() != "cardBrand") {
#ifdef MOJI_NOOK_LAYER_SHELL
    if (QGuiApplication::platformName() == "wayland") {
      auto *layer = LayerShellQt::Window::get(windowHandle());
      if (layer->keyboardInteractivity() ==
          LayerShellQt::Window::KeyboardInteractivityNone) {
        // Explicit click grants focus; downgrade once activation arrives so
        // the card never keeps an exclusive keyboard grab during practice.
        grantingClickFocus = true;
        layer->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityExclusive);
        update(); // Layer-shell state is applied with the next surface commit.
      }
    }
#endif
  }
#ifdef MOJI_NOOK_LAYER_SHELL
  if (grantingClickFocus && event->type() == QEvent::WindowActivate) {
    grantingClickFocus = false;
    LayerShellQt::Window::get(windowHandle())
        ->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityOnDemand);
    update();
  }
#endif
  if (!engaged && pending &&
      (event->type() == QEvent::MouseButtonPress ||
       event->type() == QEvent::KeyPress)) {
    engaged = true;
    active.start();
    challenge.event["first_interaction_at"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  }
  if (pending && event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        !resolved && challenge.mode == Mode::Typing &&
        (watched == input || watched == check)) {
      if (!key->isAutoRepeat() && !input->text().trimmed().isEmpty())
        grade(challenge.accepts(input->text()), input->text());
      return true; // Consume submission; it must not bubble up and finish the
                   // newly graded card.
    }
    if (key->key() == Qt::Key_Escape) {
      if (!key->isAutoRepeat())
        finish(true);
      return true;
    }
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        resolved) {
      if (!key->isAutoRepeat())
        finish();
      return true;
    }
    if (key->isAutoRepeat() &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
         key->key() == Qt::Key_Space))
      return true;
  }
  return false;
}
void Card::fitCard() {
  auto *target = displayScreen();
  if (!target)
    return;
  const QRect area = target->availableGeometry();
  const int desiredWidth = qMin(352, qMax(1, area.width() - 32));
  if (width() != desiredWidth)
    setFixedWidth(desiredWidth);
  cardContent->ensurePolished();
  cardContent->layout()->activate();
  const int maxHeight = qMin(560, qMax(1, area.height() - 32));
  int needed = cardContent->layout()->totalHeightForWidth(width());
  if (needed > maxHeight) {
    const int scrollbar = cardScroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    needed = cardContent->layout()->totalHeightForWidth(qMax(1, width() - scrollbar));
  }
  const int contentHeight = qMax(needed, cardContent->minimumSizeHint().height());
  if (cardContent->minimumHeight() != contentHeight)
    cardContent->setMinimumHeight(contentHeight);
  // Extra examples and deck notes scroll instead of growing across the user's
  // workspace. Normal prompts keep their natural compact height.
  const int desired =
      qMin(contentHeight + 4, qMin(560, qMax(1, area.height() - 32)));
  if (height() != desired)
    setFixedHeight(desired);
  setCorner(cornerIndex);
}
void Card::showAnswer() {
  const bool firstReveal = !answerVisible;
  answerVisible = true;
  if (!challenge.event.contains("revealed_at"))
    challenge.event["revealed_at"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  reveal->hide();
  answer->setText(challenge.answer());
  answer->parentWidget()->show();
  answer->show();
  QString supplementary = challenge.reading
                              ? challenge.subject.meanings.join(" / ")
                              : challenge.subject.readings.join(" / ");
  QString example = challenge.subject.contextJa;
  if (!challenge.subject.contextEn.isEmpty())
    example += "\n" + challenge.subject.contextEn;
  supplement->setText(supplementary);
  supplement->setVisible(!supplementary.isEmpty());
  context->setText(example);
  context->hide();
  exampleToggle->setChecked(false);
  exampleToggle->setVisible(!example.isEmpty());
  if (challenge.subject.source == "anki") {
    exampleToggle->hide();
    ankiDetails->reveal();
  }
  if (challenge.mode == Mode::Recall) {
    miss->show();
    remember->show();
    if (engaged && isActiveWindow())
      remember->setFocus(Qt::OtherFocusReason);
  }
  const bool recorded = preferRecording && challenge.subject.source == "anki" &&
      !challenge.subject.wordAudio.isEmpty() && QFileInfo::exists(challenge.subject.wordAudio);
  listen->setVisible(speechEnabled && !recorded &&
                    !challenge.subject.reading().isEmpty());
  if (firstReveal && speechEnabled && speechAutoplay && !recorded)
    speakAnswer();
  fitCard();
}
void Card::grade(bool correct, const QString &response) {
  if (resolved)
    return;
  const auto retryHint =
      readingRetries ? challenge.readingRetryHint(response) : QString();
  if (!correct && !retryHint.isEmpty()) {
    auto retries = challenge.event["reading_retries"].toArray();
    retries.append(QJsonObject{
        {"submitted_answer", response},
        {"normalized_answer", toKana(response)},
        {"at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"hint", retryHint}});
    challenge.event["reading_retries"] = retries;
    readingWarning->setText(retryHint);
    readingWarning->show();
    input->selectAll();
    input->setFocus(Qt::OtherFocusReason);
    fitCard();
    return;
  }
  readingWarning->hide();
  challenge.event["graded_after_interaction_ms"] =
      engaged ? QJsonValue(double(active.elapsed())) : QJsonValue();
  challenge.event["submitted_answer"] = response;
  challenge.event["normalized_answer"] =
      challenge.reading ? toKana(response) : normalizeMeaning(response);
  challenge.event["graded_at"] =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  challenge.event["graded_local_day"] = QDate::currentDate().toString(Qt::ISODate);
  resolved = true;
  result = correct ? 1 : 0;
  input->setEnabled(false);
  input->hide();
  kana->hide();
  submitted->setText(response);
  submitted->setAccessibleName("Your answer: " + response);
  submittedRow->setVisible(!correct);
  answerCaption->setVisible(!correct);
  check->hide();
  choiceArea->hide();
  showAnswer();
  applyTheme(themeIndex);
  feedback->show();
  done->show();
  dismiss->setToolTip("Finish and keep this result · Esc ends session");
  dismiss->setAccessibleName("Finish and keep this result");
  if (engaged && isActiveWindow())
    done->setFocus(Qt::OtherFocusReason);
  fitCard();
  if (!reducedMotion)
    accent->pulse(
        correct ? colors(themeIndex).accent : colors(themeIndex).negative, 320);
  emit graded(correct);
}
void Card::setReducedMotion(bool reduced) {
  reducedMotion = reduced;
  if (reduced) {
    accent->stop();
    if (!pending)
      hide();
  }
}
void Card::applyTheme(int index) {
  themeIndex = index;
  const auto c = colors(index);
  findChild<QPushButton *>("cardBrand")->setIcon(brandMark(index));
  challengeMark->setTheme(index);
  if (auto *stage = findChild<QFrame *>("wordStage")) {
    stage->setProperty("printTheme", index);
    stage->update();
  }
  auto inputPalette = input->palette();
  const QColor muted(c.muted), surface(c.surface);
  inputPalette.setColor(QPalette::PlaceholderText, QColor::fromRgbF(
      muted.redF() * .8 + surface.redF() * .2,
      muted.greenF() * .8 + surface.greenF() * .2,
      muted.blueF() * .8 + surface.blueF() * .2));
  input->setPalette(inputPalette);
  dismiss->setIcon(QIcon(symbolPixmap(true, c.muted, 28, false)));
  if (resolved) {
    feedback->setPixmap(symbolPixmap(false, c.accent, 28, true));
    feedback->setAccessibleName("Correct answer");
    feedback->setToolTip(feedback->accessibleName());
  }
  missIcon->setPixmap(symbolPixmap(true, c.negative, 28, true));
}
void Card::setProgressTile(const std::optional<ProgressTile> &tile) {
  challengeMark->setVisible(tile && tile->challenged());
  wordProgress->setVisible(tile && tile->eligible);
  if (tile && tile->eligible) {
    wordProgress->setText(tile->settled ? "Settled · Reading + meaning"
                            : tile->phase == TilePhase::Growing ? "Growing" : "Seen");
    wordProgress->setToolTip("Reading and meaning, remembered over time. "
                            "Your own recall ratings count.");
  }
  fitCard();
}
void Card::finish(bool endSession) {
  if (speech && speech->utteranceKey() == speechKey)
    speech->stop();
  ankiDetails->stop();
  if (!pending) {
    hide();
    return;
  }
  pending = false;
  challenge.event["end_session"] = endSession;
  if (result >= 0 && !challenge.event.contains("graded_at")) {
    challenge.event["graded_at"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    challenge.event["graded_local_day"] = QDate::currentDate().toString(Qt::ISODate);
  }
  if (!challenge.event.contains("example_opened"))
    challenge.event["example_opened"] = false;
  if (!resolved && !input->text().isEmpty())
    challenge.event["unfinished_input"] = input->text();
  grantingClickFocus = false;
  const bool another = !endSession && result >= 0 &&
                       challenge.event["session_position"].toInt(1) <
                           challenge.event["session_total"].toInt(1);
  if (!another) {
    if (!reducedMotion && !endSession && result >= 0) {
      accent->pulse(result ? colors(themeIndex).accent
                           : colors(themeIndex).negative,
                    180);
      const int epoch = motionEpoch;
      QTimer::singleShot(180, this, [this, epoch] {
        if (epoch == motionEpoch && !pending)
          hide();
      });
    } else
      hide();
  }
  emit completed(challenge, result, engaged ? active.elapsed() : 0);
}
void Card::closeEvent(QCloseEvent *event) {
  finish(true);
  event->accept();
}
