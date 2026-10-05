#pragma once
#include "progression_widgets.h"

// Passive end-of-session surface. It never requests keyboard focus or input.
class ProgressRecap : public QWidget {
public:
  explicit ProgressRecap(QWidget *parent = nullptr);
  void present(const QVector<ProgressNotice> &notices,
               const ProgressState &state);
  void setTheme(int index);
  void setReducedMotion(bool reduced);
  void setPlacement(const QString &monitor, int corner);
  void dismiss();
  int remainingTime() const;

protected:
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  bool eventFilter(QObject *object, QEvent *event) override;

private:
  QLabel *brand, *title, *message, *summary, *linger;
  QToolButton *closeButton;
  WordTileCanvas *composition;
  QWidget *content;
  QScrollArea *scroll;
  QTimer lifetime;
  int remaining = 5000, themeIndex = defaultTheme, cornerIndex = 0;
  QString monitorName;
  QPointer<QScreen> placementScreen;
  QMetaObject::Connection geometryConnection;
  QPoint appearedAt;
  bool intentionalHover = false, reducedMotion = false;
  void place();
};
