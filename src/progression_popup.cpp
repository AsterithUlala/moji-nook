#include "progression_popup.h"
#include "theme.h"
#include "style.h"
#include <QtWidgets>
#ifdef MOJI_NOOK_LAYER_SHELL
#include <LayerShellQt/Window>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

ProgressRecap::ProgressRecap(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint |
                          Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus) {
  setObjectName("progressionRecap");
  setWindowTitle("Moji Nook · Session progress");
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_AlwaysShowToolTips);
  setFocusPolicy(Qt::NoFocus);
  setFixedWidth(352);
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  scroll = new QScrollArea;
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFocusPolicy(Qt::NoFocus);
  content = new QWidget;
  auto *layout = new QVBoxLayout(content);
  scroll->setWidget(content);
  outer->addWidget(scroll);
  layout->setContentsMargins(20, 16, 20, 16);
  layout->setSpacing(12);
  brand = new QLabel("Moji Nook");
  brand->setObjectName("recapBrand");
  auto *header = new QWidget(content);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(8);
  headerLayout->addWidget(brand);
  headerLayout->addStretch();
  closeButton = new QToolButton(header);
  closeButton->setObjectName("recapClose");
  closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  closeButton->setToolTip(tr("Close session recap"));
  closeButton->setAccessibleName(tr("Close session recap"));
  closeButton->setFocusPolicy(Qt::NoFocus);
  closeButton->setAutoRaise(true);
  closeButton->setFixedSize(32, 32);
  closeButton->setIconSize(QSize(28, 28));
  headerLayout->addWidget(closeButton);
  connect(closeButton, &QToolButton::clicked, this, &ProgressRecap::dismiss);
  title = new QLabel;
  title->setObjectName("recapTitle");
  title->setWordWrap(true);
  message = new QLabel;
  message->setObjectName("recapMessage");
  message->setWordWrap(true);
  composition = new WordTileCanvas;
  composition->setObjectName("recapComposition");
  composition->setCompact(true);
  summary = new QLabel;
  summary->setObjectName("recapSummary");
  summary->setWordWrap(true);
  linger = new QLabel("Closes after 5 seconds · Hover to pause");
  linger->setObjectName("recapLinger");
  for (auto *label : {brand, title, message, summary, linger})
    label->setTextFormat(Qt::PlainText);
  layout->addWidget(header);
  layout->addWidget(title);
  layout->addWidget(message);
  layout->addWidget(composition);
  layout->addWidget(summary);
  layout->addWidget(linger);
  for (auto *child : findChildren<QWidget *>()) {
    child->setMouseTracking(true);
    child->installEventFilter(this);
  }
  installEventFilter(this);
  lifetime.setSingleShot(true);
  lifetime.setTimerType(Qt::PreciseTimer);
  connect(&lifetime, &QTimer::timeout, this, &ProgressRecap::dismiss);
  winId();
#ifdef MOJI_NOOK_LAYER_SHELL
  if (QGuiApplication::platformName() == "wayland") {
    auto *layer = LayerShellQt::Window::get(windowHandle());
    layer->setScope("moji-nook-progression");
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(false);
    layer->setCloseOnDismissed(false);
    layer->setMargins(QMargins(16, 16, 16, 16));
  }
#endif
  connect(qApp, &QGuiApplication::screenAdded, this, [this] { place(); });
  connect(qApp, &QGuiApplication::screenRemoved, this, [this] { place(); });
  connect(qApp, &QGuiApplication::primaryScreenChanged, this,
          [this] { place(); });
  setTheme(defaultTheme);
}

