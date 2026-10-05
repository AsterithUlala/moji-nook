#include "progression_widgets.h"
#include <QAccessibleWidget>

namespace {
class AccessibleWordTile : public QAccessibleInterface,
                           public QAccessibleActionInterface {
public:
  AccessibleWordTile(WordTileCanvas *canvas, int index) : canvas_(canvas), index_(index) {}
  bool isValid() const override { return canvas_ && index_ < canvas_->tileCount(); }
  QObject *object() const override { return nullptr; }
  QAccessibleInterface *parent() const override {
    return canvas_ ? QAccessible::queryAccessibleInterface(canvas_) : nullptr;
  }
  QAccessibleInterface *child(int) const override { return nullptr; }
  QAccessibleInterface *childAt(int, int) const override { return nullptr; }
  int childCount() const override { return 0; }
  int indexOfChild(const QAccessibleInterface *) const override { return -1; }
  QRect rect() const override {
    if (!isValid()) return {};
    auto local = canvas_->tileRect(index_);
    return QRect(canvas_->mapToGlobal(local.topLeft()), local.size());
  }
  QAccessible::Role role() const override { return QAccessible::ListItem; }
  QAccessible::State state() const override {
    QAccessible::State s;
    s.readOnly = true;
    s.invalid = !isValid();
    if (s.invalid) return s;
    s.invisible = !canvas_->isVisible();
    s.focusable = !canvas_->compact();
    s.focused = canvas_->hasFocus() && canvas_->focusedTile() == index_;
    s.offscreen = !canvas_->visibleRegion().intersects(canvas_->tileRect(index_));
    return s;
  }
  QString text(QAccessible::Text type) const override {
    if (!isValid()) return {};
    return type == QAccessible::Name ? canvas_->tileName(index_)
         : type == QAccessible::Description ? canvas_->tileDescription(index_) : QString();
  }
  void setText(QAccessible::Text, const QString &) override {}
  void *interface_cast(QAccessible::InterfaceType type) override {
    return type == QAccessible::ActionInterface
        ? static_cast<QAccessibleActionInterface *>(this) : nullptr;
  }
  QStringList actionNames() const override {
    return isValid() && !canvas_->compact() ? QStringList{setFocusAction()} : QStringList{};
  }
  void doAction(const QString &name) override {
    if (isValid() && name == setFocusAction()) canvas_->focusTile(index_);
  }
  QStringList keyBindingsForAction(const QString &) const override { return {}; }
  WordTileCanvas *canvas() const { return canvas_; }
  int index() const { return index_; }
private:
  QPointer<WordTileCanvas> canvas_;
  int index_;
};

class AccessibleWordTiles : public QAccessibleWidget {
public:
  explicit AccessibleWordTiles(WordTileCanvas *canvas)
      : QAccessibleWidget(canvas, QAccessible::List) {}
  ~AccessibleWordTiles() override {
    for (auto id : children_) QAccessible::deleteAccessibleInterface(id);
  }
  WordTileCanvas *canvas() const { return static_cast<WordTileCanvas *>(widget()); }
  int childCount() const override { return canvas()->tileCount(); }
  QAccessibleInterface *child(int index) const override {
    if (index < 0 || index >= childCount()) return nullptr;
    if (!children_.contains(index))
      children_.insert(index, QAccessible::registerAccessibleInterface(
          new AccessibleWordTile(canvas(), index)));
    return QAccessible::accessibleInterface(children_[index]);
  }
  QAccessibleInterface *childAt(int x, int y) const override {
    return child(canvas()->tileAt(canvas()->mapFromGlobal(QPoint(x, y))));
  }
  int indexOfChild(const QAccessibleInterface *child) const override {
    const auto *tile = dynamic_cast<const AccessibleWordTile *>(child);
    return tile && tile->canvas() == canvas() ? tile->index() : -1;
  }
  QAccessibleInterface *focusChild() const override {
    return canvas()->hasFocus() ? child(canvas()->focusedTile()) : nullptr;
  }
private:
  mutable QHash<int, QAccessible::Id> children_;
};

QAccessibleInterface *tileFactory(const QString &, QObject *object) {
  if (auto *canvas = qobject_cast<WordTileCanvas *>(object))
    return new AccessibleWordTiles(canvas);
  return nullptr;
}
}

void installWordTileAccessibility() {
  static const bool installed = [] { QAccessible::installFactory(tileFactory); return true; }();
  Q_UNUSED(installed);
}
