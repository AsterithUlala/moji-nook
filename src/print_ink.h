#pragma once
#include "theme.h"
#include <QtWidgets>
#include <cmath>

// Decorative only: no timers, input handlers, external files, or network access.
// The shared art is decoded once; day/night compositing is cached per process.
inline const QPixmap &printInkArt(bool night, bool neutral = false) {
  static const QPixmap day(QStringLiteral(":/print-ink/landscape.png"));
  static const QPixmap evening = [] {
    QPixmap image = day;
    if (!image.isNull()) {
      QPainter p(&image);
      p.setCompositionMode(QPainter::CompositionMode_Multiply);
      p.fillRect(image.rect(), QColor("#8c9b85"));
    }
    return image;
  }();
  static const QPixmap neutralDay = QPixmap::fromImage(day.toImage().convertToFormat(QImage::Format_Grayscale8));
  static const QPixmap neutralNight = QPixmap::fromImage(evening.toImage().convertToFormat(QImage::Format_Grayscale8));
  return neutral ? (night ? neutralNight : neutralDay) : (night ? evening : day);
}

// Quantize a high precision wash with shared-channel, half-LSB static noise.
// Dithering happens at physical resolution, so HiDPI scaling cannot blur it away.
inline QImage printInkWash(QSize size, QColor base, bool night) {
  if (size.isEmpty()) return {};
  const QColor top = base.lighter(night ? 125 : 103);
  const QColor bottom = base.darker(night ? 112 : 108);
  QImage image(size, QImage::Format_RGB32);
  const double denominator = double(size.width()) * size.width() +
                             double(size.height()) * size.height();
  for (int y = 0; y < size.height(); ++y) {
    auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = 0; x < size.width(); ++x) {
      const double position = ((x + .5) * size.width() +
                               (y + .5) * size.height()) / denominator;
      const bool first = position < .55;
      const double amount = first ? position / .55 : (position - .55) / .45;
      const QColor &a = first ? top : base, &b = first ? base : bottom;
      quint32 hash = quint32(x) * 0x8da6b343u ^ quint32(y) * 0xd8163841u;
      hash ^= hash >> 16; hash *= 0x7feb352du;
      hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
      const double noise = (double(hash & 0xffffu) + .5) / 65536.0 - .5;
      const auto channel = [&](int from, int to) {
        return qBound(0, int(std::floor(from + (to - from) * amount + noise + .5)), 255);
      };
      row[x] = qRgb(channel(a.red(), b.red()), channel(a.green(), b.green()),
                    channel(a.blue(), b.blue()));
    }
  }
  return image;
}

class PrintInkSurface : public QFrame {
public:
  enum Kind { Paper, Session, Word };
  explicit PrintInkSurface(Kind kind, QWidget *parent = nullptr)
      : QFrame(parent), kind_(kind) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
  }
protected:
  void paintEvent(QPaintEvent *) override {
    const auto selected = property("printTheme").isValid() ? property("printTheme")
                                                           : qApp->property("mojiNookTheme");
    const int theme = selected.isValid() ? selected.toInt() : defaultTheme;
    const auto c = colors(theme);
    const bool night = theme % 2;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF bounds = rect();
    const qreal radius = kind_ == Paper ? 0 : kind_ == Word ? 12 : 14;
    QPainterPath shape;
    shape.addRoundedRect(bounds, radius, radius);
    p.setClipPath(shape);
    const QColor base(kind_ == Word || kind_ == Session ? c.poster : c.background);
    const qreal ratio = devicePixelRatioF();
    const QSize physical(qCeil(width() * ratio), qCeil(height() * ratio));
    if (wash_.isNull() || wash_.size() != physical || washTheme_ != theme ||
        wash_.devicePixelRatio() != ratio) {
      wash_ = QPixmap::fromImage(printInkWash(physical, base, night));
      wash_.setDevicePixelRatio(ratio);
      washTheme_ = theme;
    }
    p.drawPixmap(QPointF(0, 0), wash_);
    const auto &art = printInkArt(night, theme / 2 == 1);
    if (art.isNull()) return;
    p.setOpacity(.065);
    static const QPixmap dayGrain = printInkArt(false).copy(60, 40, 256, 256);
    static const QPixmap nightGrain = printInkArt(true).copy(60, 40, 256, 256);
    static const QPixmap grayDayGrain = printInkArt(false, true).copy(60, 40, 256, 256);
    static const QPixmap grayNightGrain = printInkArt(true, true).copy(60, 40, 256, 256);
    const QPixmap &grain = theme / 2 == 1 ? (night ? grayNightGrain : grayDayGrain)
                                          : (night ? nightGrain : dayGrain);
    p.drawTiledPixmap(rect(), grain);
    p.setOpacity(1);
    if (kind_ != Paper) {
      // Print impressions inhabit useful surfaces, never reserve a blank section.
      p.setOpacity(kind_ == Session ? .15 : .105);
      const qreal scale = qMax(bounds.width() / art.width(), bounds.height() / art.height());
      const QSizeF size(art.width() * scale, art.height() * scale);
      p.drawPixmap(QRectF((width() - size.width()) / 2,
                         (height() - size.height()) * .65, size.width(), size.height()),
                   art, art.rect());
      p.setOpacity(1);
    }
    const QString subjectKind = property("subjectKind").toString();
    const bool subjectTint = kind_ == Word &&
        (subjectKind == "kanji" || subjectKind == "vocabulary" ||
         subjectKind == "kana_vocabulary");
    const auto sideGradient = [&](int alpha) {
      QColor tint = wanikaniSubjectHue(subjectKind, theme);
      tint.setAlpha(alpha);
      QColor clear = tint;
      clear.setAlpha(0);
      QLinearGradient gradient(bounds.topLeft(), bounds.topRight());
      gradient.setColorAt(0, tint);
      gradient.setColorAt(.35, clear);
      gradient.setColorAt(.65, clear);
      gradient.setColorAt(1, tint);
      return gradient;
    };
    if (subjectTint) {
      // A restrained edge wash fades out before reaching the word's center.
      p.fillRect(bounds, sideGradient(40));
    }
    QColor edge(c.border);
    edge.setAlpha(85);
    if (kind_ != Paper) {
      p.setClipping(false);
      p.setPen(QPen(edge, 1));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(bounds.adjusted(.5, .5, -.5, -.5), radius, radius);
      if (subjectTint) {
        p.setPen(QPen(QBrush(sideGradient(168)), 1));
        p.drawRoundedRect(bounds.adjusted(.5, .5, -.5, -.5), radius, radius);
      }
    }
  }
private:
  Kind kind_;
  QPixmap wash_;
  int washTheme_ = -1;
};
