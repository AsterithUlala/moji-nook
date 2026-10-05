#pragma once

#include "progression.h"
#include "theme.h"
#include <QtWidgets>

class WordTileCanvas : public QWidget {
  Q_OBJECT
public:
  explicit WordTileCanvas(QWidget *parent = nullptr);
  void setTiles(const QVector<ProgressTile> &tiles);
  void setTheme(int theme);
  void setCompact(bool compact);
  void setHighlightedIds(const QSet<int> &ids);
  void animateSettled(const QSet<int> &ids);
  void stopSettlement();
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;
  int tileCount() const { return compact_ ? qMin(6, int(tiles_.size())) : int(tiles_.size()); }
  QString tileName(int index) const;
  QString tileDescription(int index) const;
  QRect tileRect(int index) const;
  int tileAt(const QPoint &point) const;
  int focusedTile() const { return focused_; }
  int expandedTile() const { return expanded_; }
  bool compact() const { return compact_; }
  void focusTile(int index);
signals:
  void tileFocused(QString description);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  bool event(QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;

private:
  struct Placement { QRectF rect; int index; };
  QVector<Placement> placements(int width, int *height = nullptr) const;
  void refreshGeometry();
  QVector<ProgressTile> tiles_;
  QVector<Placement> layout_;
  QSet<int> highlighted_, settling_;
  QVariantAnimation settlement_;
  qreal settlementProgress_ = 1;
  int theme_ = defaultTheme;
  bool compact_ = false;
  int focused_ = -1;
  int expanded_ = -1;
  bool inspected_ = false;
};

class ProgressionPage : public QWidget {
public:
  explicit ProgressionPage(QWidget *parent = nullptr);
  void setState(const ProgressState &state);
  void setTheme(int theme);
private:
  void refreshText();
  ProgressState state_;
  int theme_ = defaultTheme;
  QLabel *heading_, *counts_, *empty_;
  QLineEdit *search_;
  QLabel *results_;
  WordTileCanvas *canvas_;
  QScrollArea *scroll_;
};

void installWordTileAccessibility();

class ChallengeMark : public QWidget {
public:
  explicit ChallengeMark(QWidget *parent = nullptr);
  void setTheme(int theme);
  void setCleared(bool cleared);
protected:
  void paintEvent(QPaintEvent *event) override;
private:
  int theme_ = defaultTheme;
  bool cleared_ = false;
};
