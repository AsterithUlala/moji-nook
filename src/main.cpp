#include "ui.h"
#include <iostream>

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setApplicationName("Moji Nook");
  application.setApplicationDisplayName("Moji Nook");
  application.setDesktopFileName("moji-nook");
  application.setOrganizationName("MojiNook");
  application.setApplicationVersion("0.1.0-alpha");
  application.setQuitOnLastWindowClosed(false);
  application.setStyle("Fusion");
  application.setStyleSheet(appStyle());
  application.setWindowIcon(appIcon());
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Small, read-only Japanese practice from WaniKani or Anki.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"data-dir", "Use a separate data directory.", "directory"});
  parser.addOption(
      {"demo",
       "Use sample words in an isolated demo directory; no API requests."});
  parser.addOption({"tray", "Start in the tray."});
  parser.addOption({"sync-only", "Sync and exit without showing windows."});
  parser.addOption(
      {"anki-import",
       "Read a local Anki deck and enable local practice, then exit.",
       "deck"});
  parser.addOption(
      {"anki-preview",
       "Save cached Anki card screenshots without scoring, then exit.",
       "directory"});
  parser.addOption(
      {"preview", "Save demo UI screenshots and exit.", "directory"});
  parser.addOption({"progression-preview", "Save sample word-tile and recap screenshots and exit.", "directory"});
  parser.process(application);
  QString data = parser.value("data-dir");
  if (data.isEmpty())
    data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (parser.isSet("demo") || parser.isSet("preview") || parser.isSet("progression-preview"))
    data += "-demo";
  if (!QDir().mkpath(data)) {
    std::cerr << "Could not create the practice data directory.\n";
    return 1;
  }
  QLockFile lock(data + "/app.lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0)) {
    std::cerr << "Moji Nook is already running for this data directory. Open it "
                 "from the system tray.\n";
    return 2;
  }
  Store store(data);
  if (!store.ready()) {
    std::cerr << "Could not open practice data: " << store.error.toStdString()
              << '\n';
    return 1;
  }
  if (parser.isSet("sync-only")) {
    QFile file(data + "/token");
    if (!file.open(QIODevice::ReadOnly)) {
      std::cerr << "No token saved.\n";
      return 1;
    }
    Sync sync(store);
    QObject::connect(&sync, &Sync::progress, [](const QString &s) {
      std::cout << s.toStdString() << std::endl;
    });
    QObject::connect(
        &sync, &Sync::finished, &application, [&](bool ok, const QString &s) {
          std::cout << s.toStdString() << std::endl;
          if (ok) {
            auto c = store.cache();
            auto p = parseSubjects(c["subjects"].toArray(),
                                   c["assignments"].toArray(),
                                   c["study_materials"].toArray(),
                                   c["user"]
                                       .toObject()["subscription"]
                                       .toObject()["max_level_granted"]
                                       .toInt(60));
            std::cout << p.size() << " learned items available.\n";
          }
          application.exit(ok ? 0 : 1);
        });
    QTimer::singleShot(0, &sync,
                       [&] { sync.start(QString::fromUtf8(file.readAll())); });
    return application.exec();
  }
  if (parser.isSet("anki-import")) {
    if (parser.isSet("demo") || parser.isSet("preview"))
      return 1;
    AnkiSource anki(store);
    QString profile;
    QStringList availableDecks;
    bool importing = false;
    const QString deck = parser.value("anki-import");
    QObject::connect(&anki, &AnkiSource::decksFound, &application,
                     [&](QString active, QStringList decks) {
                       profile = active;
                       availableDecks = decks;
                       if (!decks.contains(deck)) {
                         std::cerr << "Deck not found.\n";
                         application.exit(1);
                         return;
                       }
                       QTimer::singleShot(0, &anki, [&] {
                         importing = true;
                         anki.importDecks(profile, {deck});
                       });
                     });
    QObject::connect(
        &anki, &AnkiSource::finished, &application,
        [&](bool ok, const QString &message) {
          std::cout << message.toStdString() << '\n';
          if (!ok) {
            application.exit(1);
            return;
          }
          if (!importing)
            return;
          QSettings settings(data + "/settings.ini", QSettings::IniFormat);
          settings.setValue("anki/profile", profile);
          settings.setValue("anki/decks", QStringList{deck});
          settings.setValue("anki/available_decks", availableDecks);
          settings.setValue("anki/enabled", true);
          if (!settings.contains("practice/source"))
            settings.setValue("practice/source", "anki");
          settings.sync();
          application.exit(settings.status() == QSettings::NoError ? 0 : 1);
        });
    QTimer::singleShot(0, &anki, &AnkiSource::discover);
    return application.exec();
  }
  App app(store, data);
  if (parser.isSet("progression-preview"))
    return app.saveProgressionPreview(parser.value("progression-preview")) ? 0 : 1;
  if (parser.isSet("anki-preview"))
    return app.saveAnkiPreview(parser.value("anki-preview")) ? 0 : 1;
  if (parser.isSet("preview"))
    return app.savePreview(parser.value("preview")) ? 0 : 1;
  app.start(parser.isSet("demo"), parser.isSet("tray"));
  return application.exec();
}