void ProgressRecap::present(const QVector<ProgressNotice> &notices,
                           const ProgressState &state) {
  if (notices.isEmpty()) {
    dismiss();
    return;
  }
  lifetime.stop();
  remaining = 5000;
  intentionalHover = false;
  appearedAt = QCursor::pos();
  QVector<ProgressNotice> ordered = notices;
  auto priority = [](ProgressNotice::Kind kind) {
    return kind == ProgressNotice::Completed ? 0
           : kind == ProgressNotice::ChallengeCleared ? 1
           : kind == ProgressNotice::Developed ? 2 : 3;
  };
  std::stable_sort(ordered.begin(), ordered.end(), [&](const auto &a, const auto &b) {
    return priority(a.kind) < priority(b.kind);
  });
  QSet<int> highlighted;
  QSet<int> completed, cleared, developed;
  int consistency = 0;
  for (const auto &notice : ordered) {
    if (notice.kind == ProgressNotice::Consistency)
      consistency = qMax(consistency, notice.practiceDays);
    else {
      highlighted.insert(notice.subjectId);
      if (notice.kind == ProgressNotice::Completed)
        completed.insert(notice.subjectId);
      else if (notice.kind == ProgressNotice::ChallengeCleared)
        cleared.insert(notice.subjectId);
      else
        developed.insert(notice.subjectId);
    }
  }
  title->setText(tr("Session progress"));
  QVector<ProgressTile> tiles;
  QSet<int> shown;
  for (const auto &notice : ordered) {
    for (const auto &tile : state.tiles) {
      if (tile.id == notice.subjectId && tiles.size() < 2 &&
          !shown.contains(tile.id)) {
        shown.insert(tile.id);
        tiles.append(tile);
      }
    }
  }
  // A consistency acknowledgment can show the most recently practiced tiles.
  if (tiles.isEmpty())
    for (const auto &tile : state.tiles) {
      if (tiles.size() == 2)
        break;
      tiles.append(tile);
    }
  composition->setTiles(tiles);
  composition->setHighlightedIds(highlighted);
  composition->setVisible(!tiles.isEmpty());
  const int forming = std::count_if(state.tiles.cbegin(), state.tiles.cend(),
      [](const ProgressTile &tile) { return tile.eligible && !tile.settled; });
  QStringList changes;
  QStringList totals;
  if (!completed.isEmpty()) {
    changes << tr("Settled +%1").arg(completed.size());
    totals << tr("Settled · %1 total").arg(state.settled);
  }
  if (!developed.isEmpty()) {
    changes << tr("Taking shape +%1").arg(developed.size());
    totals << tr("Taking shape · %1 total").arg(forming);
  }
  if (!cleared.isEmpty())
    changes << tr("Challenges cleared +%1").arg(cleared.size());
  if (consistency > 0) {
    changes << tr("Practice day %1 reached").arg(consistency);
    totals << tr("Practice days · %1 total").arg(state.practiceDays);
  }
  QStringList changedWords;
  for (const auto &notice : ordered)
    if (!notice.characters.isEmpty() && !changedWords.contains(notice.characters))
      changedWords << notice.characters;
  message->setText(changes.join(QStringLiteral("  ·  ")) +
                   (changedWords.isEmpty() ? QString() : "\n" + changedWords.join(" · ")));
  summary->setText(totals.join(QStringLiteral("\n")));
  setAccessibleName(title->text());
  QStringList words;
  for (const auto &tile : tiles) words << tile.characters;
  setAccessibleDescription(message->text() + " " + words.join(", ") + " " + summary->text());
  place();
  show();
  if (!reducedMotion) composition->animateSettled(completed);
  // Compact tiles can wrap after their first layout at the actual popup width.
  QTimer::singleShot(0, this, [this] { if (isVisible()) place(); });
  // A parked pointer at the previous Finish button must not pin the recap.
  // Moving over the recap or returning to it is an intentional linger.
  lifetime.start(remaining);
#ifdef Q_OS_WIN
  SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
#endif
}

