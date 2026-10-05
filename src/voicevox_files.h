#pragma once
#include <QString>

// Paths relative to the speech bundle directory.
#ifdef Q_OS_WIN
inline QString voicevoxCoreLibrary() { return QStringLiteral("quality/lib/voicevox_core.dll"); }
inline QString voicevoxRuntimeLibrary() { return QStringLiteral("quality/lib/voicevox_onnxruntime.dll"); }
inline QString qualityWorkerFileName() { return QStringLiteral("moji-nook-quality-worker.exe"); }
#else
inline QString voicevoxCoreLibrary() { return QStringLiteral("quality/lib/libvoicevox_core.so"); }
inline QString voicevoxRuntimeLibrary() { return QStringLiteral("quality/lib/libvoicevox_onnxruntime.so.1.17.3"); }
inline QString qualityWorkerFileName() { return QStringLiteral("moji-nook-quality-worker"); }
#endif
