#pragma once
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>
#include <QtWidgets>
#include <memory>

QByteArray feedbackTone(bool correct);
class FeedbackAudio : public QObject {
  Q_OBJECT
public:
  explicit FeedbackAudio(QObject *parent = nullptr);
  void configure(bool enabled, int volume);
  void play(bool correct);
signals:
  void unavailable();
  void playbackStarted(bool correct);

private:
  bool enabled = false;
  float volume = .2f;
  QBuffer buffer;
  std::unique_ptr<QAudioSink> sink;
  bool currentCorrect = false;
  std::unique_ptr<QMediaDevices> devices;
  QByteArray correctPcm, incorrectPcm;
  bool preparationQueued = false;
  void schedulePreparation();
  void prepare();
};

class CardAccent : public QWidget {
public:
  explicit CardAccent(QWidget *parent);
  void pulse(const QColor &color, int duration = 260);
  void stop();

protected:
  void paintEvent(QPaintEvent *) override;
  bool eventFilter(QObject *object, QEvent *event) override;

private:
  QVariantAnimation animation;
  QColor color;
  double progress = 1;
};
