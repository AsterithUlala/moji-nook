#include "ui.h"
#include "display_preview.h"
#include "theme.h"
#include <LayerShellQt/Window>
#include <QtTest>
#include <iostream>

class NativeClickObserver : public QObject {
public:
  using QObject::QObject;
  QSet<QString> pressedControls;
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::MouseButtonPress) {
      const auto *mouse = static_cast<QMouseEvent *>(event);
      pressedControls.insert(watched->objectName());
      std::cout << "Native pointer press "
                << watched->objectName().toStdString()
                << "; spontaneous: " << (event->spontaneous() ? "yes" : "no")
                << "; synthesized: "
                << (mouse->source() == Qt::MouseEventNotSynthesized ? "no" : "yes")
                << std::endl;
      if (!event->spontaneous() ||
          mouse->source() != Qt::MouseEventNotSynthesized)
        qApp->exit(2);
    }
    return false;
  }
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  app.setStyleSheet(appStyle());
  app.setQuitOnLastWindowClosed(false);
  if (app.arguments().contains("--progression-only")) {
    const QPoint oldCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([oldCursor] { QCursor::setPos(oldCursor); });
    QProcess reference;
    reference.setProcessChannelMode(QProcess::ForwardedChannels);
    reference.start(app.applicationFilePath(), {"--reference-only"});
    ProgressRecap recap;
    ProgressState state;
    ProgressTile tile;
    tile.id = 1; tile.characters = "日本語"; tile.reading = "にほんご";
    tile.meaning = "Japanese language"; tile.eligible = tile.settled = true;
    tile.phase = TilePhase::Settled; tile.progress = 1;
    tile.readingSuccesses = tile.meaningSuccesses = 3;
    state.tiles = {tile}; state.total = state.settled = 1;
    QTimer::singleShot(1800, &app, [&] {
      if (app.applicationState() == Qt::ApplicationActive) {
        std::cerr << "Separate reference application did not acquire focus.\n";
        app.exit(2);
        return;
      }
      // Keep the test pointer outside every corner popup; deliberate hover is
      // verified separately and must not interfere with the expiry observation.
      QCursor::setPos(QGuiApplication::primaryScreen()->geometry().center());
      recap.present({{ProgressNotice::Completed, 1, tile.characters, 0}}, state);
    });
    const auto screens = orderedDisplays();
    int step = 0;
    QTimer placement;
    placement.setInterval(qMin(300, 3800 / (screens.size() * 4 + 1)));
    QObject::connect(&placement, &QTimer::timeout, &app, [&] {
      if (step > 0) {
        auto *expected = screens[(step - 1) / 4];
        const int corner = (step - 1) % 4;
        auto *layer = LayerShellQt::Window::get(recap.windowHandle());
        const auto anchors = LayerShellQt::Window::Anchors(
            (corner == 0 || corner == 2) ? LayerShellQt::Window::AnchorRight
                                        : LayerShellQt::Window::AnchorLeft) |
            ((corner < 2) ? LayerShellQt::Window::AnchorBottom
                          : LayerShellQt::Window::AnchorTop);
        const bool correct = recap.windowHandle()->isExposed() &&
            recap.screen() == expected && layer->anchors() == anchors &&
            layer->keyboardInteractivity() == LayerShellQt::Window::KeyboardInteractivityNone &&
            !layer->activateOnShow() && layer->exclusionZone() == 0 &&
            app.applicationState() != Qt::ApplicationActive &&
            recap.height() <= qMin(400, expected->availableGeometry().height() - 32);
        std::cout << "Passive recap display " << (step - 1) / 4 + 1 << ", corner "
                  << corner << ": " << (correct ? "yes" : "no") << std::endl;
        if (!correct) { app.exit(1); return; }
      }
      if (step == screens.size() * 4) { placement.stop(); return; }
      recap.setPlacement(screens[step / 4]->name(), step % 4);
      ++step;
    });
    QTimer::singleShot(1850, &placement, [&] { placement.start(); });
    QTimer::singleShot(7600, Qt::PreciseTimer, &app, [&] {
      const bool passed = !recap.isVisible() && step == screens.size() * 4 &&
                          app.applicationState() != Qt::ApplicationActive;
      std::cout << "Recap closed automatically with external focus retained: "
                << (passed ? "yes" : "no") << "; visible: " << recap.isVisible()
                << "; remaining: " << recap.remainingTime() << std::endl;
      app.exit(passed ? 0 : 1);
    });
    const int result = app.exec();
    reference.terminate();
    reference.waitForFinished(1000);
    return result;
  }
  if (app.arguments().contains("--speech-only")) {
    JapaneseSpeech speech;
    const bool quality = app.arguments().contains("--quality");
    speech.configure(true, quality ? "metan" : "mei", 10, 1.0,
                     quality ? "quality" : "fast");
    QObject::connect(&speech, &JapaneseSpeech::error, &app,
                     [&](const QString &message) {
      std::cerr << message.toStdString() << std::endl;
      app.exit(1);
    });
    QObject::connect(&speech, &JapaneseSpeech::started, &app,
                     [&](const QString &) {
      QTimer::singleShot(1500, &app, [&] {
        const auto *player = speech.findChild<QMediaPlayer *>();
        const bool decoded = player && player->duration() > 0 &&
                             player->position() > 0 &&
                             player->error() == QMediaPlayer::NoError;
        std::cout << "Bundled Japanese speech decoded and played: "
                  << (decoded ? "yes" : "no") << std::endl;
        app.exit(decoded ? 0 : 1);
      });
    });
    QTimer::singleShot(0, &app, [&] { speech.say("にほんご", "desktop-audio"); });
    QTimer::singleShot(10000, &app, [&] {
      std::cerr << "Japanese speech playback timed out.\n";
      app.exit(1);
    });
    return app.exec();
  }
  QWidget target;
  target.setWindowTitle("Moji Nook desktop check · closes automatically");
  auto *layout = new QVBoxLayout(&target);
  auto *title =
      new QLabel("Moji Nook desktop check\n\nThis fullscreen window should retain "
                 "keyboard focus when the practice card appears.\nThis check "
                 "closes automatically in a few seconds.");
  title->setAlignment(Qt::AlignCenter);
  layout->addWidget(title);
  auto *edit = new QLineEdit;
  edit->setPlaceholderText("Keyboard focus stays here");
  layout->addWidget(edit);
  target.setScreen(QGuiApplication::primaryScreen());
  const bool external = app.arguments().contains("--external-buttons");
  if (!external) {
    target.showFullScreen();
    target.activateWindow();
    edit->setFocus();
  }
  if (app.arguments().contains("--reference-only")) {
    QTimer::singleShot(800, &app, [&] {
      std::cout << "Separate reference application active: "
                << (app.applicationState() == Qt::ApplicationActive ? "yes"
                                                                     : "no")
                << std::endl;
    });
    QTimer::singleShot(15000, &app, [&] { app.quit(); });
    return app.exec();
  }
  QProcess reference;
  reference.setProcessChannelMode(QProcess::ForwardedChannels);
  auto openReference = [&] {
    if (reference.state() != QProcess::NotRunning) {
      reference.terminate();
      reference.waitForFinished(500);
    }
    reference.start(app.applicationFilePath(), {"--reference-only"});
  };
  if (external)
    openReference();
  Card card;
  card.setReducedMotion(false);
  if (app.arguments().contains("--identify-displays")) {
    QTimer::singleShot(2200, &app, [] { showDisplayIdentifiers(defaultTheme); });
    QTimer::singleShot(3200, &app, [&] {
      QSet<QScreen *> identified;
      bool exposed = true;
      for (auto *window : QApplication::topLevelWidgets()) {
        if (window->objectName() != "displayIdentifier")
          continue;
        identified.insert(window->screen());
        exposed &= window->windowHandle()->isExposed();
        if (qEnvironmentVariableIsSet("MOJI_NOOK_IDENTIFIER_PREVIEW"))
          window->grab().save(qEnvironmentVariable("MOJI_NOOK_IDENTIFIER_PREVIEW") +
              QString("-%1.png").arg(orderedDisplays().indexOf(window->screen()) + 1));
      }
      std::cout << "All display identifiers mapped: "
                << (exposed && identified.size() == orderedDisplays().size() ? "yes" : "no")
                << "; count: " << identified.size() << std::endl;
      if (!exposed || identified.size() != orderedDisplays().size() ||
          QGuiApplication::focusWindow() != target.windowHandle())
        app.exit(1);
    });
  }
  JapaneseSpeech speech;
  bool pronunciationPlayed = false;
  if (app.arguments().contains("--speech")) {
    const bool quality = app.arguments().contains("--quality");
    speech.configure(true, quality ? "metan" : "mei", 10, 1.0,
                     quality ? "quality" : "fast");
    card.setSpeech(&speech, true, true, false);
    QObject::connect(&speech, &JapaneseSpeech::started, &app,
                     [&](const QString &) { pronunciationPlayed = true; });
    QObject::connect(&speech, &JapaneseSpeech::error, &app,
                     [&](const QString &message) {
      std::cerr << message.toStdString() << std::endl;
      app.exit(1);
    });
    QTimer::singleShot(4200, &app, [&] {
      card.findChild<QLineEdit *>("readingInput")->setText(card.challenge.subject.reading());
      card.findChild<QPushButton *>("checkReading")->click();
    });
    QTimer::singleShot(7800, &app, [&] {
      const auto *player = speech.findChild<QMediaPlayer *>();
      const bool decoded = player && player->duration() > 0 && player->position() > 0 &&
                           player->error() == QMediaPlayer::NoError;
      std::cout << "Bundled Japanese pronunciation started playback: "
                << (pronunciationPlayed && decoded ? "yes" : "no") << std::endl;
      if (!pronunciationPlayed || !decoded)
        app.exit(1);
    });
  }
  FeedbackAudio audio;
  bool played[2] = {};
  audio.configure(app.arguments().contains("--feedback"), 10);
  QObject::connect(&audio, &FeedbackAudio::playbackStarted, &app,
                   [&](bool correct) { played[correct ? 1 : 0] = true; });
  QObject::connect(&audio, &FeedbackAudio::unavailable, &app, [&] {
    std::cerr << "Feedback audio output unavailable.\n";
    app.exit(1);
  });
  if (app.arguments().contains("--feedback")) {
    QTimer::singleShot(2500, &app, [&] { audio.play(true); });
    QTimer::singleShot(5300, &app, [&] { audio.play(false); });
    QTimer::singleShot(7900, &app, [&] {
      std::cout << "Both feedback sounds started playback: "
                << (played[0] && played[1] ? "yes" : "no") << std::endl;
      if (!played[0] || !played[1])
        app.exit(1);
    });
  }
  Challenge c;
  c.subject = demoSubjects().first();
  c.mode = Mode::Typing;
  c.reading = true;
  c.event = {{"session_id", "desktop-test"},
             {"session_position", 1},
             {"session_total", 2}};
  if (app.arguments().contains("--buttons") || external)
    c.mode = Mode::Recall;
  QObject::connect(&card, &Card::completed, &app,
                   [&](Challenge previous, int result, qint64) {
                     if (result >= 0 &&
                         !previous.event["end_session"].toBool() &&
                         previous.event["session_position"].toInt() == 1) {
                       c.event["session_position"] = 2;
                       card.present(c, 5);
                     }
                   });
  QTimer::singleShot(1800, &app, [&] {
    if ((!external && QGuiApplication::focusWindow() != target.windowHandle()) ||
        (external && app.applicationState() == Qt::ApplicationActive)) {
      std::cerr
          << "Fullscreen reference did not acquire focus; test inconclusive.\n";
      app.exit(2);
      return;
    }
    card.present(c, 5);
  });
  QTimer::singleShot(3600, &app, [&] {
    auto *focused = QGuiApplication::focusWindow();
    bool retained = external ? app.applicationState() != Qt::ApplicationActive
                             : focused == target.windowHandle();
    bool mapped = card.windowHandle()->isExposed();
    std::cout << "Card mapped over fullscreen: " << (mapped ? "yes" : "no")
              << "\nFullscreen keyboard focus retained: "
              << (retained ? "yes" : "no") << std::endl;
    if (!mapped || focused == card.windowHandle())
      app.exit(1);
    else if (!retained) {
      std::cerr << "Focus moved outside the test application; inconclusive "
                   "(user interaction or another application).\n";
      app.exit(2);
    }
  });
  QTimer::singleShot(external ? 10200 : 8500, &app, [&] { app.exit(0); });
  if (external) {
    auto *observer = new NativeClickObserver(&app);
    QRect desktop;
    for (const auto *screen : QGuiApplication::screens())
      desktop = desktop.united(screen->geometry());
    std::cout << "NATIVE_DESKTOP " << desktop.x() << " " << desktop.y()
              << " " << desktop.width() << " " << desktop.height()
              << std::endl;
    auto nativeTarget = [&, observer](const QString &name) {
      auto *button = card.findChild<QPushButton *>(name);
      const bool inactive = app.applicationState() != Qt::ApplicationActive;
      std::cout << "Whole Moji Nook application inactive before "
                << name.toStdString() << ": " << (inactive ? "yes" : "no")
                << std::endl;
      if (!inactive) {
        app.exit(2);
        return;
      }
      QObject::connect(button, &QPushButton::pressed, &app,
                       [name] { std::cout << "Native pressed "
                                         << name.toStdString() << std::endl; });
      QObject::connect(button, &QPushButton::clicked, &app,
                       [name] { std::cout << "Native clicked "
                                         << name.toStdString() << std::endl; });
      button->installEventFilter(observer);
      const QRect area = card.screen()->availableGeometry();
      const QPoint origin(area.x() + area.width() - card.width() - 16,
                          area.y() + area.height() - card.height() - 16);
      const auto pos = origin + button->mapTo(&card, button->rect().center());
      std::cout << "NATIVE_TARGET " << name.toStdString() << " " << pos.x()
                << " " << pos.y() << std::endl;
    };
    QTimer::singleShot(4000, &app, [&, nativeTarget] { nativeTarget("revealAnswer"); });
    QTimer::singleShot(5300, &app, [&, observer] {
      if (!observer->pressedControls.contains("revealAnswer")) {
        std::cerr << "Native press missed reveal target; check inconclusive.\n";
        app.exit(2);
        return;
      }
      const bool revealed =
          card.findChild<QPushButton *>("markRemembered")->isVisible();
      std::cout << "New card reveals on first native click: "
                << (revealed ? "yes" : "no") << std::endl;
      if (!revealed)
        app.exit(1);
    });
    QTimer::singleShot(5700, &app, openReference);
    QTimer::singleShot(7300, &app, [&, nativeTarget] { nativeTarget("markRemembered"); });
    QTimer::singleShot(9000, &app, [&, observer] {
      if (!observer->pressedControls.contains("markRemembered")) {
        std::cerr << "Native press missed grade target; check inconclusive.\n";
        app.exit(2);
        return;
      }
      const bool advanced = card.challenge.event["session_position"].toInt() == 2;
      std::cout << "Returning from another app grades on first native click: "
                << (advanced ? "yes" : "no") << std::endl;
      if (!advanced)
        app.exit(1);
    });
  }
  if (app.arguments().contains("--buttons")) {
    // Separate press/release lets native focus changes run during a real-length
    // click, unlike mouseClick's immediate synthetic press/release pair.
    QTimer::singleShot(4400, &app, [&] {
      QTest::mousePress(card.findChild<QPushButton *>("revealAnswer"),
                        Qt::LeftButton);
    });
    QTimer::singleShot(4600, &app, [&] {
      QTest::mouseRelease(card.findChild<QPushButton *>("revealAnswer"),
                          Qt::LeftButton);
    });
    QTimer::singleShot(5200, &app, [&] {
      const bool revealed =
          card.findChild<QPushButton *>("markRemembered")->isVisible();
      std::cout << "Inactive reveal works on first press/release: "
                << (revealed ? "yes" : "no") << std::endl;
      if (!revealed)
        app.exit(1);
    });
  }
  if (app.arguments().contains("--interaction")) {
    QTimer::singleShot(7100, &app, [&] {
      auto *input = card.findChild<QLineEdit *>("readingInput");
      QTest::keyClicks(input, "nihon");
      QTest::keyClick(input, Qt::Key_Return);
      QTest::keyClick(input, Qt::Key_Return);
    });
    QTimer::singleShot(7700, &app, [&] {
      auto *input = card.findChild<QLineEdit *>("readingInput");
      const bool continued =
          card.challenge.event["session_position"].toInt() == 2 &&
          QGuiApplication::focusWindow() == card.windowHandle() &&
          input->hasFocus() && input->text().isEmpty();
      std::cout << "Second card retains intentional typing focus: "
                << (continued ? "yes" : "no") << std::endl;
      if (!continued)
        app.exit(1);
    });
    QTimer::singleShot(4400, &app, [&] {
      auto *input = card.findChild<QLineEdit *>("readingInput");
      QTest::mouseClick(input, Qt::LeftButton);
    });
    QTimer::singleShot(6500, &app, [&] {
      bool focused = QGuiApplication::focusWindow() == card.windowHandle() &&
                     card.findChild<QLineEdit *>("readingInput")->hasFocus();
      std::cout << "Focused window: "
                << (QGuiApplication::focusWindow()
                        ? QGuiApplication::focusWindow()->title().toStdString()
                        : "none")
                << "; widget: "
                << (app.focusWidget()
                        ? app.focusWidget()->objectName().toStdString()
                        : "none")
                << std::endl;
      std::cout << "Reading input focused after simulated click: "
                << (focused ? "yes" : "no") << std::endl;
      bool released = LayerShellQt::Window::get(card.windowHandle())
                          ->keyboardInteractivity() ==
                      LayerShellQt::Window::KeyboardInteractivityOnDemand;
      std::cout << "Keyboard grant returned to on-demand: "
                << (released ? "yes" : "no") << std::endl;
      if (!focused || !released)
        app.exit(1);
    });
  }
  const int result = app.exec();
  if (reference.state() != QProcess::NotRunning) {
    reference.terminate();
    reference.waitForFinished(500);
  }
  return result;
}
