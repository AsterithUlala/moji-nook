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

// Quality voices, one adult and one child of each gender. Models are loaded
// on demand from quality/models/<model>.
struct QualityVoice {
  const char *id, *name, *credit, *model;
  int style;
};
inline constexpr QualityVoice qualityVoices[] = {
    {"itako", "Itako · woman", "VOICEVOX:東北イタコ", "21.vvm", 109},
    {"takehiro", "Takehiro · man", "VOICEVOX:玄野武宏", "4.vvm", 11},
    {"kotarou", "Kotarō · boy", "VOICEVOX:白上虎太郎", "9.vvm", 12},
    {"kiritan", "Kiritan · girl", "VOICEVOX:東北きりたん", "21.vvm", 108},
};
inline const QualityVoice *qualityVoice(const QString &id) {
  for (const auto &voice : qualityVoices)
    if (id == QLatin1String(voice.id))
      return &voice;
  return nullptr;
}
inline const QualityVoice *qualityVoiceForStyle(int style) {
  for (const auto &voice : qualityVoices)
    if (voice.style == style)
      return &voice;
  return nullptr;
}
