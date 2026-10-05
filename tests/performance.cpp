#include "ui.h"
#include <algorithm>
#include <iostream>

// Run only against a disposable snapshot; never point this at a live profile.
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  if (argc != 2)
    return 2;
  const QString directory = QString::fromLocal8Bit(argv[1]);
  if (!QFile::exists(directory + "/.moji-nook-performance-snapshot") ||
      QFile::exists(directory + "/token")) {
    std::cerr << "Use a marked disposable snapshot without a token.\n";
    return 2;
  }
  Store store(directory);
  if (!store.ready())
    return 3;
  const auto cache = store.cache();
  const auto pool =
      parseSubjects(cache["subjects"].toArray(), cache["assignments"].toArray(),
                    cache["study_materials"].toArray());
  if (pool.isEmpty())
    return 4;
  std::cout << "learned_items=" << pool.size() << '\n';
  auto measure = [&](const char *name, auto work, int repeats = 12) {
    QVector<double> samples;
    for (int i = 0; i < repeats; ++i) {
      QElapsedTimer timer;
      timer.start();
      work();
      samples.append(timer.nsecsElapsed() / 1e6);
    }
    std::sort(samples.begin(), samples.end());
    std::cout << name << " median_ms=" << samples[samples.size() / 2]
              << " max_ms=" << samples.last() << std::endl;
  };
  Evidence evidence;
  measure("evidence", [&] { evidence = store.evidence(); });
  Picker picker(42);
  const auto recent = store.recentSubjects();
  Challenge challenge;
  measure("pick_recall", [&] {
    challenge = picker.next(pool, {}, recent, evidence, 25, 20, 100, 0, {}, 30);
  });
  measure("pick_typed", [&] {
    challenge = picker.next(pool, {}, recent, evidence, 25, 20, 0, 100, {}, 30);
  });
  measure("pick_choice", [&] {
    challenge = picker.next(pool, {}, recent, evidence, 25, 20, 0, 0, {}, 30);
  });
  measure("record", [&] { store.record(challenge, true, 100); });
  measure("overview", [&] { store.statsHtml(pool); });
  measure("insights_details", [&] { store.insightsHtml(pool); });
  measure("chart_queries", [&] { loadChartData(store); });
  Progression progression(store.db);
  measure("word_tiles", [&] { progression.refresh(pool); });
  measure("history", [&] { store.historyHtml(); });
  Card card;
  measure("present_card", [&] { card.present(challenge, 5); });
  card.hide();
  App coordinator(store, QString::fromLocal8Bit(argv[1]));
  coordinator.start(false, true); // Snapshot has no token; no sync can occur.
  QPushButton *practice = nullptr;
  QWidget *dashboard = nullptr;
  for (auto *w : QApplication::topLevelWidgets())
    if (auto *b = w->findChild<QPushButton *>("studioBegin")) {
      practice = b;
      dashboard = w;
    }
  if (!practice)
    return 5;
  measure(
      "full_dashboard_refresh",
      [&] {
        coordinator.showDashboard();
        app.processEvents();
      },
      5);
  measure(
      "completion_dashboard_visible",
      [&] {
        practice->click();
        for (auto *w : QApplication::topLevelWidgets())
          if (auto *active = qobject_cast<Card *>(w);
              active && active->isVisible()) {
            active->completed(active->challenge, 1, 100);
            break;
          }
        app.processEvents();
      },
      12);
  dashboard->hide();
  measure(
      "completion_dashboard_hidden",
      [&] {
        practice->click();
        for (auto *w : QApplication::topLevelWidgets())
          if (auto *active = qobject_cast<Card *>(w);
              active && active->isVisible()) {
            active->completed(active->challenge, 1, 100);
            break;
          }
        app.processEvents();
      },
      12);
  if (QGuiApplication::platformName() != "offscreen") {
    FeedbackAudio feedback;
    bool heard[2] = {};
    QObject::connect(&feedback, &FeedbackAudio::playbackStarted, &app,
                     [&](bool correct) { heard[correct ? 1 : 0] = true; });
    feedback.configure(true, 5);
    measure(
        "audio_start_stop",
        [&] {
          feedback.play(true);
          QEventLoop wait;
          QTimer::singleShot(350, &wait, &QEventLoop::quit);
          wait.exec();
        },
        4);
    feedback.play(false);
    QEventLoop wait;
    QTimer::singleShot(350, &wait, &QEventLoop::quit);
    wait.exec();
    std::cout << "both_feedback_tones_started=" << (heard[0] && heard[1])
              << std::endl;
    if (!heard[0] || !heard[1])
      return 6;
  }
}
