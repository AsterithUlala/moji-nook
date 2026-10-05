#pragma once
#include <QtWidgets>
// Keep the mark as an icon so painting follows the current screen's density.
class BrandMarkLabel : public QLabel {
public:
  using QLabel::QLabel;
  void setMark(const QIcon &value) { mark = value; update(); }

protected:
  void paintEvent(QPaintEvent *event) override {
    QLabel::paintEvent(event);
    QPainter painter(this);
    mark.paint(&painter, contentsRect(), Qt::AlignCenter);
  }

private:
  QIcon mark;
};

// Keep an explicit check mark: stylesheet borders suppress the native glyph.
class SettingsCheckBox : public QCheckBox {
public:
  using QCheckBox::QCheckBox;

protected:
  void paintEvent(QPaintEvent *event) override {
    QCheckBox::paintEvent(event);
    if (!isChecked())
      return;
    QStyleOptionButton option;
    initStyleOption(&option);
    const auto box =
        style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::WindowText), 2.2,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    path.moveTo(box.left() + box.width() * .22, box.top() + box.height() * .5);
    path.lineTo(box.left() + box.width() * .43, box.top() + box.height() * .72);
    path.lineTo(box.left() + box.width() * .78, box.top() + box.height() * .25);
    painter.drawPath(path);
  }
};
inline QLabel *label(const QString &text, const QString &name = {}) {
  auto *l = new QLabel(text);
  l->setTextFormat(Qt::PlainText);
  l->setWordWrap(true);
  l->setObjectName(name);
  return l;
}
inline QPushButton *button(const QString &text, const QString &name = {}) {
  auto *b = new QPushButton(text);
  b->setObjectName(name);
  b->setProperty("role", name);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}

// Keep source status text, symbols, accessible descriptions, and colors together.
inline void setSourceStatus(QLabel *status, const QString &state,
                            const QString &message) {
  if (!status) return;
  const QString icon = state == "connected" ? "✓"
                       : state == "connecting" ? "…"
                       : state == "error" ? "×" : "";
  status->setProperty("status", state);
  QStringList clauses = message.split(" · ");
  for (auto &clause : clauses)
    if (!clause.isEmpty()) clause[0] = clause[0].toUpper();
  const auto sentence = clauses.join(" · ");
  status->setText((icon.isEmpty() ? QString() : icon + " ") + sentence);
  status->setAccessibleDescription(sentence);
  status->style()->unpolish(status);
  status->style()->polish(status);
}

// Match feedback to the visible glyphs, not the font's invisible descent space.
// Japanese and Latin fonts have different ink centers inside a centered line box.
class FeedbackIconLabel : public QLabel {
public:
  explicit FeedbackIconLabel(QLabel *text, QWidget *parent = nullptr)
      : QLabel(parent), text_(text) {
    setFixedWidth(28);
    setMinimumHeight(28);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  }
  QSize sizeHint() const override {
    return {28, qMax(28, text_ ? text_->fontMetrics().height() : 28)};
  }
protected:
  void paintEvent(QPaintEvent *) override {
    const auto icon = pixmap();
    if (icon.isNull()) return;
    qreal center = height() / 2.0;
    if (text_ && !text_->text().isEmpty()) {
      const QFontMetricsF metrics(text_->font());
      // Multiline text keeps the icon centered on the whole answer block.
      if (!text_->text().contains('\n') &&
          metrics.horizontalAdvance(text_->text()) <= text_->contentsRect().width()) {
        const auto bounds = text_->contentsRect();
        const qreal baseline = bounds.top() + (bounds.height() - metrics.height()) / 2.0 + metrics.ascent();
        const qreal inkCenter = baseline + metrics.tightBoundingRect(text_->text()).center().y();
        center = mapFromGlobal(text_->mapToGlobal(QPoint(0, 0))).y() + inkCenter;
      }
    }
    const QSizeF size = icon.deviceIndependentSize();
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(QRectF((width()-size.width())/2.0, center-size.height()/2.0,
                              size.width(), size.height()), icon, icon.rect());
  }
private:
  QPointer<QLabel> text_;
};
