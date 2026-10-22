#pragma once
#include <QStringList>

// Login startup lives in the desktop's own record (an XDG autostart entry on
// Linux, the HKCU Run key on Windows); that record is the source of truth.
namespace Autostart {
// Program and arguments for login: $APPIMAGE when set, then --tray and args.
QStringList launchCommand(const QStringList &args);
// Exec value quoted per the Desktop Entry specification.
QString desktopExec(const QStringList &command);
bool enabled();
// Idempotent; disabling removes only Moji Nook's own entry.
bool setEnabled(bool on, const QStringList &args, QString *error = nullptr);
} // namespace Autostart
