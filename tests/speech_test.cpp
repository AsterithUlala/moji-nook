#include "speech.h"
#include <QtTest>

class SpeechTest : public QObject {
  Q_OBJECT
private slots:
  void shippedJapaneseVoices_data() {
    QTest::addColumn<QString>("voice");
    for (const auto &voice : JapaneseSpeech::voices())
      QTest::newRow(qPrintable(voice)) << voice;
  }
  void shippedJapaneseVoices() {
    QFETCH(QString, voice);
    JapaneseSpeech speech;
    if (!speech.available())
      QSKIP("This platform/build has no bundled Japanese speech assets.");
    QSignalSpy readiness(&speech, &JapaneseSpeech::readyChanged);
    QSignalSpy generated(&speech, &JapaneseSpeech::generated);
    QSignalSpy errors(&speech, &JapaneseSpeech::error);
    speech.configure(true, voice, 35, 1.0);
    speech.prepare();
    QTRY_COMPARE(readiness.count(), 1);
    QVERIFY(speech.ready());
    QElapsedTimer timing;
    timing.start();
    speech.say("がっこう", "kanji-学校-reading");
    // Generation must return to the event loop without blocking on audio.
    QVERIFY(timing.elapsed() < 100);
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 1, 10000);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(speech.lastVoice(), voice);
    QFile wav(generated.first()[0].toString());
    QVERIFY(wav.open(QIODevice::ReadOnly));
    const auto data = wav.readAll();
    QCOMPARE(data.left(4), QByteArray("RIFF"));
    QCOMPARE(data.mid(8, 4), QByteArray("WAVE"));
    QVERIFY(data.size() > 10000);
    QVERIFY(data.mid(44).count('\0') < data.size() - 44);
    const auto firstPath = generated.first()[0].toString();
    speech.replay();
    QCOMPARE(generated.count(), 2);
    QCOMPARE(generated.last()[0].toString(), firstPath);
    QCOMPARE(generated.last()[1].toString(), voice);
  }
  void randomVoiceReplayAndDisabledPlayback() {
    JapaneseSpeech speech;
    if (!speech.available())
      QSKIP("This platform/build has no bundled Japanese speech assets.");
    QSignalSpy generated(&speech, &JapaneseSpeech::generated);
    speech.configure(true, "random", 35, 1.0);
    speech.say("にほん", "first-word");
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 1, 10000);
    QVERIFY(JapaneseSpeech::voices().contains(speech.lastVoice()));
    const auto originalVoice = speech.lastVoice();
    speech.replay();
    QCOMPARE(speech.lastVoice(), originalVoice);
    QCOMPARE(generated.count(), 2);
    speech.say("ねこ", "settings-audition");
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 3, 10000);
    speech.say("にほん", "first-word");
    QCOMPARE(generated.count(), 4);
    QCOMPARE(speech.lastVoice(), originalVoice);
    QCOMPARE(generated.last()[0].toString(), generated.first()[0].toString());
    speech.configure(false, "random", 35, 1.0);
    speech.say("ねこ", "second-word");
    speech.replay();
    QTest::qWait(150);
    QCOMPARE(generated.count(), 4);
  }
  void canceledWorkCannotPlayOnNextCard() {
    JapaneseSpeech speech;
    if (!speech.available())
      QSKIP("This platform/build has no bundled Japanese speech assets.");
    speech.configure(true, "mei", 35, 1.0);
    QSignalSpy generated(&speech, &JapaneseSpeech::generated);
    for (int i = 0; i < 16; ++i) {
      speech.say("おはようございます", "old-card");
      speech.stop();
    }
    speech.say("ねこ", "new-card");
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 1, 10000);
    QTest::qWait(150);
    QCOMPARE(generated.count(), 1);
    const auto cacheDirectory = QFileInfo(generated.first()[0].toString()).absolutePath();
    QCOMPARE(QDir(cacheDirectory).entryList({"*.wav"}, QDir::Files).size(), 1);
  }
  void speedChangesSynthesisAndCache() {
    JapaneseSpeech speech;
    if (!speech.available())
      QSKIP("This platform/build has no bundled Japanese speech assets.");
    QSignalSpy generated(&speech, &JapaneseSpeech::generated);
    speech.configure(true, "takumi", 35, .75);
    speech.say("おはようございます", "slow");
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 1, 10000);
    const auto slowPath = generated.last()[0].toString();
    const auto slowSize = QFileInfo(slowPath).size();
    speech.configure(true, "takumi", 35, 1.25);
    speech.say("おはようございます", "fast");
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 2, 10000);
    const auto fastPath = generated.last()[0].toString();
    QVERIFY(slowPath != fastPath);
    QVERIFY(slowSize > QFileInfo(fastPath).size());
  }
  void qualityVoicesWarmWorkerAndReplay() {
    JapaneseSpeech speech;
    if (!speech.available("quality"))
      QSKIP("This build has no bundled Quality speech assets.");
    QSignalSpy generated(&speech, &JapaneseSpeech::generated);
    QSignalSpy errors(&speech, &JapaneseSpeech::error);
    QSet<QByteArray> waveHashes;
    qint64 pid = 0;
    int count = 0;
    for (const auto &voice : JapaneseSpeech::voices("quality")) {
      speech.configure(true, voice, 35, 1.0, "quality");
      QElapsedTimer start;
      start.start();
      speech.say("にほんご", voice);
      QVERIFY(start.elapsed() < 100); // Neural inference must stay off the UI thread.
      ++count;
      QTRY_COMPARE_WITH_TIMEOUT(generated.count(), count, 30000);
      QCOMPARE(errors.count(), 0);
      QCOMPARE(generated.last()[1].toString(), voice);
      QFile file(generated.last()[0].toString());
      QVERIFY(file.open(QIODevice::ReadOnly));
      const auto wav = file.readAll();
      QCOMPARE(wav.left(4), QByteArray("RIFF"));
      QVERIFY(wav.size() > 10000);
      waveHashes.insert(QCryptographicHash::hash(wav, QCryptographicHash::Sha256));
      auto *worker = speech.findChild<QProcess *>("qualitySpeechWorker");
      QVERIFY(worker && worker->state() == QProcess::Running);
      if (!pid) pid = worker->processId();
      QCOMPARE(worker->processId(), pid); // Speaker changes reuse a warm idle worker.
      const auto originalPath = generated.last()[0].toString();
      speech.replay();
      ++count;
      QCOMPARE(generated.count(), count);
      QCOMPARE(generated.last()[0].toString(), originalPath);
    }
    QCOMPARE(waveHashes.size(), JapaneseSpeech::voices("quality").size());
    speech.configure(true, "kiritan", 35, .75, "quality");
    speech.say("おはようございます", "slow");
    ++count;
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), count, 30000);
    const auto slow = QFileInfo(generated.last()[0].toString()).size();
    speech.configure(true, "kiritan", 35, 1.25, "quality");
    speech.say("おはようございます", "fast");
    ++count;
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), count, 30000);
    QVERIFY(slow > QFileInfo(generated.last()[0].toString()).size());
    speech.configure(false, "kiritan", 35, 1., "quality");
    QTRY_VERIFY(!speech.findChild<QProcess *>("qualitySpeechWorker"));
    speech.replay();
    QCOMPARE(generated.count(), count);
  }
  void qualityCancellationAndEngineChange() {
    JapaneseSpeech speech;
    if (!speech.available("quality"))
      QSKIP("This build has no bundled Quality speech assets.");
    QSignalSpy generated(&speech, &JapaneseSpeech::generated);
    QSignalSpy errors(&speech, &JapaneseSpeech::error);
    speech.configure(true, "itako", 35, 1., "quality");
    speech.say("おはようございます", "old-neural-card");
    QTest::qWait(100); // Cancel during real model startup/inference.
    speech.stop();
    // Windows bundles only Quality; cancel into another neural speaker there.
    const bool fast = speech.available("fast");
    speech.configure(true, fast ? "mei" : "kiritan", 35, 1., fast ? "fast" : "quality");
    speech.say("ねこ", "new-card");
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 1, 30000);
    QTest::qWait(1500);
    QCOMPARE(generated.count(), 1);
    QCOMPARE(errors.count(), 0);
    const auto cache = QFileInfo(generated.last()[0].toString()).absolutePath();
    QCOMPARE(QDir(cache).entryList({"*.wav"}, QDir::Files).size(), 1);
    // Replay after changing engine must use the new engine's speaker bank.
    speech.configure(true, "takehiro", 35, 1., "quality");
    speech.replay();
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 2, 30000);
    QCOMPARE(generated.last()[1].toString(), QString("takehiro"));
    QCOMPARE(errors.count(), 0);
  }
};
QTEST_MAIN(SpeechTest)
#include "speech_test.moc"
