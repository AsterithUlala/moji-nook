#include "speech.h"
#include "voicevox_files.h"
#include <QGuiApplication>

JapaneseSpeech::JapaneseSpeech(QObject *parent) : QObject(parent) {
  qualityIdle.setSingleShot(true);
  connect(&qualityIdle, &QTimer::timeout, this, [this] {
    if (!qualityBusy)
      closeQualityWorker();
  });
}
JapaneseSpeech::~JapaneseSpeech() {
  stop();
  closeQualityWorker();
  for (auto *running : findChildren<QProcess *>())
    if (running->state() != QProcess::NotRunning)
      running->waitForFinished(1000);
}
QStringList JapaneseSpeech::voices(const QString &engine) {
  if (engine != "quality")
    return {"mei", "takumi", "tohoku"};
  QStringList ids;
  for (const auto &voice : qualityVoices)
    ids << voice.id;
  return ids;
}
QString JapaneseSpeech::voiceName(const QString &id) {
  if (const auto *voice = qualityVoice(id))
    return QString::fromUtf8(voice->name);
  return id == "mei" ? "Mei" : id == "takumi" ? "Takumi"
                            : id == "tohoku" ? "Tohoku" : "Random voice";
}
QString JapaneseSpeech::bundleDirectory() {
  const auto app = QCoreApplication::applicationDirPath();
  QStringList candidates{app + "/speech", app + "/../share/moji-nook/speech"};
#ifdef MOJI_NOOK_SPEECH_DIR
  candidates << QString::fromUtf8(MOJI_NOOK_SPEECH_DIR);
#endif
  for (const auto &dir : candidates)
    if (QFileInfo::exists(dir + "/dictionary/sys.dic"))
      return QDir(dir).absolutePath();
  return {};
}
QString JapaneseSpeech::qualityWorkerPath() {
  const auto installed = QCoreApplication::applicationDirPath() + "/" + qualityWorkerFileName();
  if (QFileInfo(installed).isExecutable())
    return installed;
#ifdef MOJI_NOOK_QUALITY_WORKER
  const auto built = QString::fromUtf8(MOJI_NOOK_QUALITY_WORKER);
  if (QFileInfo(built).isExecutable())
    return built;
#endif
  return {};
}
bool JapaneseSpeech::available(const QString &engine) const {
  const auto selected = engine.isEmpty() ? configuredEngine : engine;
  const auto dir = bundleDirectory();
  if (dir.isEmpty() || !QFileInfo::exists(dir + "/dictionary/sys.dic"))
    return false;
  if (selected == "quality") {
    for (const auto &voice : qualityVoices)
      if (!QFileInfo::exists(dir + "/quality/models/" + voice.model))
        return false;
    return !qualityWorkerPath().isEmpty() &&
        QFileInfo::exists(dir + "/" + voicevoxCoreLibrary()) &&
        QFileInfo::exists(dir + "/" + voicevoxRuntimeLibrary());
  }
  if (!QFileInfo(dir + "/bin/open_jtalk").isExecutable())
    return false;
  for (const auto &voice : voices("fast"))
    if (!QFileInfo::exists(dir + "/voices/" + voice + ".htsvoice"))
      return false;
  return true;
}
void JapaneseSpeech::configure(bool on, const QString &voice, int level,
                               double speed, const QString &engine) {
  enabled = on;
  const auto nextEngine = engine == "quality" ? QString("quality") : QString("fast");
  const auto nextVoice = voices(nextEngine).contains(voice) ? voice : QString("random");
  if (configuredVoice != nextVoice || configuredEngine != nextEngine) {
    stop();
    if (configuredEngine != nextEngine)
      closeQualityWorker();
    currentKey.clear();
    utteranceVoices.clear();
    utteranceOrder.clear();
  }
  configuredVoice = nextVoice;
  configuredEngine = nextEngine;
  volume = qBound(0, level, 100);
  rate = qBound(.75, speed, 1.25);
  if (output)
    output->setVolume(volume / 100.0);
  if (!on || volume == 0) {
    stop();
    closeQualityWorker();
  }
}
void JapaneseSpeech::prepare() {
  // The dictionary is memory mapped by the small bundled worker on demand.
  // Yield even for discovery so UI callers have a consistently asynchronous API.
  QTimer::singleShot(0, this, [this] {
    const bool found = available();
    prepared = found;
    emit readyChanged(found);
    if (!found)
      emit error("Japanese pronunciation files are missing. Reinstall Moji Nook with its speech bundle.");
  });
}
void JapaneseSpeech::say(const QString &kanaText, const QString &utteranceKey) {
  if (!enabled || volume == 0 || kanaText.trimmed().isEmpty())
    return;
  // Prefer the source's kana reading, supplied by the caller, to avoid an
  // ambiguous kanji being interpreted as the wrong word by a dictionary.
  const auto text = kanaText.normalized(QString::NormalizationForm_KC).trimmed();
  if (text.size() > 240) {
    // Track the key so the card that asked can show the error.
    currentKey = utteranceKey;
    currentText.clear();
    emit error("Pronunciation is limited to short practice words and phrases.");
    return;
  }
  const bool replaying = !utteranceKey.isEmpty() && utteranceKey == currentKey &&
                         text == currentText;
  if (!replaying) {
    const auto choices = voices(configuredEngine);
    currentVoice = configuredVoice != "random" ? configuredVoice
        : utteranceVoices.contains(utteranceKey) ? utteranceVoices[utteranceKey]
        : choices[QRandomGenerator::global()->bounded(choices.size())];
    // An independent settings audition must not change an existing card's voice.
    if (!utteranceKey.isEmpty() && !utteranceVoices.contains(utteranceKey)) {
      utteranceVoices.insert(utteranceKey, currentVoice);
      utteranceOrder.append(utteranceKey);
      while (utteranceOrder.size() > 48)
        utteranceVoices.remove(utteranceOrder.takeFirst());
    }
  }
  currentKey = utteranceKey;
  currentText = text;
  synthesize(text, currentVoice);
}
void JapaneseSpeech::replay() {
  if (enabled && volume > 0 && !currentText.isEmpty())
    say(currentText, currentKey);
}
void JapaneseSpeech::stop() {
  ++epoch;
  if (player)
    player->stop();
  if (qualityBusy)
    closeQualityWorker();
  if (process) {
    auto *canceled = process;
    process = nullptr;
    canceled->disconnect(this);
    const auto canceledPath = canceled->property("wavePath").toString();
    connect(canceled, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            canceled, [canceled, canceledPath] {
      QFile::remove(canceledPath);
      canceled->deleteLater();
    });
    canceled->kill();
    if (canceled->state() == QProcess::NotRunning) {
      QFile::remove(canceledPath);
      canceled->deleteLater();
    }
  }
}
void JapaneseSpeech::synthesize(const QString &text, const QString &voice) {
  stop();
  if (!available()) {
    prepared = false;
    emit readyChanged(false);
    emit error("Japanese pronunciation files are missing. Reinstall Moji Nook with its speech bundle.");
    return;
  }
  if (!prepared) {
    prepared = true;
    emit readyChanged(true);
  }
  const auto key = QString::fromLatin1(QCryptographicHash::hash(
      (configuredEngine + "\n" + text + "\n" + voice + "\n" + QString::number(rate, 'f', 2)).toUtf8(),
      QCryptographicHash::Sha256).toHex());
  if (generatedFiles.contains(key) && QFileInfo::exists(generatedFiles[key])) {
    lastWav = generatedFiles[key];
    const int cachedEpoch = epoch;
    emit generated(lastWav, voice);
    if (cachedEpoch == epoch)
      play(lastWav);
    return;
  }
  if (!cache.isValid()) {
    emit error("Cannot create a temporary pronunciation file.");
    return;
  }
  const auto bundle = bundleDirectory();
  const auto path = cache.path() + "/" + key + "-" + QString::number(epoch) + ".wav";
  if (configuredEngine == "quality") {
    synthesizeQuality(text, voice, key, path);
    return;
  }
  const int requestEpoch = epoch;
  auto *request = new QProcess(this);
  process = request;
  request->setProperty("wavePath", path);
  auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_LINUX
  environment.insert("LD_LIBRARY_PATH", bundle + "/lib" +
      (environment.value("LD_LIBRARY_PATH").isEmpty() ? QString()
          : ":" + environment.value("LD_LIBRARY_PATH")));
#endif
  request->setProcessEnvironment(environment);
  request->setProgram(bundle + "/bin/open_jtalk");
  request->setArguments({"-x", bundle + "/dictionary", "-m",
                        bundle + "/voices/" + voice + ".htsvoice",
                        "-r", QString::number(rate, 'f', 2), "-ow", path});
  connect(request, &QProcess::started, this, [request, text] {
    request->write((text + "\n").toUtf8());
    request->closeWriteChannel();
  });
  connect(request, &QProcess::errorOccurred, this,
          [this, request, requestEpoch, path](QProcess::ProcessError failure) {
    // FailedToStart does not emit finished; reap it explicitly. Other process
    // failures are handled once by finished to avoid duplicate notices.
    if (failure != QProcess::FailedToStart)
      return;
    QFile::remove(path);
    if (process == request)
      process = nullptr;
    request->deleteLater();
    if (requestEpoch == epoch) {
      prepared = false;
      emit readyChanged(false);
      emit error("Japanese pronunciation could not start: " + request->errorString());
    }
  });
  connect(request, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this, request, requestEpoch, path, key, voice](int code, QProcess::ExitStatus state) {
    request->deleteLater();
    if (process == request)
      process = nullptr;
    if (requestEpoch != epoch) {
      QFile::remove(path);
      return;
    }
    if (code != 0 || state != QProcess::NormalExit || QFileInfo(path).size() <= 44) {
      QFile::remove(path);
      emit error("Japanese pronunciation could not be generated.");
      return;
    }
    cacheWave(key, path);
    lastWav = path;
    emit generated(path, voice);
    if (requestEpoch == epoch)
      play(path);
  });
  QTimer::singleShot(10000, request, [this, request, requestEpoch] {
    if (requestEpoch == epoch && request->state() != QProcess::NotRunning) {
      stop();
      emit error("Japanese pronunciation took too long. Try listening again.");
    }
  });
  request->start();
}
void JapaneseSpeech::cacheWave(const QString &key, const QString &path) {
  generatedFiles.insert(key, path);
  cacheOrder.append(key);
  while (cacheOrder.size() > 48)
    QFile::remove(generatedFiles.take(cacheOrder.takeFirst()));
}
void JapaneseSpeech::closeQualityWorker() {
  qualityIdle.stop();
  qualityBusy = false;
  qualityOutput.clear();
  if (!qualityWorker)
    return;
  auto *closing = qualityWorker;
  qualityWorker = nullptr;
  closing->disconnect(this);
  const auto path = closing->property("wavePath").toString();
  connect(closing, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          closing, [closing, path] {
    if (!path.isEmpty()) QFile::remove(path);
    closing->deleteLater();
  });
  closing->kill();
  if (closing->state() == QProcess::NotRunning) {
    if (!path.isEmpty()) QFile::remove(path);
    closing->deleteLater();
  }
}
void JapaneseSpeech::synthesizeQuality(const QString &text, const QString &voice,
                                     const QString &key, const QString &path) {
  qualityIdle.stop();
  const int requestEpoch = epoch;
  qualityBusy = true;
  if (!qualityWorker) {
    auto *worker = new QProcess(this);
    worker->setObjectName("qualitySpeechWorker");
    qualityWorker = worker;
    worker->setProgram(qualityWorkerPath());
    worker->setArguments({"--bundle", bundleDirectory()});
#ifdef Q_OS_WIN
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PATH", QDir::toNativeSeparators(bundleDirectory() + "/quality/lib") + ";" +
                                   environment.value("PATH"));
    worker->setProcessEnvironment(environment);
#endif
    connect(worker, &QProcess::started, this, [this, worker] {
      if (qualityWorker == worker && qualityBusy)
        worker->write(worker->property("payload").toByteArray());
    });
    connect(worker, &QProcess::readyReadStandardOutput, this, [this, worker] {
      qualityOutput += worker->readAllStandardOutput();
      if (qualityOutput.size() > 16384) {
        closeQualityWorker();
        emit error("Quality voice returned an invalid response.");
        return;
      }
      int newline;
      while ((newline = qualityOutput.indexOf('\n')) >= 0) {
        const auto frame = QJsonDocument::fromJson(qualityOutput.left(newline)).object();
        qualityOutput.remove(0, newline + 1);
        if (!frame.contains("id") || !qualityBusy || frame["id"].toInt() != epoch)
          continue;
        const int completedEpoch = epoch;
        const auto wav = worker->property("wavePath").toString();
        const auto cacheKey = worker->property("cacheKey").toString();
        const auto speaker = worker->property("voice").toString();
        qualityBusy = false;
        worker->setProperty("wavePath", QString());
        qualityIdle.start(60000);
        if (!frame["ok"].toBool() || QFileInfo(wav).size() <= 44) {
          QFile::remove(wav);
          emit error(frame["error"].toString("Quality pronunciation could not be generated."));
          // Error handlers may mute or switch engines.
          return;
        }
        cacheWave(cacheKey, wav);
        lastWav = wav;
        emit generated(wav, speaker);
        if (completedEpoch == epoch)
          play(wav);
        return;
      }
    });
    connect(worker, &QProcess::readyReadStandardError, this, [worker] {
      // Keep backend diagnostic output bounded; the protocol carries user errors.
      worker->readAllStandardError();
    });
    connect(worker, &QProcess::errorOccurred, this, [this, worker](QProcess::ProcessError failure) {
      if (qualityWorker != worker || failure != QProcess::FailedToStart)
        return;
      closeQualityWorker();
      emit error("Quality voice could not start. Reinstall Moji Nook with its bundled voices.");
    });
    connect(worker, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, worker](int, QProcess::ExitStatus) {
      if (qualityWorker != worker)
        return;
      const bool interrupted = qualityBusy;
      closeQualityWorker();
      if (interrupted)
        emit error("Quality voice stopped before finishing. Try listening again.");
    });
  }
  auto *worker = qualityWorker;
  worker->setProperty("wavePath", path);
  worker->setProperty("cacheKey", key);
  worker->setProperty("voice", voice);
  const int style = qualityVoice(voice) ? qualityVoice(voice)->style : qualityVoices[0].style;
  const auto payload = QJsonDocument(QJsonObject{{"id", requestEpoch}, {"text", text},
      {"style", style}, {"rate", rate}, {"path", path}}).toJson(QJsonDocument::Compact) + '\n';
  worker->setProperty("payload", payload);
  QTimer::singleShot(30000, worker, [this, worker, requestEpoch] {
    if (qualityWorker == worker && qualityBusy && epoch == requestEpoch) {
      stop();
      emit error("Quality pronunciation took too long. Try listening again.");
    }
  });
  if (worker->state() == QProcess::NotRunning)
    worker->start();
  else if (worker->state() == QProcess::Running)
    worker->write(payload);
}
void JapaneseSpeech::play(const QString &path) {
  if (!enabled || volume == 0)
    return;
  if (QGuiApplication::platformName() == "offscreen")
    return;
  if (!player) {
    player = new QMediaPlayer(this);
    output = new QAudioOutput(this);
    player->setAudioOutput(output);
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
      if (state == QMediaPlayer::PlayingState)
        emit started(currentVoice);
    });
    connect(player, &QMediaPlayer::errorOccurred, this, [this] {
      emit error("Pronunciation audio output is unavailable.");
    });
  }
  output->setVolume(volume / 100.0);
  player->setSource(QUrl::fromLocalFile(path));
  player->play();
}