void ProgressRecap::setTheme(int index) {
  themeIndex = index;
  const auto c = colors(index);
  setStyleSheet(QString(
      "QWidget#progressionRecap {background:%1; color:%2;}"
      "QLabel {background:transparent; color:%2;}"
      "QLabel#recapBrand {color:%3; font-size:16px; font-weight:500;}"
      "QLabel#recapTitle {font-size:22px; font-weight:600;}"
      "QLabel#recapMessage {font-size:14px;}"
      "QLabel#recapSummary {color:%3; font-size:12px;}"
      "QLabel#recapLinger {color:%3; font-size:11px;}"
      "QToolButton#recapClose {color:%3; background:transparent; border:1px solid transparent; border-radius:6px;}"
      "QToolButton#recapClose:hover {color:%2; background:%4; border-color:%5;}"
      "QToolButton#recapClose:pressed {background:%5;}")
      .arg(c.background, c.text, c.muted, c.surface, c.border));
  closeButton->setIcon(QIcon(symbolPixmap(true, c.muted, 28, false)));
  composition->setTheme(index);
  scroll->setStyleSheet(QString("QScrollArea, QScrollArea > QWidget > QWidget {background:%1; border:none;}").arg(c.background));
}

void ProgressRecap::setPlacement(const QString &monitor, int corner) {
  monitorName = monitor;
  cornerIndex = qBound(0, corner, 3);
  place();
}

void ProgressRecap::place() {
  QScreen *target = QGuiApplication::primaryScreen();
  for (auto *screen : QGuiApplication::screens())
    if (!monitorName.isEmpty() && screen->name() == monitorName)
      target = screen;
  if (!target)
    return;
  if (placementScreen != target) {
    disconnect(geometryConnection);
    placementScreen = target;
    geometryConnection = connect(target, &QScreen::availableGeometryChanged,
                                 this, [this] { place(); });
    windowHandle()->setScreen(target);
#ifdef MOJI_NOOK_LAYER_SHELL
    if (QGuiApplication::platformName() == "wayland")
      LayerShellQt::Window::get(windowHandle())->setScreen(target);
#endif
  }
  const QRect area = target->availableGeometry();
  setFixedWidth(qMin(352, qMax(1, area.width() - 32)));
  content->ensurePolished();
  content->layout()->activate();
  const int needed = qMax(content->layout()->totalHeightForWidth(width() - 16),
                         content->minimumSizeHint().height());
  content->setMinimumHeight(needed);
  setFixedHeight(qMin(needed + 4, qMin(400, qMax(1, area.height() - 32))));
  const bool right = cornerIndex == 0 || cornerIndex == 2;
  const bool bottom = cornerIndex < 2;
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
  move(right ? area.right() - width() - 15 : area.left() + 16,
       bottom ? area.bottom() - height() - 15 : area.top() + 16);
}

void ProgressRecap::setReducedMotion(bool reduced) {
  reducedMotion = reduced;
  if (reduced) composition->stopSettlement();
}
void ProgressRecap::dismiss() {
  lifetime.stop();
  hide();
}
int ProgressRecap::remainingTime() const {
  return lifetime.isActive() ? lifetime.remainingTime() : remaining;
}
void ProgressRecap::enterEvent(QEnterEvent *event) {
  if (lifetime.isActive() &&
      (QCursor::pos() - appearedAt).manhattanLength() > 2) {
    intentionalHover = true;
    remaining = qMax(1, lifetime.remainingTime());
    lifetime.stop();
  }
  QWidget::enterEvent(event);
}
void ProgressRecap::leaveEvent(QEvent *event) {
  const bool wasLingering = intentionalHover;
  intentionalHover = false;
  if (isVisible() && wasLingering)
    lifetime.start(qMax(1, remaining));
  QWidget::leaveEvent(event);
}
bool ProgressRecap::eventFilter(QObject *, QEvent *event) {
  if (isVisible() && !intentionalHover &&
      (event->type() == QEvent::Wheel ||
       (event->type() == QEvent::MouseMove &&
        (QCursor::pos() - appearedAt).manhattanLength() > 2))) {
    intentionalHover = true;
    if (lifetime.isActive()) {
      remaining = qMax(1, lifetime.remainingTime());
      lifetime.stop();
    }
  }
  return false;
}
void ProgressRecap::hideEvent(QHideEvent *event) {
  lifetime.stop();
  QWidget::hideEvent(event);
}
