#include "display_preview.h"
#include "theme.h"
#include <algorithm>
#ifdef MOJI_NOOK_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

QList<QScreen *> orderedDisplays() {
  auto screens = QGuiApplication::screens();
  std::sort(screens.begin(), screens.end(), [](const auto *a, const auto *b) {
    const auto x = a->geometry(), y = b->geometry();
    return x.y() == y.y() ? x.x() < y.x() : x.y() < y.y();
  });
  return screens;
}

QString displayLabel(const QScreen *screen, int number) {
  const auto size = screen->geometry().size();
  QString model = screen->model().trimmed();
  if (model.isEmpty())
    model = screen->manufacturer().trimmed();
  return QString("Display %1%2 · %3 × %4")
      .arg(number)
      .arg(model.isEmpty() ? QString() : " · " + model)
      .arg(size.width()).arg(size.height());
}

void showDisplayIdentifiers(int theme) {
  // Repeated clicks replace the previous markers rather than stacking them.
  for (auto *window : QApplication::topLevelWidgets())
    if (window->objectName() == "displayIdentifier")
      window->deleteLater();
  const auto c = colors(theme);
  const auto screens = orderedDisplays();
  for (int i = 0; i < screens.size(); ++i) {
    auto *screen = screens[i];
    auto *marker = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
    marker->setObjectName("displayIdentifier");
    marker->setAttribute(Qt::WA_ShowWithoutActivating);
    marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    marker->setAttribute(Qt::WA_DeleteOnClose);
    marker->setAccessibleName(QString("Display %1 identifier").arg(i + 1));
    marker->setStyleSheet(QString(
        "QWidget#displayIdentifier {background:%1;border:2px solid %2;"
        "border-radius:16px;} QLabel {background:transparent;color:%3;"
        "border:0;font-family:'Noto Sans';}")
        .arg(c.background, c.action, c.text));
    auto *layout = new QVBoxLayout(marker);
    layout->setContentsMargins(24, 16, 24, 20);
    auto *number = new QLabel(QString::number(i + 1));
    number->setAlignment(Qt::AlignCenter);
    number->setStyleSheet("font-size:64px;font-weight:600;");
    auto *detail = new QLabel(displayLabel(screen, i + 1));
    detail->setAlignment(Qt::AlignCenter);
    detail->setWordWrap(true);
    detail->setStyleSheet("font-size:14px;");
    layout->addWidget(number);
    layout->addWidget(detail);
    if (screen == QGuiApplication::primaryScreen()) {
      auto *primary = new QLabel("Primary display");
      primary->setAlignment(Qt::AlignCenter);
      primary->setStyleSheet("font-size:13px;");
      layout->addWidget(primary);
    }
    marker->setFixedSize(qMax(1, qMin(380, screen->availableGeometry().width() - 32)), 176);
    marker->winId();
    marker->windowHandle()->setScreen(screen);
#ifdef MOJI_NOOK_LAYER_SHELL
    if (QGuiApplication::platformName() == "wayland") {
      auto *layer = LayerShellQt::Window::get(marker->windowHandle());
      layer->setScope("moji-nook-display-identifier");
      layer->setLayer(LayerShellQt::Window::LayerOverlay);
      layer->setScreen(screen);
      layer->setExclusiveZone(0);
      layer->setAnchors({});
      layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
      layer->setActivateOnShow(false);
    }
#endif
    const auto area = screen->availableGeometry();
    marker->move(area.center() - marker->rect().center());
    marker->show();
    QObject::connect(screen, &QObject::destroyed, marker, &QWidget::close);
    QTimer::singleShot(2500, marker, &QWidget::close);
  }
}
