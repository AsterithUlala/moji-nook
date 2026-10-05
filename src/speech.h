#pragma once
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QProcess>
#include <QTemporaryDir>
#include <QtCore>

// Local Japanese synthesis. No network requests or system-installed TTS service.
class JapaneseSpeech : public QObject {
  Q_OBJECT
public:
  explicit JapaneseSpeech(QObject *parent = nullptr);
  ~JapaneseSpeech() override;
  void configure(bool enabled, const QString &voice = "random", int volume = 35,
                 double rate = 1.0, const QString &engine = "fast");
  void prepare();
  void say(const QString &kanaText, const QString &utteranceKey = {});
  void replay();
  void stop();
  bool ready() const { return prepared; }
  bool available(const QString &engine = {}) const;
  QString engine() const { return configuredEngine; }
  QString lastVoice() const { return currentVoice; }
  QString utteranceKey() const { return currentKey; }
  static QStringList voices(const QString &engine = "fast");
  static QString voiceName(const QString &id);
  static QString bundleDirectory();
  static QString qualityWorkerPath();

signals:
  void readyChanged(bool ready);
  void started(QString voice);
  void generated(QString wavPath, QString voice);
  void error(QString message);

private:
  bool enabled = true, prepared = false;
  int volume = 35, epoch = 0;
  double rate = 1.0;
  QString configuredVoice = "random", currentVoice, currentText, currentKey;
  QString configuredEngine = "fast";
  QString lastWav;
  QTemporaryDir cache;
  QHash<QString, QString> generatedFiles;
  QHash<QString, QString> utteranceVoices;
  QStringList utteranceOrder;
  QStringList cacheOrder;
  QProcess *process = nullptr;
  QProcess *qualityWorker = nullptr;
  QTimer qualityIdle;
  QByteArray qualityOutput;
  bool qualityBusy = false;
  QMediaPlayer *player = nullptr;
  QAudioOutput *output = nullptr;
  void synthesize(const QString &text, const QString &voice);
  void synthesizeQuality(const QString &text, const QString &voice,
                         const QString &key, const QString &path);
  void closeQualityWorker();
  void cacheWave(const QString &key, const QString &path);
  void play(const QString &path);
};
