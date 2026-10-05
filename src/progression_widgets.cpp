#include "progression_widgets.h"
#include "print_ink.h"
#include <algorithm>
#include <limits>

namespace {
QColor mix(const QString &a, const QString &b, qreal amount) {
  const QColor x(a), y(b);
  return QColor::fromRgbF(x.redF() * (1 - amount) + y.redF() * amount,
                         x.greenF() * (1 - amount) + y.greenF() * amount,
                         x.blueF() * (1 - amount) + y.blueF() * amount);
}
QColor mix(const QColor &x, const QColor &y, qreal amount) {
  return QColor::fromRgbF(x.redF() * (1 - amount) + y.redF() * amount,
                         x.greenF() * (1 - amount) + y.greenF() * amount,
                         x.blueF() * (1 - amount) + y.blueF() * amount);
}
QFont japanese(int pixels) {
  QFont font(QStringLiteral("Noto Sans CJK JP"));
  font.setPixelSize(pixels);
  font.setWeight(QFont::Medium);
  return font;
}
QFont small(int pixels) {
  QFont font(QStringLiteral("Noto Sans"));
  font.setPixelSize(pixels);
  return font;
}
QString stage(const ProgressTile &tile) {
  if (tile.settled) return QStringLiteral("Settled");
  if (!tile.eligible) return QStringLiteral("Practiced");
  if (tile.phase == TilePhase::Growing) return QStringLiteral("Taking shape");
  return QStringLiteral("Started");
}
QString sourceName(const ProgressTile &tile) {
  if (tile.source == "wanikani") return QStringLiteral("WaniKani");
  if (tile.source == "anki") return QStringLiteral("Anki");
  return tile.source;
}
QString subjectName(const ProgressTile &tile) {
  if (tile.source != "wanikani") return {};
  if (tile.subjectKind == "kanji") return QStringLiteral("Kanji");
  if (tile.subjectKind == "vocabulary" || tile.subjectKind == "kana_vocabulary")
    return QStringLiteral("Vocab");
  return {};
}
}

