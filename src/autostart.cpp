#include "autostart.h"
#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {
#ifdef Q_OS_WIN
const QString runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const QString runName = "Moji Nook";
QString windowsCommand(const QStringList &command) {
  QStringList parts;
  for (auto part : command) {
    part = QDir::toNativeSeparators(part);
    parts << (part.contains(' ') || part.isEmpty() ? '"' + part + '"' : part);
  }
  return parts.join(' ');
}
#else
QString entryPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         "/autostart/moji-nook.desktop";
}
#endif
} // namespace

QStringList Autostart::launchCommand(const QStringList &args) {
  const QString appImage = qEnvironmentVariable("APPIMAGE");
  return QStringList{appImage.isEmpty() ? QCoreApplication::applicationFilePath()
                                        : appImage,
                     "--tray"} +
         args;
}

QString Autostart::desktopExec(const QStringList &command) {
  static const QString reserved = " \t\n\"'\\><~|&;$*?#()`";
  QStringList parts;
  for (QString part : command) {
    part.replace('%', "%%");
    if (part.isEmpty() ||
        std::any_of(part.begin(), part.end(),
                    [](QChar c) { return reserved.contains(c); })) {
      QString quoted = "\"";
      for (QChar c : part) {
        if (c == '"' || c == '`' || c == '$' || c == '\\')
          quoted += '\\';
        quoted += c;
      }
      part = quoted + '"';
    }
    // String-value escapes apply on top of Exec quoting.
    part.replace('\\', "\\\\").replace('\n', "\\n").replace('\t', "\\t");
    parts << part;
  }
  return parts.join(' ');
}

bool Autostart::enabled() {
#ifdef Q_OS_WIN
  return QSettings(runKey, QSettings::NativeFormat).contains(runName);
#else
  QFile file(entryPath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;
  // Desktops switch an entry off in place instead of deleting it.
  static const QRegularExpression off(
      "^(Hidden=true|X-GNOME-Autostart-enabled=false)\\s*$",
      QRegularExpression::MultilineOption);
  return !off.match(QString::fromUtf8(file.readAll())).hasMatch();
#endif
}

bool Autostart::setEnabled(bool on, const QStringList &args, QString *error) {
  auto fail = [error](const QString &message) {
    if (error)
      *error = message;
    return false;
  };
#ifdef Q_OS_WIN
  QSettings run(runKey, QSettings::NativeFormat);
  if (on)
    run.setValue(runName, windowsCommand(launchCommand(args)));
  else
    run.remove(runName);
  run.sync();
  return run.status() == QSettings::NoError ||
         fail("Windows did not accept the startup setting.");
#else
  const QString path = entryPath();
  if (!on) {
    QFile file(path);
    return !file.exists() || file.remove() || fail(file.errorString());
  }
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return fail("Could not create " + QFileInfo(path).absolutePath());
  const QByteArray entry = ("[Desktop Entry]\n"
                            "Type=Application\n"
                            "Name=Moji Nook\n"
                            "Comment=Short Japanese practice from WaniKani or Anki\n"
                            "Exec=" + desktopExec(launchCommand(args)) + "\n"
                            "Icon=moji-nook\n"
                            "Terminal=false\n"
                            "X-GNOME-Autostart-enabled=true\n")
                               .toUtf8();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(entry) != entry.size() ||
      !file.commit())
    return fail(file.errorString());
  return true;
#endif
}
