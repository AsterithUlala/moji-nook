#include "feedback.h"
#include "performance.h"
#include <QAudioDevice>
#include <QMediaDevices>
#include <cmath>

QByteArray feedbackTone(bool correct) {
  constexpr int rate = 24000, samples = 7200;
  QByteArray wav;
  QDataStream out(&wav, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::LittleEndian);
  out.writeRawData("RIFF", 4);
  out << quint32(36 + samples * 2);
  out.writeRawData("WAVEfmt ", 8);
  out << quint32(16) << quint16(1) << quint16(1);
  out << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
  out.writeRawData("data", 4);
  out << quint32(samples * 2);
  constexpr double pi = 3.141592653589793;
  for (int i = 0; i < samples; ++i) {
    const double t = double(i) / rate;
    double value = 0;
    for (int note = 0; note < 2; ++note) {
      const double age = t - note * .095;
      if (age < 0 || age > .205)
        continue;
      const double frequency =
          correct ? (note ? 783.99 : 587.33) : (note ? 392.0 : 440.0);
      const double envelope = std::sin(pi * age / .205) * std::exp(-age * 12);
      value += .35 * envelope * std::sin(2 * pi * frequency * age);
    }
    out << qint16(qBound(-1.0, value, 1.0) * 32767);
  }
  return wav;
}
static QByteArray makeFeedbackPcm(bool correct, const QAudioFormat &format) {
  QByteArray pcm;
  QDataStream out(&pcm, QIODevice::WriteOnly);
  out.setByteOrder(QSysInfo::ByteOrder == QSysInfo::LittleEndian
                       ? QDataStream::LittleEndian
                       : QDataStream::BigEndian);
  out.setFloatingPointPrecision(QDataStream::SinglePrecision);
  constexpr double pi = 3.141592653589793;
  for (int i = 0; i < format.sampleRate() * .3; ++i) {
    double value = 0;
    for (int note = 0; note < 2; ++note) {
      const double age = double(i) / format.sampleRate() - note * .095;
      if (age < 0 || age > .205)
        continue;
      const double frequency =
          correct ? (note ? 783.99 : 587.33) : (note ? 392.0 : 440.0);
      value += .35 * std::sin(pi * age / .205) * std::exp(-age * 12) *
               std::sin(2 * pi * frequency * age);
    }
    value = qBound(-1.0, value, 1.0);
    for (int channel = 0; channel < format.channelCount(); ++channel) {
      switch (format.sampleFormat()) {
      case QAudioFormat::UInt8:
        out << quint8((value + 1) * 127.5);
        break;
      case QAudioFormat::Int16:
        out << qint16(value * 32767);
        break;
      case QAudioFormat::Int32:
        out << qint32(value * 2147483647);
        break;
      case QAudioFormat::Float:
        out << float(value);
        break;
      default:
        return {};
      }
    }
  }
  return pcm;
}

FeedbackAudio::FeedbackAudio(QObject *parent) : QObject(parent) {}
void FeedbackAudio::schedulePreparation() {
  if (!enabled || volume == 0 || preparationQueued || sink ||
      QGuiApplication::platformName() == "offscreen")
    return;
  preparationQueued = true;
  QTimer::singleShot(0, this, [this] {
    preparationQueued = false;
    if (enabled && volume > 0)
      prepare();
  });
}
void FeedbackAudio::prepare() {
  PerformanceSpan timing("prepare-audio");
  if (!devices) {
    devices = std::make_unique<QMediaDevices>();
    connect(devices.get(), &QMediaDevices::audioOutputsChanged, this, [this] {
      if (sink) {
        sink->disconnect(this);
        sink->stop();
        sink.reset();
      }
      schedulePreparation();
    });
  }
  const auto device = QMediaDevices::defaultAudioOutput();
  const auto format = device.preferredFormat();
  if (device.isNull() || !format.isValid() ||
      !device.isFormatSupported(format)) {
    emit unavailable();
    return;
  }
  correctPcm = makeFeedbackPcm(true, format);
  incorrectPcm = makeFeedbackPcm(false, format);
  if (correctPcm.isEmpty() || incorrectPcm.isEmpty()) {
    emit unavailable();
    return;
  }
  sink = std::make_unique<QAudioSink>(device, format);
  connect(
      sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::ActiveState)
          emit playbackStarted(currentCorrect);
        if (state == QAudio::IdleState)
          sink->stop();
        if (state == QAudio::StoppedState && sink->error() != QAudio::NoError)
          emit unavailable();
      });
  sink->setVolume(volume);
}
void FeedbackAudio::configure(bool on, int level) {
  enabled = on;
  volume = qBound(0, level, 100) / 100.f;
  if (sink) {
    sink->setVolume(volume);
    if (!enabled || volume == 0)
      sink->stop();
  }
  schedulePreparation();
}
void FeedbackAudio::play(bool correct) {
  PerformanceSpan timing("play-feedback");
  if (!enabled || volume == 0 || QGuiApplication::platformName() == "offscreen")
    return;
  // A missing/reconnecting device may skip a tone, never block an answer.
  if (!sink) {
    schedulePreparation();
    return;
  }
  sink->stop();
  buffer.close();
  buffer.setData(correct ? correctPcm : incorrectPcm);
  buffer.open(QIODevice::ReadOnly);
  currentCorrect = correct;
  sink->start(&buffer);
}
CardAccent::CardAccent(QWidget *parent) : QWidget(parent) {
  setObjectName("cardAccent");
  parent->installEventFilter(this);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);
  setStyleSheet("background:transparent;");
  animation.setStartValue(0.0);
  animation.setEndValue(1.0);
  connect(&animation, &QVariantAnimation::valueChanged, this,
          [this](const QVariant &value) {
            progress = value.toDouble();
            update();
          });
  connect(&animation, &QVariantAnimation::finished, this, &QWidget::hide);
  hide();
}
void CardAccent::pulse(const QColor &value, int duration) {
  animation.stop();
  color = value;
  progress = 0;
  setGeometry(parentWidget()->rect());
  show();
  raise();
  animation.setDuration(duration);
  animation.start();
}
void CardAccent::stop() {
  animation.stop();
  hide();
}
bool CardAccent::eventFilter(QObject *object, QEvent *event) {
  if (object == parentWidget()) {
    if (event->type() == QEvent::Resize)
      setGeometry(parentWidget()->rect());
    else if (event->type() == QEvent::Hide)
      stop();
  }
  return QWidget::eventFilter(object, event);
}
void CardAccent::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QColor ink = color;
  ink.setAlphaF((1 - progress) * .65);
  painter.setPen(QPen(ink, 2));
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 10, 10);
  painter.setPen(QPen(ink, 3, Qt::SolidLine, Qt::RoundCap));
  const double stroke = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(progress);
  const double length = (width() - 32) * (.3 + .7 * stroke);
  painter.drawLine(QPointF((width() - length) / 2, 5),
                   QPointF((width() + length) / 2, 5));
}