WordTileCanvas::WordTileCanvas(QWidget *parent) : QWidget(parent) {
  setObjectName("wordTileCanvas");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAccessibleName(tr("Japanese word tile composition"));
  installWordTileAccessibility();
  settlement_.setParent(this);
  settlement_.setObjectName("settleMotion");
  settlement_.setStartValue(0.0);
  settlement_.setEndValue(1.0);
  settlement_.setDuration(420);
  settlement_.setEasingCurve(QEasingCurve::OutCubic);
  connect(&settlement_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
    settlementProgress_ = value.toReal();
    update();
  });
  connect(&settlement_, &QVariantAnimation::finished, this, &WordTileCanvas::stopSettlement);
}
void WordTileCanvas::setTiles(const QVector<ProgressTile> &tiles) {
  stopSettlement();
  const std::optional<int> selected = focused_ >= 0 && focused_ < tiles_.size()
      ? std::optional<int>(tiles_[focused_].id) : std::nullopt;
  const std::optional<int> expanded = expanded_ >= 0 && expanded_ < tiles_.size()
      ? std::optional<int>(tiles_[expanded_].id) : std::nullopt;
  tiles_ = tiles;
  focused_ = tiles.isEmpty() ? -1 : qBound(0, focused_, int(tiles.size()) - 1);
  expanded_ = -1;
  if (selected)
    for (int i = 0; i < tiles_.size(); ++i)
      if (tiles_[i].id == *selected) { focused_ = i; break; }
  if (expanded && !compact_)
    for (int i = 0; i < tiles_.size(); ++i)
      if (tiles_[i].id == *expanded) { expanded_ = i; break; }
  setAccessibleDescription(tr("%1 word tiles. Hover a tile for reading, meaning and progress.").arg(tiles.size()));
  refreshGeometry();
  if (inspected_) emit tileFocused(tileDescription(focused_));
  QAccessibleEvent changed(this, QAccessible::ObjectReorder);
  QAccessible::updateAccessibility(&changed);
}
void WordTileCanvas::animateSettled(const QSet<int> &ids) {
  stopSettlement();
  if (!isVisible()) return;
  for (const auto &tile : tiles_)
    if (tile.settled && ids.contains(tile.id)) settling_.insert(tile.id);
  if (settling_.isEmpty()) return;
  settlementProgress_ = 0;
  settlement_.start();
}
void WordTileCanvas::stopSettlement() {
  settlement_.stop();
  settlementProgress_ = 1;
  settling_.clear();
  update();
}
void WordTileCanvas::hideEvent(QHideEvent *event) {
  stopSettlement();
  QWidget::hideEvent(event);
}
void WordTileCanvas::setTheme(int theme) { theme_ = qBound(0, theme, themeCount - 1); update(); }
void WordTileCanvas::setCompact(bool compact) {
  if (compact_ == compact) return;
  compact_ = compact;
  if (compact_) expanded_ = -1;
  setFocusPolicy(compact ? Qt::NoFocus : Qt::StrongFocus);
  refreshGeometry();
}
void WordTileCanvas::setHighlightedIds(const QSet<int> &ids) { highlighted_ = ids; update(); }
QVector<WordTileCanvas::Placement> WordTileCanvas::placements(int width, int *height) const {
  QVector<Placement> result;
  const int available = qMax(1, width);
  const int gap = compact_ ? 8 : 12;
  const int count = compact_ ? qMin(6, int(tiles_.size())) : int(tiles_.size());
  const QFontMetricsF metrics(japanese(compact_ ? 26 : 32));
  qreal x = 0, y = 0, rowHeight = 0;
  for (int i = 0; i < count; ++i) {
    int tileWidth = compact_ ? (available >= 248 ? (available - gap) / 2 : available)
                            : qBound(144, int(metrics.horizontalAdvance(tiles_[i].characters)) + 40, 300);
    if (compact_ && (count == 1 || metrics.horizontalAdvance(tiles_[i].characters) > tileWidth - 28))
      tileWidth = available;
    tileWidth = qMin(tileWidth, available);
    const int tileHeight = compact_ ? 88 : i == expanded_ ? 240 : 130;
    if (x > 0 && x + tileWidth > available) {
      x = 0; y += rowHeight + gap; rowHeight = 0;
    }
    result.append({QRectF(x, y, tileWidth, tileHeight), i});
    rowHeight = qMax(rowHeight, qreal(tileHeight));
    x += tileWidth + gap;
  }
  if (!compact_ && expanded_ >= 0 && expanded_ < count) {
    // Choose rows at collapsed widths so expansion cannot move the selected
    // word (or its neighbors) to another row. Only redistribute its own row.
    const qreal rowTop = result[expanded_].rect.top();
    int first = expanded_, last = expanded_;
    while (first > 0 && result[first - 1].rect.top() == rowTop) --first;
    while (last + 1 < count && result[last + 1].rect.top() == rowTop) ++last;
    const int spare = available - int(result[last].rect.right());
    const QFontMetricsF minimumWordMetrics(japanese(26));
    const auto shrinkRoom = [&](int index) {
      const int minimumWidth = qMax(120, qCeil(minimumWordMetrics.horizontalAdvance(tiles_[index].characters)) + 28);
      return qMax(0, int(result[index].rect.width()) - minimumWidth);
    };
    int capacity = 0;
    for (int i = first; i <= last; ++i)
      if (i != expanded_) capacity += shrinkRoom(i);
    const int collapsedWidth = int(result[expanded_].rect.width());
    const int expandedWidth = qMin(340, collapsedWidth + spare + capacity);
    int remainingShrink = qMax(0, expandedWidth - collapsedWidth - spare);
    int rowX = 0;
    for (int i = first; i <= last; ++i) {
      int tileWidth = int(result[i].rect.width());
      if (i == expanded_) {
        tileWidth = expandedWidth;
      } else {
        const int room = shrinkRoom(i);
        const int shrink = capacity ? qRound(qreal(remainingShrink) * room / capacity) : 0;
        tileWidth -= shrink;
        remainingShrink -= shrink;
        capacity -= room;
      }
      result[i].rect.setLeft(rowX);
      result[i].rect.setWidth(tileWidth);
      rowX += tileWidth + gap;
    }
  }
  if (height) *height = count ? int(y + rowHeight) : 0;
  return result;
}
int WordTileCanvas::heightForWidth(int width) const { int h; placements(width, &h); return h; }
QSize WordTileCanvas::sizeHint() const { const int w = compact_ ? 320 : 640; return QSize(w, heightForWidth(w)); }
QSize WordTileCanvas::minimumSizeHint() const { return QSize(120, 0); }
void WordTileCanvas::refreshGeometry() {
  int height;
  layout_ = placements(width(), &height);
  setMinimumHeight(height);
  updateGeometry();
  update();
}
void WordTileCanvas::resizeEvent(QResizeEvent *event) { QWidget::resizeEvent(event); refreshGeometry(); }
void WordTileCanvas::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  p.setClipRegion(event->region());
  const auto c = colors(theme_);
  const int top = event->rect().top(), bottom = event->rect().bottom();
  const int maxTileHeight = compact_ ? 88 : 240;
  auto first = std::lower_bound(layout_.cbegin(), layout_.cend(), top - maxTileHeight,
      [](const Placement &place, int y) { return place.rect.top() < y; });
  for (auto it = first; it != layout_.cend() && it->rect.top() <= bottom; ++it) {
    if (it->rect.bottom() < top) continue;
    const auto &tile = tiles_[it->index];
    const QRectF r = it->rect;
    const bool settled = tile.settled;
    const bool growing = tile.phase == TilePhase::Growing;
    const bool expanded = !compact_ && it->index == expanded_;
    const bool highlight = highlighted_.contains(tile.id);
    const QString subject = subjectName(tile);
    const auto subjectGradient = [&](int alpha) {
      QColor tint = wanikaniSubjectHue(tile.subjectKind, theme_);
      tint.setAlpha(alpha);
      QColor clear = tint;
      clear.setAlpha(0);
      QLinearGradient gradient(r.topRight(), r.bottomLeft());
      gradient.setColorAt(0, tint);
      gradient.setColorAt(.33, clear);
      gradient.setColorAt(1, clear);
      return gradient;
    };
    const QColor tint = mix(c.surface, c.accent,
                           settled ? .28 : growing ? .18 : .045);
    p.setPen(Qt::NoPen);
    QLinearGradient inkWash(r.topLeft(), r.bottomRight());
    inkWash.setColorAt(0, tint.lighter(104));
    inkWash.setColorAt(1, tint.darker(103));
    p.setBrush(inkWash);
    p.drawRoundedRect(r.adjusted(0, 0, 0, -1), 6, 6);
    if (!subject.isEmpty()) {
      p.setBrush(subjectGradient(40));
      p.drawRoundedRect(r.adjusted(0, 0, 0, -1), 6, 6);
    }
    const QColor outline = mix(c.border, c.accent,
                               settled ? .82 : growing ? .68 : .28);
    QLinearGradient goldEdge(r.topLeft(), r.bottomRight());
    goldEdge.setColorAt(0, theme_ % 2 ? QColor("#eddaa0") : QColor("#886220"));
    goldEdge.setColorAt(.5, theme_ % 2 ? QColor("#b89140") : QColor("#b88632"));
    goldEdge.setColorAt(1, theme_ % 2 ? QColor("#e2c67b") : QColor("#795719"));
    QPen outlinePen(settled ? QBrush(goldEdge) : QBrush(outline), settled ? 1.8 : growing ? 1.5 : 1.2,
                    settled || growing ? Qt::SolidLine : Qt::DashLine);
    p.setPen(outlinePen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(.7, .7, -.7, -1.7), 6, 6);
    if (!subject.isEmpty()) {
      auto subjectEdge = outlinePen;
      subjectEdge.setBrush(subjectGradient(168));
      p.setPen(subjectEdge);
      p.drawRoundedRect(r.adjusted(.7, .7, -.7, -1.7), 6, 6);
    }
    p.setPen(Qt::NoPen);
    if (!compact_ && hasFocus() && it->index == focused_) {
      p.setPen(QPen(QColor(c.action), 2));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(r.adjusted(2, 2, -2, -3), 5, 5);
      p.setPen(Qt::NoPen);
    }
    // The footing grows with the progression model's earned progress.
    const bool settling = settling_.contains(tile.id);
    const QRectF footing(r.left(), r.bottom() - 5, r.width(), 4);
    p.setBrush(mix(c.surface, c.accent, .22));
    p.drawRoundedRect(footing, 2, 2);
    const qreal progress = settled ? 1.0 : qBound(0.0, tile.progress, 1.0);
    if (progress > 0 && !settling) {
      p.setBrush(mix(c.surface, c.accent, settled && !settling ? .82 : .68));
      p.drawRoundedRect(QRectF(footing.left(), footing.top(),
                               footing.width() * progress, footing.height()), 2, 2);
    }
    if (settling) {
      // Only the earned footing develops; text, layout, and state are final now.
      p.setBrush(mix(c.surface, c.accent, .82));
      p.drawRoundedRect(QRectF(footing.left(), footing.top(),
                              footing.width() * settlementProgress_, footing.height()), 2, 2);
    }
    if (highlight) {
      p.setBrush(QColor(c.accent));
      p.drawRoundedRect(QRectF(r.left() + 14, r.top() + 10, 18, 3), 1.5, 1.5);
    }
    auto headerFont = small(compact_ ? 11 : 12);
    headerFont.setWeight(QFont::Medium);
    p.setFont(headerFont);
    qreal headerRight = r.right() - 12;
    if (tile.challenged()) {
      p.setPen(QColor(c.negative));
      const qreal width = QFontMetricsF(p.font()).horizontalAdvance(QStringLiteral("!!"));
      p.drawText(QRectF(headerRight - width, r.top() + 6, width, 16),
                 Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("!!"));
      headerRight -= width + 6;
    }
    if (!subject.isEmpty()) {
      p.setPen(wanikaniSubjectInk(tile.subjectKind, theme_));
      const qreal width = QFontMetricsF(p.font()).horizontalAdvance(subject);
      p.drawText(QRectF(headerRight - width, r.top() + 6, width, 16),
                 Qt::AlignRight | Qt::AlignVCenter, subject);
    }
    auto wordFont = japanese(compact_ ? 26 : 32);
    const int naturalWidth = qMin(width(), qBound(144,
        int(QFontMetricsF(wordFont).horizontalAdvance(tile.characters)) + 40, 300));
    if (!compact_ && r.width() < naturalWidth) {
      // Narrowed neighbors retain readable Japanese without clipping a word
      // that fit before expansion. Long words keep their existing wrapping.
      while (wordFont.pixelSize() > 26 &&
             QFontMetricsF(wordFont).horizontalAdvance(tile.characters) > r.width() - 28)
        wordFont.setPixelSize(wordFont.pixelSize() - 1);
    }
    p.setFont(wordFont);
    p.setPen(QColor(c.text));
    const bool hasHeader = !subject.isEmpty() || tile.challenged();
    const QRectF word(r.left() + 14, r.top() + (hasHeader ? 24 : 14),
                     r.width() - 28, (compact_ ? 58 : 60) - (hasHeader ? 10 : 0));
    // Wrap long vocabulary, retaining the full text in the hover description.
    p.save();
    p.setClipRect(word, Qt::IntersectClip);
    p.drawText(word, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWrapAnywhere, tile.characters);
    p.restore();
    if (!compact_) {
      p.setFont(japanese(13));
      p.setPen(QColor(c.muted));
      QFontMetricsF fm(p.font());
      p.drawText(QRectF(r.left() + 14, r.top() + 78, r.width() - 28, 20),
                 Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(tile.reading, Qt::ElideRight, r.width() - 28));
      p.setFont(small(12));
      fm = QFontMetricsF(p.font());
      p.drawText(QRectF(r.left() + 14, r.top() + 98, r.width() - 28, 19),
                 Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(tile.meaning, Qt::ElideRight, r.width() - 28));
      if (expanded) {
        p.setFont(small(12));
        p.setPen(QColor(c.text));
        const QString reading = tr("Reading · %1 successful days%2")
            .arg(tile.readingSuccesses)
            .arg(tile.readingChallenge ? tr(" · Challenging") : QString());
        const QString meaning = tr("Meaning · %1 successful days%2")
            .arg(tile.meaningSuccesses)
            .arg(tile.meaningChallenge ? tr(" · Challenging") : QString());
        p.drawText(QRectF(r.left() + 14, r.top() + 125, r.width() - 28, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(reading, Qt::ElideRight, r.width() - 28));
        p.drawText(QRectF(r.left() + 14, r.top() + 146, r.width() - 28, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(meaning, Qt::ElideRight, r.width() - 28));
        p.setPen(QColor(c.muted));
        QString progress = tr("Progress · %1 · %2%")
            .arg(stage(tile)).arg(qBound(0, qRound(tile.progress * 100), 100));
        p.drawText(QRectF(r.left() + 14, r.top() + 168, r.width() - 28, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(progress, Qt::ElideRight, r.width() - 28));
        if (!tile.source.trimmed().isEmpty()) {
          p.setPen(QColor(c.muted));
          const auto source = tr("Source · %1").arg(sourceName(tile));
          p.drawText(QRectF(r.left() + 14, r.top() + 190, r.width() - 28, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetricsF(p.font()).elidedText(source, Qt::ElideRight, r.width() - 28));
        }
      }
    }
    // Paired seeds refer to reading and meaning; their development is qualitative.
    p.setPen(Qt::NoPen);
    for (int dimension = 0; dimension < 2; ++dimension) {
      const int successes = dimension ? tile.meaningSuccesses : tile.readingSuccesses;
      p.setBrush(mix(c.surface, c.accent, settled ? .90 : successes > 0 ? .55 : .18));
      p.drawEllipse(QPointF(r.right() - 15 - dimension * 9, r.bottom() - 12), settled ? 2.5 : 2, settled ? 2.5 : 2);
    }
  }
}
bool WordTileCanvas::event(QEvent *event) {
  if (event->type() == QEvent::ToolTip) {
    auto *help = static_cast<QHelpEvent *>(event);
    const int index = tileAt(help->pos());
    if (index >= 0) {
      const auto text = tileDescription(index).toHtmlEscaped().replace('\n', "<br>");
      QToolTip::showText(help->globalPos(), "<html>" + text + "</html>", this, tileRect(index));
      return true;
    }
    QToolTip::hideText();
    return true;
  }
  return QWidget::event(event);
}
QString WordTileCanvas::tileName(int index) const {
  return index >= 0 && index < tileCount() ? tiles_[index].characters : QString();
}
QString WordTileCanvas::tileDescription(int index) const {
  if (index < 0 || index >= tileCount()) return {};
  const auto &tile = tiles_[index];
  auto text = tile.characters + "\n" + tile.reading + " · " + tile.meaning;
  text += tr("\n\nRecall");
  text += tr("\nReading · %1 successful days%2")
              .arg(tile.readingSuccesses)
              .arg(tile.readingChallenge ? tr(" · Needs care") : QString());
  text += tr("\nMeaning · %1 successful days%2")
              .arg(tile.meaningSuccesses)
              .arg(tile.meaningChallenge ? tr(" · Needs care") : QString());
  text += tr("\n\nProgress · %1 · %2%")
              .arg(stage(tile))
              .arg(qBound(0, qRound(tile.progress * 100), 100));
  if (tile.challenged())
    text += tr("\nChallenging word: recent reading or meaning misses. Clears after three successful days in the affected skill.");
  if (!subjectName(tile).isEmpty())
    text += tr("\nType · %1").arg(tile.subjectKind == "kanji" ? tr("Kanji") : tr("Vocabulary"));
  if (!tile.source.trimmed().isEmpty())
    text += tr("\nSource · %1").arg(sourceName(tile));
  return text;
}
QRect WordTileCanvas::tileRect(int index) const {
  return index >= 0 && index < layout_.size() ? layout_[index].rect.toRect() : QRect();
}
int WordTileCanvas::tileAt(const QPoint &point) const {
  // Tops remain ordered even when a selected tile grows taller than its row.
  auto first = std::lower_bound(layout_.cbegin(), layout_.cend(), point.y() - 240,
      [](const Placement &place, int y) { return place.rect.top() < y; });
  for (auto it = first; it != layout_.cend() && it->rect.top() <= point.y(); ++it)
    if (it->rect.contains(point)) return it->index;
  return -1;
}
void WordTileCanvas::focusTile(int index) {
  if (compact_ || index < 0 || index >= tileCount()) return;
  inspected_ = true;
  focused_ = index;
  expanded_ = index;
  refreshGeometry();
  setFocus(Qt::OtherFocusReason);
  for (auto *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
    if (auto *area = qobject_cast<QScrollArea *>(ancestor)) {
      // Expansion posts a parent layout request. Scroll after the new range
      // exists, and discard a stale jump if focus/data changed in the meantime.
      const QPointer<QScrollArea> guardedArea(area);
      const int subjectId = tiles_[index].id;
      QTimer::singleShot(0, this, [this, guardedArea, subjectId] {
        if (!guardedArea || focused_ < 0 || focused_ >= tiles_.size() ||
            expanded_ != focused_ || tiles_[focused_].id != subjectId) return;
        const auto bounds = tileRect(focused_);
        const auto position = mapTo(guardedArea->widget(), bounds.center());
        guardedArea->ensureVisible(position.x(), position.y(), bounds.width() / 2 + 8,
                                   bounds.height() / 2 + 8);
      });
      break;
    }
  }
  emit tileFocused(tileDescription(index));
  auto *accessible = QAccessible::queryAccessibleInterface(this);
  if (accessible && accessible->child(index)) {
    QAccessibleEvent focused(accessible->child(index), QAccessible::Focus);
    QAccessible::updateAccessibility(&focused);
  }
  update();
}
void WordTileCanvas::focusInEvent(QFocusEvent *event) {
  QWidget::focusInEvent(event);
  inspected_ = true;
  if (focused_ >= 0) emit tileFocused(tileDescription(focused_));
  update();
}
void WordTileCanvas::mousePressEvent(QMouseEvent *event) {
  if (!compact_ && event->button() == Qt::LeftButton) {
    const int index = tileAt(event->position().toPoint());
    if (index >= 0) {
      if (expanded_ == index) {
        expanded_ = -1;
        focused_ = index;
        refreshGeometry();
        emit tileFocused(tileDescription(index));
        update();
      } else focusTile(index);
      event->accept(); return;
    }
  }
  QWidget::mousePressEvent(event);
}
void WordTileCanvas::keyPressEvent(QKeyEvent *event) {
  if (compact_ || tileCount() == 0) { QWidget::keyPressEvent(event); return; }
  if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (expanded_ == focused_) {
      expanded_ = -1;
      refreshGeometry();
      emit tileFocused(tileDescription(focused_));
      update();
    } else focusTile(focused_);
    event->accept();
    return;
  }
  int next = qMax(0, focused_);
  if (event->key() == Qt::Key_Left) --next;
  else if (event->key() == Qt::Key_Right) ++next;
  else if (event->key() == Qt::Key_Home) next = 0;
  else if (event->key() == Qt::Key_End) next = tileCount() - 1;
  else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
    const auto originRect = tileRect(next);
    const auto origin = originRect.center();
    qint64 best = std::numeric_limits<qint64>::max();
    for (int i = 0; i < tileCount(); ++i) {
      const auto center = tileRect(i).center();
      const int dy = tileRect(i).top() - originRect.top();
      if ((event->key() == Qt::Key_Up && dy >= 0) ||
          (event->key() == Qt::Key_Down && dy <= 0)) continue;
      const qint64 distance = qint64(qAbs(dy)) * 100000 + qAbs(center.x() - origin.x());
      if (distance < best) { best = distance; next = i; }
    }
  } else { QWidget::keyPressEvent(event); return; }
  focusTile(qBound(0, next, tileCount() - 1));
  event->accept();
}

ProgressionPage::ProgressionPage(QWidget *parent) : QWidget(parent) {
  setObjectName("wordTilesPage");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 20);
  layout->setSpacing(12);
  heading_ = new QLabel(tr("Your word tiles"), this);
  counts_ = new QLabel(this);
  layout->addWidget(heading_);
  layout->addWidget(counts_);
  search_ = new QLineEdit(this);
  search_->setObjectName("wordSearch");
  search_->setPlaceholderText(tr("Search Japanese, romaji, meanings or tags"));
  search_->setAccessibleName(tr("Search words"));
  search_->setClearButtonEnabled(true);
  layout->addWidget(search_);
  results_ = new QLabel(this);
  results_->setObjectName("wordSearchResults");
  layout->addWidget(results_);
  connect(search_, &QLineEdit::textChanged, this, [this] {
    refreshText();
    scroll_->verticalScrollBar()->setValue(0);
  });
  scroll_ = new QScrollArea(this);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *body = new QWidget;
  auto *bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(0, 12, 0, 0);
  bodyLayout->setSpacing(0);
  empty_ = new QLabel(tr("No word tiles yet\nConnect WaniKani or Anki and practice to start a collection."), body);
  empty_->setObjectName("wordTilesEmpty");
  empty_->setWordWrap(true);
  empty_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  empty_->setContentsMargins(20, 18, 20, 18);
  canvas_ = new WordTileCanvas(body);
  bodyLayout->addWidget(empty_);
  bodyLayout->addWidget(canvas_);
  bodyLayout->addStretch();
  scroll_->setWidget(body);
  layout->addWidget(scroll_, 1);
  setTheme(theme_);
  refreshText();
}
void ProgressionPage::setState(const ProgressState &state) {
  state_ = state;
  refreshText();
}
void ProgressionPage::refreshText() {
  int forming = 0, practiceOnly = 0;
  for (const auto &tile : state_.tiles) {
    forming += tile.eligible && !tile.settled;
    practiceOnly += !tile.eligible && !tile.settled;
  }
  auto counts = tr("%1 settled  ·  %2 taking shape").arg(state_.settled).arg(forming);
  if (practiceOnly) counts += tr("  ·  %1 practiced").arg(practiceOnly);
  counts_->setText(counts);
  const QString query = search_->text().trimmed().normalized(QString::NormalizationForm_KC);
  const QString kanaQuery = toKana(query);
  const QString tagQuery = query.toCaseFolded();
  const QString requestedTag = tagQuery == "kanji" ? QStringLiteral("Kanji")
      : tagQuery == "vocab" || tagQuery == "vocabulary" ? QStringLiteral("Vocab") : QString();
  QVector<ProgressTile> matches;
  matches.reserve(state_.tiles.size());
  for (const auto &tile : state_.tiles) {
    if (!requestedTag.isEmpty()) {
      if (subjectName(tile) == requestedTag) matches.append(tile);
      continue;
    }
    const auto contains = [&query](const QString &text) {
      return text.normalized(QString::NormalizationForm_KC).contains(query, Qt::CaseInsensitive);
    };
    if (query.isEmpty() || contains(tile.characters) || contains(tile.reading) || contains(tile.meaning) ||
        contains(subjectName(tile)) ||
        toKana(tile.reading).contains(kanaQuery) || toKana(tile.characters).contains(kanaQuery))
      matches.append(tile);
  }
  canvas_->setTiles(matches);
  results_->setText(tr("%1 of %2 words").arg(matches.size()).arg(state_.tiles.size()));
  results_->setVisible(!query.isEmpty());
  empty_->setText(state_.tiles.isEmpty()
      ? tr("No word tiles yet\nConnect WaniKani or Anki and practice to start a collection.")
      : tr("No matching words\nTry Japanese, romaji, a meaning or a tag, or clear the search."));
  empty_->setVisible(matches.isEmpty());
  canvas_->setVisible(!matches.isEmpty());
}
void ProgressionPage::setTheme(int theme) {
  theme_ = qBound(0, theme, themeCount - 1);
  const auto c = colors(theme_);
  heading_->setStyleSheet(QString("font-size:26px; font-weight:600; color:%1; background:transparent;").arg(c.text));
  counts_->setStyleSheet(QString("font-size:14px; color:%1; background:transparent;").arg(c.accent));
  results_->setStyleSheet(QString("font-size:14px; color:%1; background:transparent;").arg(c.muted));
  const QColor surface(c.surface);
  const QString wash = QString("rgba(%1,%2,%3,26)").arg(surface.red()).arg(surface.green()).arg(surface.blue());
  empty_->setStyleSheet(QString("font-size:16px; color:%1; background:%2; border:none; border-radius:8px;").arg(c.text, wash));
  scroll_->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }");
  canvas_->setTheme(theme_);
}
ChallengeMark::ChallengeMark(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::NoFocus);
  setCleared(false);
}
void ChallengeMark::setTheme(int theme) { theme_ = qBound(0, theme, themeCount - 1); update(); }
void ChallengeMark::setCleared(bool cleared) {
  cleared_ = cleared;
  setFixedSize(cleared ? 112 : 26, 22);
  setAccessibleName(cleared ? tr("Challenge cleared") : tr("Challenging word"));
  setToolTip(cleared
                 ? tr("Challenge cleared after three successful practice days.")
                 : tr("Challenging word: recent reading or meaning misses. Clears after three successful days in the affected skill."));
  update();
}
void ChallengeMark::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto c = colors(theme_);
  const QColor ink(cleared_ ? c.accent : c.negative);
  QLinearGradient edge(rect().topLeft(), rect().bottomRight());
  edge.setColorAt(0, cleared_ ? QColor(c.accent) : theme_ % 2 ? QColor("#dfa16c") : QColor("#b85b31"));
  edge.setColorAt(1, ink);
  p.setPen(QPen(QBrush(edge), 1));
  QColor wash(ink); wash.setAlpha(18);
  p.setBrush(wash);
  p.drawRoundedRect(rect().adjusted(.5, .5, -.5, -.5), 5, 5);
  p.setPen(ink);
  auto markFont = small(cleared_ ? 11 : 12);
  if (!cleared_) markFont.setWeight(QFont::DemiBold);
  p.setFont(markFont);
  p.drawText(rect().adjusted(5, 1, -5, -1), Qt::AlignCenter,
             cleared_ ? tr("Challenge cleared") : QStringLiteral("!!"));
}
