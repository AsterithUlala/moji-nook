#include "print_ink.h"
#include "theme.h"
#include "display_preview.h"
#include "ui.h"
#include <QAccessible>
#include <QScopeGuard>
#include <QtTest>
#include <numeric>

class UiTest : public QObject {
  Q_OBJECT
  static ProgressState progressionFixture() {
    ProgressState state;
    const QStringList words{"猫", "犬", "山", "川", "空", "花", "雨", "月"};
    const QStringList readings{"ねこ", "いぬ", "やま", "かわ", "そら", "はな", "あめ", "つき"};
    const QStringList meanings{"cat", "dog", "mountain", "river", "sky", "flower", "rain", "moon"};
    for (int i = 0; i < words.size(); ++i) {
      ProgressTile tile;
      tile.id = i + 1;
      tile.characters = words[i];
      tile.reading = readings[i];
      tile.meaning = meanings[i];
      tile.source = "wanikani";
      tile.subjectKind = "vocabulary";
      tile.eligible = i != 2;
      tile.settled = i == 0;
      tile.phase = i == 0 ? TilePhase::Settled : i == 1 ? TilePhase::Growing : TilePhase::Seen;
      tile.meaningChallenge = i == 1;
      state.tiles.append(tile);
    }
    state.total = state.tiles.size();
    state.settled = 1;
    return state;
  }
private slots:
  void progressionRecapIsPassiveAndEmptyNoticesStayHidden() {
    ProgressRecap recap;
    const auto state = progressionFixture();
    recap.present({}, state);
    QVERIFY(!recap.isVisible());
    recap.present({{ProgressNotice::Completed, 1, "猫", 0}}, state);
    QVERIFY(recap.isVisible());
    QVERIFY(recap.testAttribute(Qt::WA_ShowWithoutActivating));
    QVERIFY(recap.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
    QCOMPARE(recap.focusPolicy(), Qt::NoFocus);
    auto *close = recap.findChild<QToolButton *>("recapClose");
    QVERIFY(close);
    QCOMPARE(close->focusPolicy(), Qt::NoFocus);
    close->click();
    QVERIFY(!recap.isVisible());
    recap.present({{ProgressNotice::Completed, 1, "猫", 0}}, state);
    QVERIFY(recap.findChildren<QLineEdit *>().isEmpty());
    auto *composition = recap.findChild<WordTileCanvas *>("recapComposition");
    QVERIFY(composition);
    QVERIFY(composition->compact());
    QCOMPARE(composition->focusPolicy(), Qt::NoFocus);
    QCOMPARE(composition->tileCount(), 1);
    QCOMPARE(composition->tileName(0), QString("猫"));
    QVERIFY(!recap.accessibleName().isEmpty());
    QVERIFY(recap.accessibleDescription().contains("猫"));
    QCoreApplication::processEvents(); // Include the queued post-show placement.
    const QRect available = recap.screen()->availableGeometry();
    const int shortHeight = recap.height();
    QVERIFY(recap.width() <= qMax(1, available.width() - 32));
    QVERIFY(recap.height() <= qMax(1, available.height() - 32));
    auto *scroll = recap.findChild<QScrollArea *>();
    QVERIFY(scroll && scroll->viewport()->width() > 0 && scroll->viewport()->height() > 0);
    auto longState = state;
    const QString longWord = QString("日本語").repeated(120);
    longState.tiles[0].characters = longWord;
    recap.present({{ProgressNotice::Completed, 1, longWord, 0}}, longState);
    QCoreApplication::processEvents();
    QVERIFY(recap.width() <= qMax(1, available.width() - 32));
    QVERIFY(recap.height() <= qMax(1, available.height() - 32));
    QVERIFY(recap.height() >= shortHeight);
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    recap.present({{ProgressNotice::Completed, 1, "猫", 0}}, state);
    QCoreApplication::processEvents();
    QCOMPARE(recap.height(), shortHeight);
    QCOMPARE(scroll->verticalScrollBar()->value(), 0);
    recap.present({}, state);
    QVERIFY(!recap.isVisible());
    QTest::qWait(50);
    QVERIFY(!recap.isVisible());
  }
  void progressionRecapParkedPointerExpiresAfterFiveSeconds() {
    const QPoint oldCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([oldCursor] { QCursor::setPos(oldCursor); });
    ProgressRecap recap;
    recap.setPlacement({}, 0);
    // Match the pointer already sitting on Finish when the recap replaces it.
    const QPoint parked = recap.geometry().center();
    QCursor::setPos(parked);
    QElapsedTimer elapsed;
    elapsed.start();
    recap.present({{ProgressNotice::Completed, 1, "猫", 0}}, progressionFixture());
    const QPoint local = recap.mapFromGlobal(parked);
    QEnterEvent initialEnter(local, local, parked);
    QCoreApplication::sendEvent(&recap, &initialEnter);
    QTest::qWait(350);
    // Compositors may deliver the initial Enter late, after the grace period.
    QEnterEvent delayedParkedEnter(local, local, parked);
    QCoreApplication::sendEvent(&recap, &delayedParkedEnter);
    QMouseEvent stationaryMove(QEvent::MouseMove, local, parked,
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&recap, &stationaryMove);
    const int beforeLeave = recap.remainingTime();
    QEvent ordinaryLeave(QEvent::Leave);
    QCoreApplication::sendEvent(&recap, &ordinaryLeave);
    // An ordinary leave must not restart the original five-second lifetime.
    QVERIFY(recap.remainingTime() <= beforeLeave + 30);
    QTest::qWait(3650);
    QVERIFY(recap.isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!recap.isVisible(), 2500);
    QVERIFY2(elapsed.elapsed() >= 4500, "Recap disappeared substantially before five seconds.");
    QVERIFY2(elapsed.elapsed() <= 6500, "Parked pointer pinned the recap or extended its lifetime.");
  }
  void progressionRecapIntentionalHoverPausesAndResumes() {
    const QPoint oldCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([oldCursor] { QCursor::setPos(oldCursor); });
    QCursor::setPos(0, 0);
    ProgressRecap recap;
    const auto state = progressionFixture();
    const QVector<ProgressNotice> notices{{ProgressNotice::Completed, 1, "猫", 0}};
    recap.present(notices, state);
    QTest::qWait(50); // Deliberate hover counts even in the first 250ms.
    const QPoint local = recap.rect().center();
    QCursor::setPos(recap.mapToGlobal(local));
    QEnterEvent deliberateEnter(local, local, recap.mapToGlobal(local));
    QCoreApplication::sendEvent(&recap, &deliberateEnter);
    const int paused = recap.remainingTime();
    QVERIFY(paused > 3500 && paused < 5000);
    QTest::qWait(1100);
    QVERIFY(recap.isVisible());
    QCOMPARE(recap.remainingTime(), paused);
    QEvent leave(QEvent::Leave);
    QElapsedTimer resumed;
    resumed.start();
    QCursor::setPos(0, 0);
    QCoreApplication::sendEvent(&recap, &leave);
    QTest::qWait(200);
    QVERIFY(recap.remainingTime() < paused - 100);
    QTRY_VERIFY_WITH_TIMEOUT(!recap.isVisible(), paused + 1000);
    QVERIFY(resumed.elapsed() >= paused - 500);
    QVERIFY(resumed.elapsed() <= paused + 1000);

    // A pointer initially parked inside can also deliberately move over a child.
    const QPoint parked = recap.geometry().center();
    QCursor::setPos(parked);
    recap.present(notices, state);
    QTest::qWait(350);
    auto *label = recap.findChild<QLabel *>("recapTitle");
    QVERIFY(label);
    const QPoint moved = parked + QPoint(8, 0);
    QCursor::setPos(moved);
    QMouseEvent childMove(QEvent::MouseMove, label->mapFromGlobal(moved), moved,
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(label, &childMove);
    const int movedPause = recap.remainingTime();
    QTest::qWait(350);
    QCOMPARE(recap.remainingTime(), movedPause);
    QCursor::setPos(0, 0);
    QCoreApplication::sendEvent(&recap, &leave);
    QTest::qWait(200);
    QVERIFY(recap.remainingTime() < movedPause - 100);
    recap.dismiss();

    // Scrolling a short-screen recap is also an intentional linger, even when
    // the pointer was parked there before the recap appeared.
    QCursor::setPos(recap.geometry().center());
    recap.present(notices, state);
    const QPoint wheelLocal = label->rect().center();
    QWheelEvent wheel(wheelLocal, label->mapToGlobal(wheelLocal), {}, QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(label, &wheel);
    const int wheelPause = recap.remainingTime();
    QTest::qWait(350);
    QCOMPARE(recap.remainingTime(), wheelPause);
    QCursor::setPos(0, 0);
    QCoreApplication::sendEvent(&recap, &leave);
    QTest::qWait(200);
    QVERIFY(recap.remainingTime() < wheelPause - 100);
    recap.dismiss();
  }
  void progressionTilesSupportKeyboardAndVirtualAccessibility() {
    const auto state = progressionFixture();
    WordTileCanvas canvas;
    canvas.setTiles(state.tiles);
    canvas.resize(340, 620);
    canvas.show();
    canvas.activateWindow();
    canvas.setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(canvas.hasFocus());
    QCOMPARE(canvas.focusPolicy(), Qt::StrongFocus);
    auto *accessible = QAccessible::queryAccessibleInterface(&canvas);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::List);
    QCOMPARE(accessible->childCount(), state.tiles.size());
    QVERIFY(!accessible->child(-1));
    QVERIFY(!accessible->child(state.tiles.size()));
    for (int i = 0; i < state.tiles.size(); ++i) {
      auto *tile = accessible->child(i);
      QVERIFY(tile && tile->isValid());
      QCOMPARE(tile->role(), QAccessible::ListItem);
      QCOMPARE(tile->parent(), accessible);
      QCOMPARE(accessible->indexOfChild(tile), i);
      QCOMPARE(tile->text(QAccessible::Name), state.tiles[i].characters);
      const auto description = tile->text(QAccessible::Description);
      QVERIFY(description.contains(state.tiles[i].reading));
      QVERIFY(description.contains(state.tiles[i].meaning));
      QVERIFY(tile->state().readOnly);
      QVERIFY(tile->state().focusable);
      QVERIFY(!tile->rect().isEmpty());
      QCOMPARE(accessible->childAt(tile->rect().center().x(), tile->rect().center().y()), tile);
      QVERIFY(tile->actionInterface());
      QVERIFY(tile->actionInterface()->actionNames().contains(QAccessibleActionInterface::setFocusAction()));
    }
    QVERIFY(accessible->child(0)->text(QAccessible::Description).contains("Settled"));
    QVERIFY(accessible->child(1)->text(QAccessible::Description).contains("Needs care"));
    QVERIFY(accessible->child(2)->text(QAccessible::Description).contains("Practiced"));
    QSignalSpy inspected(&canvas, &WordTileCanvas::tileFocused);
    QTest::keyClick(&canvas, Qt::Key_Right);
    QCOMPARE(canvas.focusedTile(), 1);
    QTest::keyClick(&canvas, Qt::Key_Down);
    const int lowerTile = canvas.focusedTile();
    QVERIFY(lowerTile > 1);
    QTest::keyClick(&canvas, Qt::Key_Up);
    const int upperTile = canvas.focusedTile();
    QVERIFY(upperTile < lowerTile);
    QTest::keyClick(&canvas, Qt::Key_Left);
    QCOMPARE(canvas.focusedTile(), qMax(0, upperTile - 1));
    QTest::keyClick(&canvas, Qt::Key_Home);
    QTest::keyClick(&canvas, Qt::Key_Left);
    QCOMPARE(canvas.focusedTile(), 0);
    QTest::keyClick(&canvas, Qt::Key_End);
    QCOMPARE(canvas.focusedTile(), 7);
    QCOMPARE(inspected.last()[0].toString(), canvas.tileDescription(7));
    QCOMPARE(accessible->focusChild(), accessible->child(7));
    QVERIFY(accessible->child(7)->state().focused);
    QVERIFY(!accessible->child(0)->state().focused);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.tileRect(2).center());
    QCOMPARE(canvas.focusedTile(), 2);
    QCOMPARE(inspected.last()[0].toString(), canvas.tileDescription(2));
    accessible->child(4)->actionInterface()->doAction(QAccessibleActionInterface::setFocusAction());
    QCOMPARE(canvas.focusedTile(), 4);
    QCOMPARE(accessible->focusChild(), accessible->child(4));
    QTest::keyClick(&canvas, Qt::Key_Home);
    QCOMPARE(canvas.focusedTile(), 0);
    auto *previousTile = accessible->child(7);
    canvas.setTiles({});
    QCOMPARE(accessible->childCount(), 0);
    QCOMPARE(canvas.focusedTile(), -1);
    QVERIFY(!accessible->focusChild());
    QVERIFY(!previousTile->isValid());
    QTest::keyClick(&canvas, Qt::Key_Right);
    QCOMPARE(canvas.focusedTile(), -1);

    ProgressionPage page;
    page.resize(600, 400);
    // First End from a collapsed collection must reveal the full expanded tile.
    auto longState = state;
    for (int i = longState.tiles.size(); i < 40; ++i) {
      auto tile = longState.tiles.first();
      tile.id = 1000 + i;
      tile.characters = "日";
      longState.tiles.append(tile);
    }
    page.setState(longState);
    page.show();
    QCoreApplication::processEvents();
    auto *jumpCanvas = page.findChild<WordTileCanvas *>();
    auto *jumpScroll = page.findChild<QScrollArea *>();
    QTest::keyClick(jumpCanvas, Qt::Key_End);
    QTest::qWait(20);
    const auto lastBounds = jumpCanvas->tileRect(39);
    const int bottom = jumpCanvas->mapTo(jumpScroll->viewport(), lastBounds.bottomRight()).y();
    QVERIFY2(bottom <= jumpScroll->viewport()->height(), "Expanded last tile's stats must be visible on first End");
    page.setState(state);
    page.show();
    auto *pageCanvas = page.findChild<WordTileCanvas *>();
    QVERIFY(pageCanvas);
    QVERIFY(!page.findChild<QLabel *>("tileInspection"));
    const int collapsedHeight = pageCanvas->tileRect(1).height();
    pageCanvas->focusTile(1);
    QCOMPARE(pageCanvas->expandedTile(), 1);
    QVERIFY(pageCanvas->tileRect(1).height() > collapsedHeight);
    QVERIFY(pageCanvas->tileDescription(1).contains("犬"));
    QVERIFY(pageCanvas->tileDescription(1).contains("Needs care"));
    QTest::mouseClick(pageCanvas, Qt::LeftButton, Qt::NoModifier,
                     pageCanvas->tileRect(1).center());
    QCOMPARE(pageCanvas->expandedTile(), -1);
    QCOMPARE(pageCanvas->tileRect(1).height(), collapsedHeight);
    QTest::keyClick(pageCanvas, Qt::Key_Space);
    QCOMPARE(pageCanvas->expandedTile(), 1);
    pageCanvas->clearFocus();
    auto reordered = state;
    reordered.tiles.swapItemsAt(0, 1);
    reordered.tiles[0].meaningChallenge = false;
    page.setState(reordered);
    QCOMPARE(pageCanvas->focusedTile(), 0);
    QCOMPARE(pageCanvas->tileName(pageCanvas->focusedTile()), QString("犬"));
    QCOMPARE(pageCanvas->expandedTile(), 0);
    QVERIFY(pageCanvas->tileDescription(0).contains("犬"));
    QVERIFY(pageCanvas->tileDescription(0).contains("いぬ"));
    QVERIFY(pageCanvas->tileDescription(0).contains("dog"));
    QVERIFY(!pageCanvas->tileDescription(0).contains("Needs care"));
  }
  void progressionTileExpansionKeepsItsCollapsedRow() {
    QVector<ProgressTile> tiles;
    for (int i = 0; i < 24; ++i) {
      ProgressTile tile;
      tile.id = i + 1;
      tile.characters = i == 14 ? QString::fromUtf8("元パートナー") : QString::fromUtf8("日");
      if (i == 8) tile.characters = QString::fromUtf8("日本語");
      if (i == 9) tile.characters = QString::fromUtf8("中学校");
      tile.reading = "にち";
      tile.meaning = "day";
      tiles.append(tile);
    }

    WordTileCanvas canvas;
    canvas.setTiles(tiles);
    canvas.resize(1256, 1200);
    canvas.show();
    QCoreApplication::processEvents();

    const int selected = 14;
    QVector<QRect> collapsed;
    for (int i = 0; i < tiles.size(); ++i) collapsed.append(canvas.tileRect(i));
    QVERIFY(collapsed[selected].top() == collapsed[selected - 1].top());
    QVERIFY(collapsed[selected + 1].top() > collapsed[selected].top());

    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, collapsed[selected].center());
    QCOMPARE(canvas.expandedTile(), selected);
    QCOMPARE(canvas.tileRect(selected).top(), collapsed[selected].top());
    QVERIFY(canvas.tileRect(selected).width() > collapsed[selected].width());
    bool neighborShrank = false;
    for (int i = 0; i < tiles.size(); ++i) {
      const int expectedTop = collapsed[i].top() <= collapsed[selected].top()
          ? collapsed[i].top() : collapsed[i].top() + 110;
      QCOMPARE(canvas.tileRect(i).top(), expectedTop);
      if (i != selected && collapsed[i].top() == collapsed[selected].top()) {
        QVERIFY(canvas.tileRect(i).width() >= 120);
        neighborShrank |= canvas.tileRect(i).width() < collapsed[i].width();
      }
    }
    QVERIFY(neighborShrank);
    auto verifyGeometry = [&] {
      for (int i = 0; i < tiles.size(); ++i) {
        const QRect a = canvas.tileRect(i);
        QVERIFY(a.left() >= 0 && a.right() < canvas.width());
        for (int j = i + 1; j < tiles.size(); ++j) {
          const QRect b = canvas.tileRect(j);
          QVERIFY(!a.intersects(b));
        }
      }
    };
    verifyGeometry();

    // Selecting a sibling transfers expansion without changing row membership.
    const int sibling = selected - 1;
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.tileRect(sibling).center());
    QCOMPARE(canvas.expandedTile(), sibling);
    QCOMPARE(canvas.tileRect(sibling).top(), collapsed[sibling].top());
    QFont minimumJapanese(QStringLiteral("Noto Sans CJK JP"));
    minimumJapanese.setPixelSize(26);
    minimumJapanese.setWeight(QFont::Medium);
    for (int i : {8, 9})
      QVERIFY(canvas.tileRect(i).width() - 28 >=
              QFontMetricsF(minimumJapanese).horizontalAdvance(tiles[i].characters));
    verifyGeometry();
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.tileRect(sibling).center());
    QCOMPARE(canvas.expandedTile(), -1);
    for (int i = 0; i < tiles.size(); ++i) QCOMPARE(canvas.tileRect(i), collapsed[i]);

    // Very narrow widths still constrain every rectangle and keep the selected
    // tile inside the canvas, even when sibling minimum widths cannot be met.
    for (int width : {640, 300, 100}) {
      canvas.resize(width, 2400);
      QCoreApplication::processEvents();
      const QRect before = canvas.tileRect(selected);
      QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, before.center());
      QCOMPARE(canvas.expandedTile(), selected);
      QCOMPARE(canvas.tileRect(selected).top(), before.top());
      verifyGeometry();
      // Resizing an expanded collection uses the new collapsed row grouping.
      WordTileCanvas reference;
      reference.setTiles(tiles);
      reference.resize(width + 24, 2400);
      reference.show();
      canvas.resize(width + 24, 2400);
      QCoreApplication::processEvents();
      QCOMPARE(canvas.tileRect(selected).top(), reference.tileRect(selected).top());
      verifyGeometry();
      QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                       canvas.tileRect(selected).center());
      QCOMPARE(canvas.expandedTile(), -1);
    }
  }
  void wordsSearchFiltersImmediatelyAndKeepsDetailsUsable() {
    ProgressionPage page;
    auto state = progressionFixture();
    state.tiles[0].characters = "すし";
    state.tiles[0].reading = "すし";
    state.tiles[0].meaning = "sushi";
    state.tiles[1].characters = "休む";
    state.tiles[1].reading = "やすむ";
    state.tiles[1].meaning = "rest";
    page.setState(state);
    page.resize(600, 450);
    page.show();
    QCoreApplication::processEvents();
    auto *search = page.findChild<QLineEdit *>("wordSearch");
    auto *canvas = page.findChild<WordTileCanvas *>();
    auto *results = page.findChild<QLabel *>("wordSearchResults");
    auto *empty = page.findChild<QLabel *>("wordTilesEmpty");
    QVERIFY(search && canvas && results && empty);
    QVERIFY(search->isClearButtonEnabled());
    QCOMPARE(canvas->tileCount(), state.tiles.size());
    QVERIFY(!results->isVisible());

    search->setFocus();
    QInputMethodEvent kana;
    kana.setCommitString("す");
    QCoreApplication::sendEvent(search, &kana);
    QCOMPARE(canvas->tileCount(), 2);
    QCOMPARE(canvas->tileName(0), QString("すし"));
    QCOMPARE(canvas->tileName(1), QString("休む"));
    QCOMPARE(results->text(), QString("2 of 8 words"));
    QVERIFY(search->hasFocus());
    QCoreApplication::processEvents();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->tileRect(1).center());
    QCOMPARE(canvas->expandedTile(), 1);
    QVERIFY(canvas->tileDescription(1).contains("やすむ"));
    QVERIFY(canvas->tileDescription(1).contains("rest"));

    search->setFocus();
    QInputMethodEvent moreKana;
    moreKana.setCommitString("し");
    QCoreApplication::sendEvent(search, &moreKana);
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("すし"));
    QCOMPARE(canvas->expandedTile(), -1);
    QTest::keyClick(search, Qt::Key_Backspace);
    QCOMPARE(canvas->tileCount(), 2);

    // Refreshes keep the active query and remove newly nonmatching tiles.
    state.tiles[1].reading = "やめる";
    page.setState(state);
    QCOMPARE(search->text(), QString("す"));
    QCOMPARE(canvas->tileCount(), 1);
    search->setText("休");
    QCOMPARE(canvas->tileName(0), QString("休む"));
    search->setText(" RIV ");
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("川"));
    search->setText("ＲＩＶ");
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("川"));
    search->setText("no such word");
    QCOMPARE(canvas->tileCount(), 0);
    QVERIFY(!canvas->isVisible());
    QVERIFY(empty->isVisible());
    QVERIFY(empty->text().contains("No matching words"));
    QCOMPARE(results->text(), QString("0 of 8 words"));
    auto *clear = search->findChild<QAbstractButton *>();
    QVERIFY(clear && clear->isVisible());
    QTest::mouseClick(clear, Qt::LeftButton);
    QVERIFY(search->text().isEmpty());
    QCOMPARE(canvas->tileCount(), state.tiles.size());
    QVERIFY(canvas->isVisible());
    QVERIFY(!empty->isVisible());
    QVERIFY(!results->isVisible());
    page.setState({});
    QVERIFY(empty->isVisible());
    QVERIFY(empty->text().contains("No word tiles yet"));
  }
  void wordsSearchMatchesRomajiReadingSubstrings() {
    ProgressionPage page;
    auto state = progressionFixture();
    state.tiles[0].characters = "宇宙";
    state.tiles[0].reading = "うちゅう";
    state.tiles[0].meaning = "universe";
    state.tiles[1].characters = "中学校";
    state.tiles[1].reading = "ちゅうがっこう";
    state.tiles[1].meaning = "middle school";
    state.tiles[2].characters = "チューブ";
    state.tiles[2].reading = "チューブ";
    state.tiles[2].meaning = "tube";
    page.setState(state);
    auto *search = page.findChild<QLineEdit *>("wordSearch");
    auto *canvas = page.findChild<WordTileCanvas *>();
    QVERIFY(search && canvas);

    // Ordinary keyboard input must find kana substrings without submitting.
    QTest::keyClicks(search, "chu");
    QCOMPARE(search->text(), QString("chu"));
    QCOMPARE(canvas->tileCount(), 3);
    QCOMPARE(canvas->tileName(0), QString("宇宙"));
    QCOMPARE(canvas->tileName(1), QString("中学校"));
    QCOMPARE(canvas->tileName(2), QString("チューブ"));
    for (const QString &query : {QString("CHU"), QString("ｃｈｕ"), QString("tyu"),
                                QString("ちゅ"), QString("チュ"), QString("ﾁｭ")}) {
      search->setText(query);
      QCOMPARE(canvas->tileCount(), 3);
    }
    search->setText("chuu");
    QCOMPARE(canvas->tileCount(), 2);
    search->setText("uchuu");
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("宇宙"));
    search->setText("SCHOOL");
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("中学校"));
    search->setText("中");
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("中学校"));
    search->setText("ちゆ");
    QCOMPARE(canvas->tileCount(), 0); // Small ゅ remains distinct from ゆ.
    search->clear();
    QCOMPARE(canvas->tileCount(), state.tiles.size());
  }
  void wordsSearchFiltersWaniKaniTags() {
    ProgressionPage page;
    auto state = progressionFixture();
    state.tiles[0].subjectKind = "kanji";
    state.tiles[2].subjectKind = "kana_vocabulary";
    state.tiles[3].subjectKind = "radical";
    state.tiles[4].subjectKind.clear();
    for (int i = 5; i < state.tiles.size(); ++i) state.tiles[i].source = "anki";
    // Literal matches on untagged sources must not bypass a requested tag.
    state.tiles[5].meaning = "vocab";
    state.tiles[6].subjectKind = "kanji";
    state.tiles[6].meaning = "kanji";
    state.tiles[6].reading = "かんじ";
    page.setState(state);
    auto *search = page.findChild<QLineEdit *>("wordSearch");
    auto *canvas = page.findChild<WordTileCanvas *>();
    QVERIFY(search && canvas);

    QTest::keyClicks(search, "kanji");
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("猫"));
    search->setText(" KANJI ");
    QCOMPARE(canvas->tileCount(), 1);
    QCOMPARE(canvas->tileName(0), QString("猫"));
    for (const QString &query : {QString("vocab"), QString("VOCAB"),
                                QString("ｖｏｃａｂ"), QString("vocabulary")}) {
      search->setText(query);
      QCOMPARE(canvas->tileCount(), 2);
      QCOMPARE(canvas->tileName(0), QString("犬"));
      QCOMPARE(canvas->tileName(1), QString("山"));
    }
    state.tiles[0].subjectKind = "vocabulary";
    page.setState(state);
    QCOMPARE(canvas->tileCount(), 3);
    search->setText("kanji");
    QCOMPARE(canvas->tileCount(), 0);
    search->clear();
    QCOMPARE(canvas->tileCount(), state.tiles.size());
  }
  void progressionCompactTilesDoNotClaimKeyboardFocus() {
    WordTileCanvas canvas;
    canvas.setCompact(true);
    canvas.setTiles(progressionFixture().tiles);
    QCOMPARE(canvas.focusPolicy(), Qt::NoFocus);
    QCOMPARE(canvas.tileCount(), 6);
    auto *accessible = QAccessible::queryAccessibleInterface(&canvas);
    QVERIFY(accessible);
    QCOMPARE(accessible->childCount(), 6);
    QVERIFY(!accessible->child(6));
    const int original = canvas.focusedTile();
    canvas.focusTile(3);
    QCOMPARE(canvas.focusedTile(), original);
    QTest::keyClick(&canvas, Qt::Key_End);
    QCOMPARE(canvas.focusedTile(), original);
    for (int i = 0; i < accessible->childCount(); ++i) {
      auto *tile = accessible->child(i);
      QVERIFY(!tile->state().focusable);
      QVERIFY(tile->actionInterface()->actionNames().isEmpty());
      tile->actionInterface()->doAction(QAccessibleActionInterface::setFocusAction());
      QCOMPARE(canvas.focusedTile(), original);
    }
  }
  void progressionCardResetsStaleChallengeAndDoesNotLeakAnswers() {
    Card card;
    Challenge challenge;
    challenge.subject = demoSubjects().first();
    challenge.subject.characters = "犬";
    challenge.subject.readings = {"いぬ"};
    challenge.subject.meanings = {"dog"};
    challenge.mode = Mode::Recall;
    challenge.reading = true;
    card.present(challenge, 5);
    card.setProgressTile(progressionFixture().tiles[1]);
    auto *mark = card.findChild<QWidget *>("wordChallengeMark");
    auto *progress = card.findChild<QLabel *>("wordProgress");
    QVERIFY(mark && progress);
    QVERIFY(!mark->isHidden());
    QVERIFY(!progress->isHidden());
    QCOMPARE(mark->focusPolicy(), Qt::NoFocus);
    auto *kind = card.findChild<QLabel *>("wordKind");
    auto *stage = card.findChild<QFrame *>("wordStage");
    QVERIFY(kind && stage);
    QVERIFY(!kind->isHidden());
    QCOMPARE(kind->text(), QString("Vocab"));
    QCOMPARE(stage->property("subjectKind").toString(), QString("vocabulary"));
    QVERIFY(mark->width() < 30);
    for (const auto &text : {mark->toolTip(), mark->accessibleName(), mark->accessibleDescription(),
                             progress->toolTip(), progress->text()}) {
      QVERIFY(!text.contains("犬"));
      QVERIFY(!text.contains("いぬ"));
      QVERIFY(!text.contains("dog", Qt::CaseInsensitive));
    }
    challenge.subject.characters = "山";
    challenge.subject.readings = {"やま"};
    challenge.subject.meanings = {"mountain"};
    challenge.subject.kind = "kanji";
    card.present(challenge, 5);
    QCOMPARE(kind->text(), QString("Kanji"));
    QCOMPARE(stage->property("subjectKind").toString(), QString("kanji"));
    QVERIFY(mark->isHidden());
    QVERIFY(progress->isHidden());
    card.setProgressTile(progressionFixture().tiles[2]);
    QVERIFY(mark->isHidden());
    QVERIFY(progress->isHidden());
    card.setProgressTile(progressionFixture().tiles[0]);
    QVERIFY(mark->isHidden());
    QVERIFY(!progress->isHidden());
    QVERIFY(progress->text().contains("Settled"));
    card.setProgressTile(std::nullopt);
    QVERIFY(mark->isHidden());
    QVERIFY(progress->isHidden());
    challenge.subject.source = "anki";
    card.present(challenge, 5);
    QVERIFY(kind->isHidden());
    QVERIFY(stage->property("subjectKind").toString().isEmpty());
    card.hide();
  }
  void progressionAppClearingShowsOnePassiveRecapAcrossSessions_data() {
    QTest::addColumn<bool>("failSecondSave");
    QTest::newRow("same-day repeat stays quiet") << false;
    QTest::newRow("failed save cancels deferred recap") << true;
  }
  void progressionAppClearingShowsOnePassiveRecapAcrossSessions() {
    QFETCH(bool, failSecondSave);
    QTemporaryDir dir;
    Store store(dir.path());
    QVERIFY(store.error.isEmpty());
    const QJsonObject word{{"characters", "日"}, {"level", 1},
      {"meanings", QJsonArray{QJsonObject{{"meaning", "sun"}, {"primary", true}, {"accepted_answer", true}}}},
      {"readings", QJsonArray{QJsonObject{{"reading", "ひ"}, {"primary", true}, {"accepted_answer", true}, {"type", "onyomi"}}}}};
    QVERIFY(store.saveCache({
      {"subjects", QJsonArray{QJsonObject{{"id", 1}, {"object", "kanji"}, {"data", word}}}},
      {"assignments", QJsonArray{QJsonObject{{"data", QJsonObject{
        {"subject_id", 1}, {"srs_stage", 1}, {"started_at", "2026-01-01T00:00:00Z"}}}}}},
      {"synced_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}));
    QSqlQuery query(store.db);
    for (int day = -5; day < 0; ++day) {
      const QDate date = QDate::currentDate().addDays(day);
      query.prepare("INSERT INTO attempts(subject_id,characters,mode,dimension,correct,active_ms,created_at,local_day,event_json) "
                    "VALUES(1,'日',?,'Reading',?,0,?,?,?)");
      query.addBindValue(modeName(Mode::Typing));
      query.addBindValue(day >= -2 ? 1 : 0);
      query.addBindValue(date.toString(Qt::ISODate) + "T12:00:00.000Z");
      query.addBindValue(date.toString(Qt::ISODate));
      query.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{
        {"accepted_readings", QJsonArray{"ひ"}},
        {"accepted_meanings", QJsonArray{"sun"}}}).toJson(QJsonDocument::Compact)));
      QVERIFY(query.exec());
    }
    QSettings saved(dir.path() + "/settings.ini", QSettings::IniFormat);
    saved.setValue("practice/source", "wanikani");
    saved.setValue("session_count", 1);
    saved.setValue("recall_share", 0);
    saved.setValue("typed_share", 100);
    saved.setValue("speech/enabled", false);
    saved.setValue("sound_enabled", false);
    saved.setValue("reduced_motion", true);
    saved.sync();
    QVERIFY(!QFile::exists(dir.path() + "/token"));
    App app(store, dir.path());
    app.start(false, true);
    app.showDashboard();
    QWidget *dashboard = nullptr;
    Card *card = nullptr;
    ProgressRecap *recap = nullptr;
    for (auto *window : QApplication::topLevelWidgets()) {
      if (window->findChild<QTabWidget *>("dashboardTabs")) dashboard = window;
      if (window->objectName() == "practiceCard") card = qobject_cast<Card *>(window);
      if (window->objectName() == "progressionRecap") recap = static_cast<ProgressRecap *>(window);
    }
    QVERIFY(dashboard && recap);
    auto *begin = dashboard->findChild<QPushButton *>("studioBegin");
    QVERIFY(begin);
    begin->click();
    // App owns both the real and preview cards; select the visible real card.
    for (auto *window : QApplication::topLevelWidgets())
      if (auto *candidate = qobject_cast<Card *>(window); candidate && candidate->isVisible())
        card = candidate;
    QVERIFY(card && card->isVisible());
    QCOMPARE(card->challenge.subject.id, 1);
    QCOMPARE(card->challenge.subject.source, QString("wanikani"));
    QCOMPARE(card->challenge.mode, Mode::Typing);
    QVERIFY(card->challenge.reading);
    auto *mark = card->findChild<QWidget *>("wordChallengeMark");
    auto *input = card->findChild<QLineEdit *>("readingInput");
    auto *done = card->findChild<QPushButton *>("finishCard");
    QVERIFY(mark && input && done);
    QVERIFY(!mark->isHidden());
    QVERIFY(!recap->isVisible());
    QSignalSpy completed(card, &Card::completed);
    input->setText("ひ");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!done->isHidden());
    done->click();
    QCOMPARE(completed.count(), 1);
    QVERIFY(!card->isVisible());
    if (!failSecondSave) {
      QTRY_VERIFY_WITH_TIMEOUT(recap->isVisible(), 1000);
      QVERIFY(recap->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
      QVERIFY(recap->testAttribute(Qt::WA_ShowWithoutActivating));
      QCOMPARE(recap->findChild<QToolButton *>("recapClose")->focusPolicy(), Qt::NoFocus);
      QCOMPARE(recap->findChild<QLabel *>("recapTitle")->text(), QString("Session progress"));
      QVERIFY(recap->findChild<QLabel *>("recapMessage")->text().contains("日"));
    } else {
      QVERIFY(!recap->isVisible()); // First session's notice is still deferred.
    }
    QVERIFY(query.exec("SELECT data_json FROM progression_tiles WHERE subject_id=1"));
    QVERIFY(query.next());
    const auto tile = QJsonDocument::fromJson(query.value(0).toByteArray()).object();
    QVERIFY(!tile["reading_challenge"].toBool());
    QCOMPARE(tile["reading_clears"].toInt(), 1);
    query.prepare("SELECT COUNT(*) FROM attempts WHERE mode=? AND dimension='Reading' AND correct=1");
    query.addBindValue(modeName(Mode::Typing));
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    if (failSecondSave)
      QVERIFY(query.exec("CREATE TEMP TRIGGER reject_ui_attempt BEFORE INSERT ON attempts "
                         "BEGIN SELECT RAISE(ABORT, 'test answer rejected'); END"));
    begin->click();
    QVERIFY(card->isVisible());
    QVERIFY(!recap->isVisible());
    QVERIFY(mark->isHidden());
    input->setText("ひ");
    QTest::keyClick(input, Qt::Key_Return);
    done->click();
    QCOMPARE(completed.count(), 2);
    QTest::qWait(400); // Beyond App's deferred 200ms recap presentation.
    QVERIFY(!recap->isVisible());
    QVERIFY(query.exec("SELECT COUNT(*) FROM attempts"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), failSecondSave ? 6 : 7);
    if (failSecondSave) {
      QVERIFY(store.error.contains("test answer rejected"));
      QVERIFY(dashboard->isVisible());
    }
  }
  void localSpeechOnlyAfterRevealAndStableReplay() {
    JapaneseSpeech speech;
    if (!speech.available())
      QSKIP("This platform/build has no bundled Japanese speech assets.");
    speech.configure(true, "mei", 35, 1.0);
    QSignalSpy generated(&speech, &JapaneseSpeech::generated);
    Card card;
    card.setSpeech(&speech, true, false, true); // Missing recordings fall back locally.
    card.setAnkiAudio(false, 0);
    Challenge c;
    c.subject = demoSubjects().first();
    QTemporaryDir missingRecording;
    c.subject.source = "anki";
    c.subject.wordAudio = missingRecording.path() + "/missing.wav";
    c.subject.characters = "学校";
    c.subject.readings = {"がっこう", "different secondary reading"};
    c.mode = Mode::Recall;
    c.reading = false; // Meaning prompts still pronounce the accepted reading.
    auto *listen = card.findChild<QPushButton *>("listenPronunciation");
    QVERIFY(listen);
    card.present(c, 5);
    QVERIFY(listen->isHidden());
    QTest::qWait(120);
    QCOMPARE(generated.count(), 0);
    card.findChild<QPushButton *>("revealAnswer")->click();
    QVERIFY(listen->isVisible());
    QTest::qWait(120);
    QCOMPARE(generated.count(), 0); // Autoplay disabled, manual listen remains.
    listen->click();
    QTRY_COMPARE_WITH_TIMEOUT(generated.count(), 1, 10000);
    const auto original = generated.last()[0].toString();
    QCOMPARE(generated.last()[1].toString(), QString("mei"));
    QCOMPARE(card.findChild<QLabel *>("voiceCaption")->text(), QString("Mei"));
    listen->click();
    QCOMPARE(generated.count(), 2);
    QCOMPARE(generated.last()[0].toString(), original);
    speech.say("がっこう", "same-authoritative-kana");
    QCOMPARE(generated.count(), 3);
    QCOMPARE(generated.last()[0].toString(), original);
    card.setSpeech(&speech, true, true, false);
    c.subject.readings = {"おはようございます"};
    card.present(c, 5);
    card.findChild<QPushButton *>("revealAnswer")->click();
    speech.configure(true, "takumi", 35, 1.0);
    card.hide();
    QTest::qWait(200);
    QCOMPARE(generated.count(), 3); // Changing voice then hiding must cancel old speech.
    card.setSpeech(&speech, false, true, false);
    card.present(c, 5);
    card.findChild<QPushButton *>("revealAnswer")->click();
    QVERIFY(listen->isHidden());
    QCOMPARE(generated.count(), 3);
  }
  void settingsPreviewAndVoicePreferences() {
    QTemporaryDir dir;
    Store store(dir.path());
    App app(store, dir.path());
    app.showSettings();
    QWidget *dashboard = nullptr;
    for (auto *window : QApplication::topLevelWidgets())
      if (window->findChild<QTabWidget *>("dashboardTabs"))
        dashboard = window;
    QVERIFY(dashboard);
    dashboard->findChild<QPushButton *>("studioSettings0")->click();
    auto *monitor = dashboard->findChild<QComboBox *>("cardMonitor");
    auto *corner = dashboard->findChild<QComboBox *>("cardCorner");
    QVERIFY(monitor && corner);
    QVERIFY(monitor->itemText(1).startsWith("Display 1"));
    QCOMPARE(monitor->itemData(1).toString(), orderedDisplays().first()->name());
    corner->setCurrentIndex((corner->currentIndex() + 1) % 4);
    Card *preview = nullptr;
    for (auto *window : QApplication::topLevelWidgets())
      if (auto *card = qobject_cast<Card *>(window);
          card && card->isVisible() && card->windowTitle().contains("Settings preview"))
        preview = card;
    QVERIFY(preview);
    dashboard->findChild<QComboBox *>("previewChallengeMode")->setCurrentIndex(1);
    QCOMPARE(preview->challenge.mode, Mode::Typing);
    monitor->setCurrentIndex(1);
    QCOMPARE(preview->challenge.mode, Mode::Typing);
    QVERIFY(preview->findChild<QLabel *>("cardHint")->text().contains("Display 1"));
    QSignalSpy completed(preview, &Card::completed);
    preview->close();
    QCOMPARE(completed.count(), 1);
    QSqlQuery q(store.db);
    QVERIFY(q.exec("SELECT COUNT(*) FROM attempts"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    dashboard->findChild<QPushButton *>("identifyDisplays")->click();
    int identifiers = 0;
    for (auto *window : QApplication::topLevelWidgets()) {
      if (window->objectName() != "displayIdentifier")
        continue;
      ++identifiers;
      QVERIFY(window->testAttribute(Qt::WA_ShowWithoutActivating));
      QVERIFY(window->windowFlags().testFlag(Qt::WindowTransparentForInput));
      window->close();
    }
    QCOMPARE(identifiers, orderedDisplays().size());
    dashboard->findChild<QPushButton *>("studioSettings4")->click();
    auto *voice = dashboard->findChild<QComboBox *>("speechVoice");
    auto *engine = dashboard->findChild<QComboBox *>("speechEngine");
    QVERIFY(voice && engine);
    if (JapaneseSpeech().available("quality")) {
      QCOMPARE(engine->currentData().toString(), QString("quality"));
      voice->setCurrentIndex(voice->findData("itako"));
      QVERIFY(dashboard->findChild<QLabel *>("speechVoiceCredits")->isVisible());
      if (engine->findData("fast") >= 0) {
        engine->setCurrentIndex(engine->findData("fast"));
        voice->setCurrentIndex(voice->findData("takumi"));
        engine->setCurrentIndex(engine->findData("quality"));
        QCOMPARE(voice->currentData().toString(), QString("itako"));
      }
    }
    // Builds with only Quality speech (Windows) do not offer Fast.
    const bool fastOffered = engine->findData("fast") >= 0;
    const QString savedEngine = fastOffered ? "fast" : "quality";
    const QString savedVoice = fastOffered ? "takumi" : "takehiro";
    engine->setCurrentIndex(engine->findData(savedEngine));
    voice->setCurrentIndex(voice->findData(savedVoice));
    QCOMPARE(dashboard->findChild<QLabel *>("speechVoiceCredits")->isVisible(), !fastOffered);
    dashboard->findChild<QSlider *>("speechVolume")->setValue(20);
    dashboard->findChild<QSlider *>("speechRate")->setValue(85);
    dashboard->findChild<QCheckBox *>("speechAutoplay")->setChecked(false);
    dashboard->findChild<QCheckBox *>("speechEnabled")->setChecked(false);
    QSettings saved(dir.path() + "/settings.ini", QSettings::IniFormat);
    QCOMPARE(saved.value("speech/voice").toString(), savedVoice);
    QCOMPARE(saved.value("speech/engine").toString(), savedEngine);
    QCOMPARE(saved.value("speech/volume").toInt(), 20);
    QCOMPARE(saved.value("speech/rate").toInt(), 85);
    QCOMPARE(saved.value("speech/autoplay").toBool(), false);
    QCOMPARE(saved.value("speech/enabled").toBool(), false);
    QVERIFY(!dashboard->findChild<QPushButton *>("speechPreview")->isVisible());
  }
  void gradientDitheringIsSubtleAndStable() {
    const auto first = printInkWash(QSize(1024, 1024), QColor("#202020"), true);
    QCOMPARE(first, printInkWash(QSize(1024, 1024), QColor("#202020"), true));
    int minimum = 255, maximum = 0;
    for (int x = 100; x < 116; ++x) {
      const auto pixel = first.pixelColor(x, 128);
      QCOMPARE(pixel.red(), pixel.green());
      QCOMPARE(pixel.green(), pixel.blue());
      minimum = qMin(minimum, pixel.red()); maximum = qMax(maximum, pixel.red());
      if (x > 100)
        QVERIFY(qAbs(pixel.red() - first.pixelColor(x - 1, 128).red()) <= 1);
    }
    // This nearly-flat patch mixes neighboring values instead of a broad band.
    QCOMPARE(maximum - minimum, 1);
    QVERIFY(printInkWash(QSize(), QColor("#202020"), true).isNull());
  }
  void mojiNookWordmark() {
    const auto icon = appIcon();
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
      QVERIFY(icon.availableSizes().contains(QSize(size, size)));
      QVERIFY(!icon.pixmap(size, size).isNull());
    }
    Card card;
    QVERIFY(card.findChild<QPushButton *>("cardBrand")->text().startsWith("Moji Nook"));
    const auto path = qEnvironmentVariable("MOJI_NOOK_ICON_PREVIEW");
    if (!path.isEmpty()) {
      QPixmap sheet(480, 160); sheet.fill(QColor("#f5f5f5"));
      QPainter painter(&sheet);
      int x = 12;
      for (int size : {16, 22, 32, 48, 64, 128}) {
        painter.drawPixmap(x, 16, icon.pixmap(size, size));
        painter.setPen(Qt::black);
        painter.drawText(x, 154, QString::number(size));
        x += size + 12;
      }
      painter.end();
      QVERIFY(sheet.save(path));
    }
  }
  void sourceSelectionUsesOnlyChosenCache() {
    QTemporaryDir dir;
    Store store(dir.path());
    AnkiSource source(store);
    QVERIFY(source.error.isEmpty());
    QJsonObject word{{"characters", "日"}, {"level", 1},
                     {"meanings", QJsonArray{QJsonObject{{"meaning", "sun"},
                        {"primary", true}, {"accepted_answer", true}}}},
                     {"readings", QJsonArray{QJsonObject{{"reading", "にち"},
                        {"primary", true}, {"accepted_answer", true},
                        {"type", "onyomi"}}}}};
    QVERIFY(store.saveCache({
      {"subjects", QJsonArray{QJsonObject{{"id", 1}, {"object", "kanji"}, {"data", word}}}},
      {"assignments", QJsonArray{QJsonObject{{"data", QJsonObject{
        {"subject_id", 1}, {"srs_stage", 1}, {"started_at", "2026-01-01T00:00:00Z"}}}}}},
      {"synced_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}));
    auto field = [](const QString &value) { return QJsonObject{{"value", value}}; };
    QJsonObject ankiCard{{"cardId", 42}, {"note", 42}, {"ord", 0},
      {"deckName", "Test deck"}, {"queue", 2}, {"type", 2},
      {"reps", 10}, {"interval", 7},
      {"fields", QJsonObject{{"Word", field("猫")}, {"Word Reading", field("ねこ")},
                            {"Word Meaning", field("cat")}}}};
    QSqlQuery q(store.db);
    q.prepare("INSERT INTO anki_items(profile,card_id,deck,data_json,reviews_json) VALUES(?,?,?,?,?)");
    q.addBindValue("Test profile");
    q.addBindValue("42");
    q.addBindValue("Test deck");
    q.addBindValue(QString::fromUtf8(QJsonDocument(ankiCard).toJson()));
    q.addBindValue("[]");
    QVERIFY(q.exec());
    q.prepare("INSERT INTO anki_meta(key,data_json) VALUES('snapshot',?)");
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{
      {"profile", "Test profile"}, {"synced_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson()));
    QVERIFY(q.exec());
    QSettings saved(dir.path() + "/settings.ini", QSettings::IniFormat);
    saved.setValue("anki/profile", "Test profile");
    saved.setValue("anki/decks", QStringList{"Test deck"});
    saved.setValue("anki/enabled", true);
    saved.setValue("practice/source", "wanikani");
    saved.setValue("session_count", 1);
    saved.setValue("recall_share", 100);
    saved.setValue("typed_share", 0);
    saved.setValue("sound_enabled", false);
    saved.setValue("reduced_motion", true);
    saved.sync();
    App app(store, dir.path());
    app.start(false, true);
    app.showSettings();
    QWidget *dashboard = nullptr;
    for (auto *window : QApplication::topLevelWidgets())
      if (window->findChild<QTabWidget *>("dashboardTabs"))
        dashboard = window;
    QVERIFY(dashboard);
    dashboard->findChild<QPushButton *>("studioSettings0")->click();
    auto *selector = dashboard->findChild<QComboBox *>("practiceSource");
    auto *warning = dashboard->findChild<QLabel *>("practiceSourceWarning");
    auto *share = dashboard->findChild<QSlider *>("ankiShare");
    QVERIFY(selector && warning && share);
    for (const auto &selection : {QString("wanikani"), QString("anki"), QString("both")}) {
      selector->setCurrentIndex(selector->findData(selection));
      QCOMPARE(warning->isHidden(), selection != "both");
      QCOMPARE(share->parentWidget()->isHidden(), selection != "both");
      if (selection == "both" && qEnvironmentVariableIsSet("MOJI_NOOK_SOURCE_PREVIEW")) {
        QCoreApplication::processEvents();
        QVERIFY(dashboard->grab().save(qEnvironmentVariable("MOJI_NOOK_SOURCE_PREVIEW")));
      }
      share->setValue(100);
      dashboard->findChild<QPushButton *>("studioBegin")->click();
      Card *active = nullptr;
      for (auto *window : QApplication::topLevelWidgets())
        if (auto *card = qobject_cast<Card *>(window); card && card->isVisible())
          active = card;
      QVERIFY(active);
      QCOMPARE(active->challenge.subject.source,
               selection == "wanikani" ? QString("wanikani") : QString("anki"));
      active->findChild<QPushButton *>("revealAnswer")->click();
      active->findChild<QPushButton *>("markRemembered")->click();
      saved.sync();
      QCOMPARE(saved.value("practice/source").toString(), selection);
    }
    QCOMPARE(source.subjects("Test profile", {"Test deck"}).size(), 1);
    QCOMPARE(store.cache()["subjects"].toArray().size(), 1);
    QVERIFY(q.exec("SELECT COUNT(*) FROM attempts WHERE correct=1"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 3);
    // Choosing a source with no cached words must not silently use the other.
    QVERIFY(store.saveCache({}));
    selector->setCurrentIndex(selector->findData("wanikani"));
    dashboard->findChild<QPushButton *>("studioBegin")->click();
    for (auto *window : QApplication::topLevelWidgets())
      if (auto *card = qobject_cast<Card *>(window))
        QVERIFY(!card->isVisible());
    QCOMPARE(source.subjects("Test profile", {"Test deck"}).size(), 1);
  }
  void sourceMigrationPreservesExistingIntegrations_data() {
    QTest::addColumn<bool>("ankiEnabled");
    QTest::addColumn<bool>("wkCached");
    QTest::addColumn<QString>("expected");
    QTest::newRow("new profile") << false << false << QString("wanikani");
    QTest::newRow("Anki only") << true << false << QString("anki");
    QTest::newRow("existing mixed profile") << true << true << QString("both");
  }
  void sourceMigrationPreservesExistingIntegrations() {
    QFETCH(bool, ankiEnabled);
    QFETCH(bool, wkCached);
    QFETCH(QString, expected);
    QTemporaryDir dir;
    Store store(dir.path());
    if (wkCached)
      QVERIFY(store.saveCache({{"subjects", QJsonArray{QJsonObject{{"id", 1}}}}}));
    QSettings saved(dir.path() + "/settings.ini", QSettings::IniFormat);
    saved.setValue("anki/enabled", ankiEnabled);
    saved.sync();
    {
      App app(store, dir.path());
      saved.sync();
      QCOMPARE(saved.value("practice/source").toString(), expected);
      QCOMPARE(saved.value("anki/enabled").toBool(), ankiEnabled);
    }
    {
      App app(store, dir.path());
      saved.sync();
      QCOMPARE(saved.value("practice/source").toString(), expected);
    }
  }
  void optionalLiveAnkiAudio() {
    const auto path = qEnvironmentVariable("MOJI_NOOK_ANKI_AUDIO_DATA");
    if (path.isEmpty())
      QSKIP("Set MOJI_NOOK_ANKI_AUDIO_DATA for an explicit local playback check.");
    QVERIFY(QGuiApplication::platformName() != "offscreen");
    Store store(path);
    AnkiSource source(store);
    QSettings settings(path + "/settings.ini", QSettings::IniFormat);
    const auto pool =
        source.subjects(settings.value("anki/profile").toString(),
                        settings.value("anki/decks").toStringList());
    QVERIFY(!pool.isEmpty());
    QVERIFY(!pool.first().wordAudio.isEmpty());
    AnkiDetails details;
    details.setSubject(pool.first());
    details.configure(true, 20);
    details.reveal();
    QTRY_VERIFY(details.findChild<QMediaPlayer *>());
    auto *player = details.findChild<QMediaPlayer *>();
    QTRY_VERIFY(player->playbackState() == QMediaPlayer::PlayingState);
    QCOMPARE(player->error(), QMediaPlayer::NoError);
    details.configure(false, 0);
    QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
  }
  void ankiIsOptionalAndCompact() {
    QTemporaryDir dir;
    Store store(dir.path());
    App app(store, dir.path());
    app.showSettings();
    QWidget *dashboard = nullptr;
    for (auto *w : QApplication::topLevelWidgets())
      if (w->findChild<QTabWidget *>("dashboardTabs"))
        dashboard = w;
    QVERIFY(dashboard);
    auto *source = dashboard->findChild<QComboBox *>("practiceSource");
    QVERIFY(source);
    QCOMPARE(source->currentData().toString(), QString("wanikani"));
    source->setCurrentIndex(source->findData("anki"));
    dashboard->findChild<QPushButton *>("studioSettings3")->click();
    QVERIFY(dashboard->findChild<QListWidget *>("ankiDecks")->isVisible());
    Card card;
    card.setReducedMotion(true);
    Challenge c;
    c.subject.source = "anki";
    c.subject.id = -1;
    c.subject.characters = "出来る";
    c.subject.meanings = {"to be able to do"};
    c.subject.readings = {"できる"};
    c.subject.deck = "Kaishi 1.5k";
    c.subject.contextJa = "出来ます。";
    c.subject.contextEn = "It can be done.";
    c.subject.notes = "A short note.";
    c.subject.sourceData = {{"type", 2}, {"interval_days", 7}};
    c.mode = Mode::Recall;
    c.reading = true;
    QSignalSpy done(&card, &Card::completed);
    card.present(c, 5);
    QCoreApplication::processEvents();
    QVERIFY(card.width() <= 400);
    QVERIFY(!card.findChild<AnkiDetails *>()->isVisible());
    QVERIFY(card.findChild<QLabel *>("cardMeta")->text().contains("Anki"));
    QVERIFY(!card.findChild<QLabel *>("cardMeta")->text().contains("Guru"));
    auto *frontContext = card.findChild<QLabel *>("ankiFrontContext");
    QVERIFY(frontContext->isVisible());
    QCOMPARE(frontContext->text(), "出来ます。");
    QVERIFY(!frontContext->text().contains("It can be done."));
    card.findChild<QPushButton *>("revealAnswer")->click();
    QVERIFY(card.findChild<AnkiDetails *>()->isVisible());
    QVERIFY(!card.findChild<QPushButton *>("exampleToggle")->isVisible());
    card.findChild<QPushButton *>("ankiShowDetails")->click();
    QCoreApplication::processEvents();
    QCOMPARE(done.size(), 0);
    card.findChild<QPushButton *>("markRemembered")->click();
    QCOMPARE(done.size(), 1);
    c.subject.source = "wanikani";
    card.present(c, 5);
    QVERIFY(!frontContext->isVisible());
    QVERIFY(!card.findChild<AnkiDetails *>()->isVisible());
  }
  void countdownDoesNotSkipSeconds() {
    QTemporaryDir dir;
    Store store(dir.path());
    App app(store, dir.path());
    app.start(true, true);
    QWidget *dashboard = nullptr;
    for (auto *w : QApplication::topLevelWidgets())
      if (w->findChild<QTabWidget *>("dashboardTabs"))
        dashboard = w;
    QVERIFY(dashboard);
    dashboard->findChild<QPushButton *>("studioBegin")->click();
    Card *active = nullptr;
    for (auto *w : QApplication::topLevelWidgets())
      if (auto *c = qobject_cast<Card *>(w); c && c->isVisible())
        active = c;
    QVERIFY(active);
    active->completed(active->challenge, 1, 100);
    active->completed(active->challenge, 1, 100);
    auto *schedule = dashboard->findChild<QLabel *>("practiceSchedule");
    QVERIFY(schedule);
    int previous = -1, changes = 0;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3500) {
      QTest::qWait(40);
      const auto match = QRegularExpression("Next practice in (\\d+):(\\d+)")
                             .match(schedule->text());
      if (!match.hasMatch())
        continue;
      const int now =
          match.captured(1).toInt() * 60 + match.captured(2).toInt();
      if (previous >= 0 && now != previous) {
        QCOMPARE(previous - now, 1);
        ++changes;
      }
      previous = now;
    }
    QVERIFY(changes >= 2);
  }
  void answersDoNotRebuildHiddenDashboard() {
    QTemporaryDir dir;
    Store store(dir.path());
    App app(store, dir.path());
    app.start(true, true);
    QWidget *dashboard = nullptr;
    for (auto *w : QApplication::topLevelWidgets())
      if (w->findChild<QTabWidget *>("dashboardTabs"))
        dashboard = w;
    QVERIFY(dashboard);
    dashboard->hide();
    const int before = dashboard->property("statsRenderCount").toInt();
    dashboard->findChild<QPushButton *>("studioBegin")->click();
    Card *active = nullptr;
    for (auto *w : QApplication::topLevelWidgets())
      if (auto *c = qobject_cast<Card *>(w); c && c->isVisible())
        active = c;
    QVERIFY(active);
    active->completed(active->challenge, 1, 100);
    active->completed(active->challenge, 1, 100);
    QCoreApplication::processEvents();
    QCOMPARE(dashboard->property("statsRenderCount").toInt(), before);
    app.showDashboard();
    QVERIFY(dashboard->property("statsRenderCount").toInt() > before);
    auto *tabs = dashboard->findChild<QTabWidget *>("dashboardTabs");
    tabs->setCurrentIndex(2);
    auto *history = tabs->currentWidget()->findChild<QTextBrowser *>();
    QVERIFY(history);
    QVERIFY(history->toPlainText().contains("Correct"));
  }
  void themeCatalogMigration() {
    QTemporaryDir freshDir;
    Store freshStore(freshDir.path());
    {
      App app(freshStore, freshDir.path());
      QSettings fresh(freshDir.path() + "/settings.ini", QSettings::IniFormat);
      QCOMPARE(fresh.value("theme").toInt(), 3);
      QCOMPARE(fresh.value("theme_catalog_version").toInt(), 3);
    }
    QCOMPARE(migratedTheme(4), 0);
    QCOMPARE(migratedTheme(5), 1);
    QCOMPARE(migratedTheme(8), 2);
    QCOMPARE(migratedTheme(9), 3);
    QCOMPARE(migratedTheme(6), 0);
    QCOMPARE(migratedTheme(7), 1);
    QTemporaryDir dir;
    QSettings saved(dir.path() + "/settings.ini", QSettings::IniFormat);
    saved.setValue("theme", 9);
    saved.sync();
    Store store(dir.path());
    {
      App app(store, dir.path());
      QCOMPARE(saved.value("theme").toInt(), 1);
      QCOMPARE(saved.value("theme_catalog_version").toInt(), 3);
    }
    {
      App app(store, dir.path());
      QCOMPARE(saved.value("theme").toInt(), 1);
    }
  }
  void historyWordsStayTogether() {
    QTemporaryDir dir;
    Store store(dir.path());
    Challenge c;
    c.subject.id = 1;
    c.subject.characters = "分かる";
    c.reading = true;
    c.mode = Mode::Choice;
    QVERIFY(store.record(c, true, 100));
    c.subject.id = 2;
    c.subject.characters = "お疲れ様でした";
    QVERIFY(store.record(c, true, 100));
    QTextBrowser browser;
    browser.resize(680, 400);
    browser.setHtml(store.historyHtml());
    browser.show();
    QTest::qWait(30);
    for (const auto &word : {QString("分かる"), QString("お疲れ様でした")}) {
      auto cursor = browser.document()->find(word);
      QVERIFY(!cursor.isNull());
      QCOMPARE(cursor.block().layout()->lineCount(), 1);
      QCOMPARE(cursor.selectedText(), word);
    }
    QCOMPARE(browser.horizontalScrollBar()->maximum(), 0);
    if (qEnvironmentVariableIsSet("MOJI_NOOK_HISTORY_PREVIEW"))
      QVERIFY(
          browser.grab().save(qEnvironmentVariable("MOJI_NOOK_HISTORY_PREVIEW")));
  }
  void studioNavigation() {
    QTemporaryDir dir;
    Store store(dir.path());
    App app(store, dir.path());
    app.showDashboard();
    QTabWidget *tabs = nullptr;
    for (auto *w : QApplication::topLevelWidgets())
      if (auto *t = w->findChild<QTabWidget *>("dashboardTabs"))
        tabs = t;
    QVERIFY(tabs);
    QVERIFY(tabs->tabBar()->isHidden());
    auto *more = tabs->window()->findChild<QPushButton *>("studioMore");
    QVERIFY(more && more->menu());
    QCOMPARE(more->menu()->actions().size(), 3);
    for (int i = 0; i < 6; ++i) {
      if (i == 2) {
        auto *action = more->menu()->findChild<QAction *>(
            QString("studioNav%1").arg(i));
        QVERIFY(action);
        action->trigger();
        QVERIFY(more->isChecked());
        for (int main : {0, 1, 3, 4, 5})
          QVERIFY(!tabs->window()->findChild<QPushButton *>(
                       QString("studioNav%1").arg(main))->isChecked());
      } else {
        auto *nav = tabs->window()->findChild<QPushButton *>(
            QString("studioNav%1").arg(i));
        QVERIFY(nav && nav->isVisible());
        nav->click();
        QVERIFY(nav->isChecked());
        QVERIFY(!more->isChecked());
      }
      QCOMPARE(tabs->currentIndex(), i);
    }
    auto *appearance = tabs->findChild<QPushButton *>("studioSettings4");
    QVERIFY(appearance);
    appearance->click();
    QVERIFY(tabs->findChild<QSlider *>("soundVolume")->isVisible());
    tabs->findChild<QPushButton *>("studioSettings0")->click();
    QVERIFY(!tabs->findChild<QSlider *>("soundVolume")->isVisible());
    tabs->window()->findChild<QPushButton *>("studioNav0")->click();
    QVERIFY(
        tabs->window()->findChild<QPushButton *>("studioBegin")->isVisible());
    auto *night = tabs->window()->findChild<QCheckBox *>("nightMode");
    QVERIFY(night);
    night->setChecked(true);
    QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 128);
    QSettings saved(dir.path() + "/settings.ini", QSettings::IniFormat);
    QCOMPARE(saved.value("theme").toInt(), defaultTheme);
    night->setChecked(false);
    QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 128);
    QCOMPARE(saved.value("theme").toInt(), defaultTheme - 1);
    auto *selector = tabs->findChild<QComboBox *>("themeSelector");
    QCOMPARE(selector->count(), themeCount);
    for (int i = 0; i < themeCount; i += 2) {
      selector->setCurrentIndex(i);
      night->setChecked(true);
      QCOMPARE(selector->currentIndex(), i + 1);
      QCOMPARE(saved.value("theme").toInt(), i + 1);
      QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 128);
      night->setChecked(false);
      QCOMPARE(selector->currentIndex(), i);
      QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 128);
    }
    QVERIFY(tabs->window()
                ->findChild<QLabel *>("studioHero")
                ->text()
                .contains("No words available"));
  }
  void homeActions() {
    QTemporaryDir dir;
    QSettings prefs(dir.path() + "/settings.ini", QSettings::IniFormat);
    prefs.setValue("speech/enabled", false);
    prefs.setValue("sounds/enabled", false);
    prefs.sync();
    Store store(dir.path());
    App app(store, dir.path());
    app.showDashboard();
    QTabWidget *tabs = nullptr;
    for (auto *w : QApplication::topLevelWidgets())
      if (auto *t = w->findChild<QTabWidget *>("dashboardTabs")) tabs = t;
    QVERIFY(tabs);
    auto *window = tabs->window();
    auto *begin = window->findChild<QPushButton *>("studioBegin");
    auto *pause = window->findChild<QPushButton *>("pauseReminders");
    auto *snooze = window->findChild<QPushButton *>("homeSnooze");
    QVERIFY(begin && pause && snooze);
    QVERIFY(!pause->isVisible());
    begin->click();
    QCOMPARE(tabs->currentIndex(), 5);
    QVERIFY(window->findChild<QPushButton *>("studioSettings2")->isChecked());
    window->findChild<QPushButton *>("studioSettings0")->click();
    window->findChild<QCheckBox *>("advancedPracticeToggle")->setChecked(false);
    QVERIFY(window->findChild<QWidget *>("placementSettings")->isVisible());
    window->findChild<QPushButton *>("studioNav0")->click();
    window->findChild<QPushButton *>("homeWords")->click();
    QCOMPARE(tabs->currentIndex(), 3);
    window->findChild<QPushButton *>("studioNav0")->click();
    app.start(true, false);
    QTRY_COMPARE(begin->text(), QString("Practice now"));
    QVERIFY(pause->isVisible());
    auto *homeStats = window->findChild<QTextBrowser *>("homeStats");
    QVERIFY(homeStats);
    QTRY_COMPARE(homeStats->verticalScrollBar()->maximum(), 0);
    pause->click();
    QCOMPARE(pause->text(), QString("Resume reminders"));
    QCOMPARE(window->findChild<QLabel *>("studioHero")->text(), QString("Paused"));
    snooze->click();
    QCOMPARE(pause->text(), QString("Pause reminders"));
    QTRY_VERIFY(window->findChild<QLabel *>("practiceSchedule")->text().contains("29:5"));
    QCOMPARE(snooze->text(), QString("Snooze 30 minutes"));
    auto *snoozeLength = window->findChild<QSpinBox *>("snoozeMinutes");
    QVERIFY(snoozeLength);
    QCOMPARE(window->findChild<QSpinBox *>("sessionCount")->maximum(), 25);
    snoozeLength->setValue(10);
    QCOMPARE(snooze->text(), QString("Snooze 10 minutes"));
    snooze->click();
    QTRY_VERIFY(window->findChild<QLabel *>("practiceSchedule")->text().contains("in 9:5"));
    QSqlQuery q(store.db);
    QVERIFY(q.exec("SELECT COUNT(*) FROM attempts"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
  }
  void optionalSecondChances() {
    QTemporaryDir dir;
    Store store(dir.path());
    PracticeModes modes(store);
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Typing;
    c.reading = true;
    modes.setPool({c.subject});
    QVERIFY(modes.candidates().isEmpty());
    QVERIFY(store.record(c, false, 100));
    // Qt and SQLite clocks may differ at the freshly written millisecond
    // boundary.
    QTRY_COMPARE(modes.candidates().size(), 1);
    modes.show();
    modes.findChild<QPushButton *>("startSecondChances")->click();
    auto *input = modes.findChild<QLineEdit *>("modeReading");
    auto *check = modes.findChild<QPushButton *>("modeCheck");
    auto *next = modes.findChild<QPushButton *>("modeNext");
    check->click();
    QVERIFY(next->isHidden());
    input->setText(c.subject.reading());
    check->click();
    QVERIFY(!next->isHidden());
    QSqlQuery q(store.db);
    QVERIFY(q.exec("BEGIN"));
    QVERIFY(q.exec("DROP TABLE mode_events"));
    next->click();
    QCOMPARE(input->text(), c.subject.reading());
    QVERIFY(q.exec("ROLLBACK"));
    next->click();
    QVERIFY(q.exec("SELECT event_json FROM mode_events"));
    QVERIFY(q.next());
    QVERIFY(QJsonDocument::fromJson(q.value(0).toByteArray())
                .object()["correct"]
                .toBool());
    QVERIFY(q.exec("SELECT COUNT(*) FROM attempts"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    modes.setPool({});
    QVERIFY(modes.candidates().isEmpty());
  }
  void syncAgeFormatting() {
    const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
    QVERIFY(syncAgeText({}, now).contains("not synced yet"));
    QVERIFY(syncAgeText(now, now).contains("just now"));
    QVERIFY(syncAgeText(now.addSecs(-120), now).contains("2 min"));
    QVERIFY(syncAgeText(now.addSecs(-7200), now).contains("2 hr"));
    QVERIFY(syncAgeText(now.addDays(-3), now).contains("3 days"));
    QVERIFY(automaticSyncDue({}, {}, now));
    QVERIFY(!automaticSyncDue(now.addSecs(-86399), {}, now));
    QVERIFY(automaticSyncDue(now.addSecs(-86400), {}, now));
    QVERIFY(!automaticSyncDue({}, now.addSecs(-3599), now));
    QVERIFY(automaticSyncDue({}, now.addSecs(-3600), now));
    QVERIFY(!automaticSyncDue(now, now.addSecs(-3600), now));
  }
  void alternateReadingRetry() {
    Card card;
    Challenge c;
    c.subject.kind = "vocabulary";
    c.subject.characters = "足";
    c.subject.meanings = {"Foot"};
    c.subject.readings = {"あし"};
    c.subject.kanjiReadingTypes = {{"あし", "kunyomi"}, {"そく", "onyomi"}};
    c.mode = Mode::Typing;
    c.reading = true;
    QSignalSpy graded(&card, &Card::graded);
    QSignalSpy completed(&card, &Card::completed);
    card.present(c, 5);
    auto *input = card.findChild<QLineEdit *>("readingInput");
    auto *check = card.findChild<QPushButton *>("checkReading");
    auto *warning = card.findChild<QLabel *>("readingWarning");
    input->setText("soku");
    check->click();
    QCOMPARE(graded.count(), 0);
    QCOMPARE(completed.count(), 0);
    QVERIFY(input->isEnabled());
    QVERIFY(warning->isVisible());
    QVERIFY(warning->text().contains("kun’yomi"));
    QCOMPARE(card.challenge.event["reading_retries"].toArray().size(), 1);
    QVERIFY(!card.challenge.event.contains("graded_at"));
    input->setText("ashi");
    check->click();
    QCOMPARE(graded.count(), 1);
    QVERIFY(graded.first().first().toBool());
    QVERIFY(!warning->isVisible());
    card.setReadingRetries(false);
    card.present(c, 5);
    input->setText("soku");
    check->click();
    QCOMPARE(graded.count(), 2);
    QVERIFY(!graded.last().first().toBool());
    card.setReadingRetries(true);
    card.present(c, 5);
    input->setText("neko");
    check->click();
    QCOMPARE(graded.count(), 3);
    QVERIFY(!graded.last().first().toBool());
    c.subject.kind = "kanji";
    c.subject.readings = {"そく"};
    c.subject.readingKind = "onyomi";
    QVERIFY(c.readingRetryHint("アシ").contains("on’yomi"));
    QVERIFY(c.readingRetryHint("soku").isEmpty());
    c.mode = Mode::Choice;
    QVERIFY(c.readingRetryHint("ashi").isEmpty());
  }
  void dashboardNavigationAndFeedbackSettings() {
    QTemporaryDir directory;
    Store store(directory.path());
    App app(store, directory.path());
    app.showDashboard();
    QTabWidget *tabs = nullptr;
    for (auto *window : QApplication::topLevelWidgets())
      if (auto *found = window->findChild<QTabWidget *>("dashboardTabs"))
        tabs = found;
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 6);
    const QStringList expected = {"Practice",   "Insights", "History",
                                  "Word tiles", "Modes",    "Settings"};
    for (int i = 0; i < expected.size(); ++i)
      QCOMPARE(tabs->tabText(i), expected[i]);
    auto *settingsNav = tabs->window()->findChild<QPushButton *>("studioNav5");
    QVERIFY(settingsNav && settingsNav->isVisible());
    settingsNav->click();
    QCOMPARE(tabs->currentIndex(), 5);
    tabs->setCurrentIndex(0);
    app.showSettings();
    QCOMPARE(tabs->currentIndex(), 5);
    auto *advanced = tabs->findChild<QWidget *>("advancedPractice");
    auto *advancedToggle =
        tabs->findChild<QCheckBox *>("advancedPracticeToggle");
    QVERIFY(advanced && advancedToggle);
    QVERIFY(advanced->isHidden());
    advancedToggle->setChecked(true);
    QVERIFY(!advanced->isHidden());
    advancedToggle->setChecked(false);
    auto *body = tabs->findChild<QWidget *>("settingsBody");
    QVERIFY(body);
    tabs->window()->resize(1600, 1000);
    QTest::qWait(30);
    QCOMPARE(body->property("columns").toInt(), 1);
    tabs->window()->resize(800, 800);
    QTest::qWait(30);
    QCOMPARE(body->property("columns").toInt(), 1);
    auto *sound = tabs->findChild<QCheckBox *>("soundEnabled");
    auto *volume = tabs->findChild<QSlider *>("soundVolume");
    auto *motion = tabs->findChild<QCheckBox *>("reducedMotion");
    QVERIFY(sound);
    QVERIFY(volume);
    QCOMPARE(volume->maximum(), 100);
    volume->setValue(75);
    auto *recent = tabs->findChild<QSlider *>("recentShare");
    auto *persistent = tabs->findChild<QSlider *>("persistentShare");
    auto *recall = tabs->findChild<QSlider *>("recallShare");
    auto *typed = tabs->findChild<QSlider *>("typedShare");
    QVERIFY(recent && persistent && recall && typed);
    auto *newest = tabs->findChild<QSlider *>("newestShare");
    QVERIFY(newest);
    QCOMPARE(newest->value(), 30);
    QCOMPARE(recent->value(), 25);
    QCOMPARE(persistent->value(), 20);
    newest->setValue(0);
    recent->setValue(95);
    QCOMPARE(persistent->value(), 5);
    recall->setValue(100);
    QCOMPARE(typed->value(), 0);
    QVERIFY(motion);
    auto *readingRetries =
        tabs->findChild<QCheckBox *>("readingRetriesEnabled");
    QVERIFY(readingRetries);
    QVERIFY(readingRetries->isChecked());
    readingRetries->setChecked(false);
    sound->setChecked(false);
    QVERIFY(!volume->isEnabled());
    motion->setChecked(true);
    QSettings saved(directory.path() + "/settings.ini", QSettings::IniFormat);
    QCOMPARE(saved.value("sound_volume").toInt(), 75);
    QCOMPARE(saved.value("newest_share").toInt(), 0);
    QCOMPARE(saved.value("recent_share").toInt(), 95);
    QCOMPARE(saved.value("persistent_share").toInt(), 5);
    QCOMPARE(saved.value("recall_share").toInt(), 100);
    QCOMPARE(saved.value("typed_share").toInt(), 0);
    QVERIFY(!saved.value("sound_enabled").toBool());
    QVERIFY(saved.value("reduced_motion").toBool());
    QVERIFY(!saved.value("reading_retry_enabled").toBool());
    QVERIFY(!store.insightsHtml({}).contains("Little milestones"));
    QVERIFY(!store.insightsHtml({}).contains("Newest first"));
    tabs->setCurrentIndex(3);
    QVERIFY(tabs->currentWidget()->findChild<WordTileCanvas *>());
    QVERIFY(!store.statsHtml({}).contains("Worth another look"));
    QVERIFY(store.insightsHtml({}).contains("Worth another look"));
  }
  void soundFilesAndAnimationSafety() {
    const auto success = feedbackTone(true), miss = feedbackTone(false);
    QCOMPARE(success.size(), 14444);
    QCOMPARE(miss.size(), 14444);
    QVERIFY(success.startsWith("RIFF"));
    QCOMPARE(success.mid(8, 4), QByteArray("WAVE"));
    QVERIFY(success != miss);
    QCOMPARE(success.mid(44, 2), QByteArray(2, '\0'));
    FeedbackAudio audio;
    QSignalSpy played(&audio, &FeedbackAudio::playbackStarted);
    audio.configure(false, 20);
    audio.play(true);
    QCOMPARE(played.count(), 0);
    Card card;
    card.setReducedMotion(false);
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Typing;
    c.reading = true;
    QSignalSpy graded(&card, &Card::graded);
    card.present(c, 5);
    auto *input = card.findChild<QLineEdit *>("readingInput");
    input->setText(c.subject.reading());
    card.findChild<QPushButton *>("checkReading")->click();
    QCOMPARE(graded.count(), 1);
    card.findChild<QPushButton *>("finishCard")->click();
    QVERIFY(card.isVisible()); // Short finish highlight, no delay in scoring.
    card.present(c, 5);
    QTest::qWait(230);
    QVERIFY(card.isVisible());
    card.setReducedMotion(true);
    input->setText(c.subject.reading());
    card.findChild<QPushButton *>("checkReading")->click();
    card.findChild<QPushButton *>("finishCard")->click();
    QVERIFY(!card.isVisible());
  }
  void recallFinishMotionCannotGradeTwice() {
    Card card;
    card.setReducedMotion(false);
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Recall;
    c.reading = true;
    QSignalSpy graded(&card, &Card::graded), completed(&card, &Card::completed);
    card.present(c, 5);
    card.findChild<QPushButton *>("revealAnswer")->click();
    card.findChild<QPushButton *>("markRemembered")->click();
    QVERIFY(card.isVisible());
    card.findChild<QPushButton *>("markMissed")->click();
    card.findChild<QPushButton *>("markRemembered")->click();
    QCOMPARE(graded.count(), 1);
    QCOMPARE(graded.first().first().toBool(), true);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(1).toInt(), 1);
    card.setReducedMotion(true);
    QVERIFY(!card.isVisible());
  }
  void accentTracksResizeAndStopsWhenHidden() {
    QWidget host;
    host.resize(352, 280);
    CardAccent accent(&host);
    host.show();
    accent.pulse(QColor("#9bd9bb"), 1000);
    host.resize(340, 500);
    QCOMPARE(accent.geometry(), host.rect());
    QVERIFY(accent.isVisible());
    host.hide();
    host.show();
    QVERIFY(!accent.isVisible());
  }
  void earnedFootingMotionPreservesStateAndCancels() {
    ProgressRecap recap;
    const auto state = progressionFixture();
    const QVector<ProgressNotice> notices{{ProgressNotice::Completed, 1, "猫", 0}};
    recap.present(notices, state);
    auto *canvas = recap.findChild<WordTileCanvas *>("recapComposition");
    auto *animation = canvas->findChild<QVariantAnimation *>("settleMotion");
    QVERIFY(animation);
    QCOMPARE(animation->state(), QAbstractAnimation::Running);
    const auto description = canvas->tileDescription(0);
    const auto start = canvas->grab().toImage();
    QTRY_COMPARE_WITH_TIMEOUT(animation->state(), QAbstractAnimation::Stopped, 800);
    const auto final = canvas->grab().toImage();
    QVERIFY(start != final);
    QCOMPARE(canvas->tileDescription(0), description);
    QCOMPARE(canvas->tileCount(), 1);
    recap.present(notices, state);
    QCOMPARE(animation->state(), QAbstractAnimation::Running);
    recap.setReducedMotion(true);
    QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    QCOMPARE(canvas->grab().toImage(), final);
    QVERIFY(recap.isVisible());
    QVERIFY(recap.remainingTime() > 4000);
    recap.setReducedMotion(false);
    recap.present(notices, state);
    recap.dismiss();
    QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    recap.present({{ProgressNotice::Developed, 2, "犬", 0}}, state);
    QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
    QVERIFY(canvas->tileDescription(0).contains("犬"));
    recap.setReducedMotion(true);
    recap.present(notices, state);
    QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
  }
  void chartDataAndRendering() {
    QTemporaryDir directory;
    Store store(directory.path());
    auto empty = loadChartData(store);
    QCOMPARE(empty.days.size(), 28);
    QCOMPARE(empty.mix, QVector<int>({0, 0, 0, 0, 0}));
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Typing;
    c.reading = true;
    c.selectionReason = "Recent struggles";
    QVERIFY(store.record(c, false, 100));
    QVERIFY(store.record(c, true, 100)); // Repeated same-day attempt does not
                                         // inflate checked performance.
    c.mode = Mode::Recall;
    QVERIFY(store.record(c, true, 100));
    QVERIFY(store.record(c, std::nullopt, 0));
    c.reading = false;
    c.mode = Mode::Choice;
    c.selectionReason = "Broad recall";
    QVERIFY(store.record(c, true, 100));
    auto data = loadChartData(store);
    QCOMPARE(data.days.last().answered, 4);
    QCOMPARE(data.days.last().readingTotal, 1);
    QCOMPARE(data.days.last().readingCorrect, 0);
    QCOMPARE(data.days.last().meaningCorrect, 1);
    QCOMPARE(data.mix, QVector<int>({3, 0, 1, 0, 0}));
    QCOMPARE(data.formatTotal, QVector<int>({1, 2, 1}));
    QCOMPARE(data.formatCorrect, QVector<int>({1, 1, 1}));
    QCOMPARE(std::accumulate(data.hourTotal.begin(), data.hourTotal.end(), 0),
             4);
    QSqlQuery legacy(store.db);
    QVERIFY(legacy.exec("UPDATE attempts SET event_json=NULL WHERE id=3"));
    QCOMPARE(loadChartData(store).mix, QVector<int>({2, 0, 1, 1, 0}));
    c.selectionReason = "Newest 30";
    QVERIFY(store.record(c, true, 100));
    QCOMPARE(loadChartData(store).mix, QVector<int>({2, 0, 1, 1, 1}));
    InsightsBrowser browser;
    browser.resize(760, 700);
    browser.show();
    browser.setInsights(store, "<p>Preserved history</p>");
    QTest::qWait(20);
    QVERIFY(!browser.toPlainText().contains("Preserved history"));
    browser.anchorClicked(QUrl("moji-nook:details"));
    QVERIFY(browser.toPlainText().contains("Preserved history"));
    browser.anchorClicked(QUrl("moji-nook:help"));
    QVERIFY(browser.toPlainText().contains("reading 0/1"));
    QVERIFY(!browser.document()
                 ->resource(QTextDocument::ImageResource,
                            QUrl("moji-nook-chart:daily-activity"))
                 .value<QImage>()
                 .isNull());
    browser.resize(420, 600);
    QTest::qWait(20);
    QCOMPARE(browser.horizontalScrollBar()->maximum(), 0);
    QCOMPARE(browser.property("chartColumns").toInt(), 1);
    if (qEnvironmentVariableIsSet("MOJI_NOOK_CHART_PREVIEW"))
      QVERIFY(
          browser.grab().save(qEnvironmentVariable("MOJI_NOOK_CHART_PREVIEW")));
    browser.resize(1000, 760);
    QTest::qWait(20);
    QCOMPARE(browser.property("chartColumns").toInt(), 2);
    QCOMPARE(browser.horizontalScrollBar()->maximum(), 0);
    browser.resize(1600, 1000);
    QTest::qWait(20);
    QCOMPARE(browser.property("chartColumns").toInt(), 3);
    QCOMPARE(browser.horizontalScrollBar()->maximum(), 0);
    const auto previousTheme = qApp->property("mojiNookTheme");
    for (int i = 0; i < themeCount; ++i) {
      qApp->setProperty("mojiNookTheme", i);
      const auto c = colors(i);
      QPalette p = browser.palette();
      p.setColor(QPalette::Base, c.background);
      p.setColor(QPalette::AlternateBase, c.surface);
      p.setColor(QPalette::Text, c.text);
      browser.setPalette(p);
      browser.setStyleSheet(appStyle(i));
      browser.setInsights(store, {});
      const auto chart = browser.document()
                             ->resource(QTextDocument::ImageResource,
                                        QUrl("moji-nook-chart:daily-activity"))
                             .value<QImage>();
      QVERIFY(!chart.isNull());
      const auto backing = chart.pixelColor(40, 40);
      // Rounded chart grouping must let the workspace show through.
      QVERIFY(backing.alpha() > 0 && backing.alpha() <= 32);
      bool hasSeriesColor = false;
      for (int y = 0; y < chart.height() && !hasSeriesColor; ++y)
        for (int x = 0; x < chart.width(); ++x)
          if (chart.pixelColor(x, y) == QColor(c.series1)) {
            hasSeriesColor = true;
            break;
          }
      QVERIFY2(hasSeriesColor,
               "Chart must use the active family's series color");
      if (qEnvironmentVariableIsSet("MOJI_NOOK_CHART_PREVIEW"))
        QVERIFY(
            browser.grab().save(qEnvironmentVariable("MOJI_NOOK_CHART_PREVIEW") +
                                QString(".theme-%1.png").arg(i)));
    }
    qApp->setProperty("mojiNookTheme", previousTheme);
    if (qEnvironmentVariableIsSet("MOJI_NOOK_CHART_PREVIEW"))
      QVERIFY(browser.grab().save(qEnvironmentVariable("MOJI_NOOK_CHART_PREVIEW") +
                                  ".wide.png"));
    if (qEnvironmentVariableIsSet("MOJI_NOOK_CHART_PREVIEW")) {
      QTextBrowser history;
      history.resize(800, 650);
      history.setHtml(store.historyHtml(0));
      history.show();
      QTest::qWait(20);
      QVERIFY(history.grab().save(qEnvironmentVariable("MOJI_NOOK_CHART_PREVIEW") +
                                  ".history.png"));
    }
  }
  void completeTwoCardAppSession() {
    QTemporaryDir directory;
    Store store(directory.path());
    App app(store, directory.path());
    app.start(true, true);
    Card *card = nullptr;
    auto locate = [&] {
      for (auto *window : QApplication::topLevelWidgets())
        if (auto *candidate = qobject_cast<Card *>(window);
            candidate && candidate->isVisible())
          return candidate;
      return static_cast<Card *>(nullptr);
    };
    QTRY_VERIFY_WITH_TIMEOUT((card = locate()) != nullptr, 2000);
    for (int position = 1; position <= 2; ++position) {
      QCOMPARE(card->challenge.event["session_position"].toInt(), position);
      QCOMPARE(card->challenge.event["session_total"].toInt(), 2);
      const auto challenge = card->challenge;
      if (challenge.mode == Mode::Recall) {
        card->findChild<QPushButton *>("revealAnswer")->click();
        card->findChild<QPushButton *>("markRemembered")->click();
      } else {
        if (challenge.mode == Mode::Typing) {
          card->findChild<QLineEdit *>("readingInput")
              ->setText(challenge.subject.reading());
          card->findChild<QPushButton *>("checkReading")->click();
        } else {
          for (auto *button : card->findChildren<QPushButton *>())
            if (button->property("answer").isValid() &&
                challenge.accepts(button->property("answer").toString())) {
              button->click();
              break;
            }
        }
        card->findChild<QPushButton *>("finishCard")->click();
      }
      QCOMPARE(store.recentSubjects().size(), position);
    }
    QTRY_VERIFY(!card->isVisible());
    QSqlQuery q(store.db);
    QVERIFY(q.exec(
        "SELECT COUNT(DISTINCT "
        "json_extract(event_json,'$.session_id')),SUM(correct) FROM attempts"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    QCOMPARE(q.value(1).toInt(), 2);
    QVERIFY(store.insightsHtml(demoSubjects())
                .contains("1 fully answered / 1 recorded sessions"));
  }
  void wheelDoesNotEditSettings() {
    ScrollSafeComboBox combo;
    combo.addItems({"One", "Two", "Three"});
    combo.setCurrentIndex(1);
    ScrollSafeSpinBox spin;
    spin.setValue(5);
    ScrollSafeSlider slider;
    slider.setValue(30);
    for (QWidget *widget : QList<QWidget *>{&combo, &spin, &slider}) {
      widget->show();
      widget->setFocus();
      QWheelEvent event(QPointF(5, 5), widget->mapToGlobal(QPoint(5, 5)),
                        QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
      QApplication::sendEvent(widget, &event);
      QVERIFY(!event.isAccepted());
    }
    QCOMPARE(combo.currentIndex(), 1);
    QCOMPARE(spin.value(), 5);
    QCOMPARE(slider.value(), 30);
    QTest::keyClick(&slider, Qt::Key_Right);
    QCOMPARE(slider.value(), 31);
    QTest::keyClick(&combo, Qt::Key_Down);
    QCOMPARE(combo.currentIndex(), 2);
    QTest::keyClick(&spin, Qt::Key_Up);
    QCOMPARE(spin.value(), 6);
  }
  void sessionClockAndCardProgress() {
    PracticeClock clock;
    QSignalSpy due(&clock, &PracticeClock::due);
    QCOMPARE(clock.sessionSize, 2);
    clock.request();
    QCOMPARE(clock.position, 1);
    QCOMPARE(due.count(), 1);
    QVERIFY(!clock.timer.isActive());
    clock.sessionSize =
        5; // Never enlarge a session that is already in progress.
    clock.advance();
    QCOMPARE(clock.position, 2);
    QCOMPARE(clock.sessionTotal, 2);
    QCOMPARE(due.count(), 2);
    QVERIFY(!clock.timer.isActive());
    clock.advance();
    QVERIFY(!clock.pending);
    QVERIFY(clock.timer.isActive());
    clock.request();
    QCOMPARE(clock.sessionTotal, 5);
    clock.complete();
    clock.sessionSize = 99;
    clock.request();
    QCOMPARE(clock.sessionTotal, PracticeClock::maxSessionSize);
    clock.setPaused(true);
    clock.advance();
    QCOMPARE(clock.position, 1);
    clock.setPaused(false);
    clock.complete();
    Card card;
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Typing;
    c.reading = true;
    c.event = {
        {"session_id", "test"}, {"session_position", 1}, {"session_total", 2}};
    card.present(c, 5);
    QVERIFY(
        card.findChild<QLabel *>("cardHint")->text().contains("Card 1 of 2"));
    QCOMPARE(card.findChild<QPushButton *>("finishCard")->text(),
             QString("Next · 2 of 2"));
    Challenge logged;
    connect(&card, &Card::completed,
            [&](Challenge event, int, qint64) { logged = event; });
    auto *input = card.findChild<QLineEdit *>("readingInput");
    QTest::mouseClick(input, Qt::LeftButton);
    QTest::keyClicks(input, "nihon");
    QTest::keyClick(input, Qt::Key_Return);
    card.findChild<QPushButton *>("dismiss")->click();
    QVERIFY(logged.event["end_session"].toBool());
    QCOMPARE(logged.event["submitted_answer"].toString(), QString("nihon"));
    QVERIFY(logged.event.contains("presented_at"));
    QVERIFY(logged.event.contains("first_interaction_at"));
    QVERIFY(logged.event.contains("graded_at"));
  }
  void choiceMissPreservesResponse() {
    Card card;
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Choice;
    for (bool reading : {true, false}) {
      c.reading = reading;
      c.choices = reading
                      ? QStringList{"にほん", "ほんじつ", "まいにち", "にっき"}
                      : QStringList{"Japan", "Today", "Every day", "Diary"};
      card.present(c, 5);
      auto buttons = card.findChildren<QPushButton *>();
      for (auto *b : buttons)
        if (b->property("answer").toString() == c.choices[1])
          b->click();
      QCOMPARE(card.findChild<QLabel *>("submittedAnswer")->text(),
               c.choices[1]);
      QVERIFY(card.findChild<QLabel *>("resultIcon")->accessibleName() ==
              "Correct answer");
      QVERIFY(card.findChild<QPushButton *>("dismiss")->toolTip().contains(
          "keep this result"));
      int outcome = -9;
      auto conn =
          connect(&card, &Card::completed,
                  [&](Challenge, int result, qint64) { outcome = result; });
      card.close();
      QCOMPARE(outcome, 0);
      disconnect(conn);
    }
  }
  void keyboardAndExampleStayLocal() {
    Card card;
    Challenge c;
    c.subject = demoSubjects().first();
    c.subject.contextJa = "日本語";
    c.subject.contextEn = "Japanese";
    c.mode = Mode::Typing;
    c.reading = true;
    int completed = 0;
    connect(&card, &Card::completed,
            [&](Challenge, int, qint64) { ++completed; });
    card.present(c, 5);
    card.activateWindow();
    auto *input = card.findChild<QLineEdit *>("readingInput");
    QVERIFY(input->focusPolicy() & Qt::TabFocus);
    QTest::mouseClick(input, Qt::LeftButton);
    input->setFocus();
    QTest::keyClicks(input, "wrong");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(completed, 0);
    QCOMPARE(card.findChild<QLabel *>("submittedAnswer")->text(),
             QString("wrong"));
    auto *example = card.findChild<QLabel *>("example");
    QVERIFY(!example->isVisible());
    card.findChild<QPushButton *>("exampleToggle")->click();
    QVERIFY(example->isVisible());
    QCOMPARE(completed, 0);
    auto *done = card.findChild<QPushButton *>("finishCard");
    QTest::keyClick(done, Qt::Key_Return);
    QCOMPARE(completed, 1);
    card.present(c, 5);
    QTest::keyClick(card.findChild<QPushButton *>("dismiss"), Qt::Key_Escape);
    QCOMPARE(completed, 2);
  }
  void longContentFitsScreen() {
    Card card;
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Choice;
    c.choices = {QString("A very long vocabulary meaning ").repeated(8),
                 "Today", "Every day", "Japan"};
    card.present(c, 5);
    QVERIFY(card.height() <= card.screen()->availableGeometry().height() - 32);
    QVERIFY(card.height() <= 560);
    auto *scroll = card.findChild<QScrollArea *>();
    QVERIFY(scroll);
    QVERIFY(scroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
  }
  void cardPlacementIsExplicitAndCompact() {
    Card card;
    card.setStyleSheet(appStyle());
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Recall;
    c.reading = true;
    c.event = {{"session_id", "placement"},
               {"session_position", 1},
               {"session_total", 2}};
    card.setMonitor("disconnected-display");
    card.present(c, 5);
    QCoreApplication::processEvents();
    const auto *primary = QGuiApplication::primaryScreen();
    QCOMPARE(card.screen(), primary);
    const QRect area = primary->availableGeometry();
    QCOMPARE(card.width(), qMin(352, area.width() - 32));
    QVERIFY(card.height() < 360);
    QVERIFY(!card.findChild<QLabel *>("cardHint")->text().contains('\n'));
    for (int corner = 0; corner < 4; ++corner) {
      card.setCorner(corner);
      const bool right = corner == 0 || corner == 2,
                 bottom = corner < 2;
      QCOMPARE(card.x(), right ? area.x() + area.width() - card.width() - 16
                               : area.x() + 16);
      QCOMPARE(card.y(), bottom ? area.y() + area.height() - card.height() - 16
                                : area.y() + 16);
      QVERIFY(area.contains(card.geometry()));
    }
    card.setMonitor(primary->name());
    QCOMPARE(card.screen(), primary);
  }
  void shortAnswerCardsFitWithoutScrolling() {
    Card card;
    Challenge c;
    c.subject = demoSubjects().first();
    c.subject.contextJa = "日本語を勉強しています。";
    c.subject.contextEn = "I’m studying Japanese.";
    c.mode = Mode::Typing;
    c.reading = true;
    for (int theme = 0; theme < themeCount; ++theme) {
      card.setStyleSheet(appStyle(theme));
      card.applyTheme(theme);
      card.present(c, 5);
      QVERIFY(!card.findChild<QWidget *>("answerRow")->isVisible());
      auto *input = card.findChild<QLineEdit *>("readingInput");
      input->setText("incorrect");
      card.findChild<QPushButton *>("checkReading")->click();
      QCoreApplication::processEvents();
      auto *submitted = card.findChild<QLabel *>("submittedAnswer");
      QVERIFY(card.findChild<QWidget *>("answerRow")->isVisible());
      QCOMPARE(submitted->text(), QString("incorrect"));
      QVERIFY(submitted->height() <= QFontMetrics(submitted->font()).lineSpacing() + 4);
      auto *scroll = card.findChild<QScrollArea *>();
      QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
      QVERIFY(!card.findChild<QLabel *>("cardHint")->isVisible());
      card.findChild<QPushButton *>("exampleToggle")->click();
      QCoreApplication::processEvents();
      card.findChild<QPushButton *>("exampleToggle")->click();
      QCoreApplication::processEvents();
      QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
    }
    // A stored pixmap is insufficient: invalid coordinate mapping clipped the
    // actual feedback paint while the grade and accessibility name still passed.
    for (int theme = 0; theme < themeCount; ++theme) {
      card.setStyleSheet(appStyle(theme));
      card.applyTheme(theme);
      card.present(c, 5);
      card.findChild<QLineEdit *>("readingInput")->setText("nihon");
      card.findChild<QPushButton *>("checkReading")->click();
      QCoreApplication::processEvents();
      const auto painted = card.findChild<QLabel *>("resultIcon")->grab().toImage();
      int inkPixels = 0;
      for (int y = 0; y < painted.height(); ++y)
        for (int x = 0; x < painted.width(); ++x)
          inkPixels += painted.pixelColor(x, y) == QColor(colors(theme).accent);
      QVERIFY2(inkPixels > 4, "Correct feedback symbol must actually paint inside its widget");
    }
    c.mode = Mode::Choice;
    c.choices = {"にほん", "ほんじつ", "まいにち", "にっき"};
    card.present(c, 5);
    QCoreApplication::processEvents();
    QCOMPARE(card.findChild<QScrollArea *>()->verticalScrollBar()->maximum(), 0);
    QVERIFY(card.height() < 520);
  }
  void readingChoicesDoNotOverlap() {
    Card card;
    card.setStyleSheet(appStyle());
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Choice;
    c.reading = true;
    c.choices = {"かい", "がい", "はい", "ばい"};
    card.present(c, 5);
    QTest::qWait(30);
    QList<QRect> bounds;
    for (auto *b : card.findChildren<QPushButton *>())
      if (b->property("answer").isValid()) {
        bounds.append(QRect(b->mapTo(&card, QPoint()), b->size()));
        auto *text = b->findChild<QLabel *>();
        QVERIFY(text);
        QVERIFY(text->height() >= text->fontMetrics().height());
      }
    QCOMPARE(bounds.size(), 4);
    for (int i = 0; i < bounds.size(); ++i)
      for (int j = i + 1; j < bounds.size(); ++j)
        QVERIFY(!bounds[i].intersects(bounds[j]));
  }
  void emptyTokenDoesNotClaimSaved() {
    QTemporaryDir dir;
    Store store(dir.path());
    App app(store, dir.path());
    QPushButton *save = nullptr;
    QLabel *status = nullptr;
    QLineEdit *token = nullptr;
    for (auto *window : qApp->topLevelWidgets())
      if (auto *candidate = window->findChild<QPushButton *>("saveToken")) {
        save = candidate;
        status = window->findChild<QLabel *>("connectionStatus");
        token = window->findChild<QLineEdit *>("apiTokenInput");
      }
    QVERIFY(save && status && token);
    save->click();
    QVERIFY(status->text().contains("Paste"));
    QVERIFY(!token->placeholderText().contains("Token saved"));
    QVERIFY(!QFile::exists(dir.path() + "/token"));
  }
  void selectedTextContrast() {
    auto luminance = [](QColor c) {
      auto linear = [](double v) {
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
      };
      return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) +
             .0722 * linear(c.blueF());
    };
    for (int theme = 0; theme < themeCount; ++theme) {
      QLineEdit input;
      input.setStyleSheet(appStyle(theme));
      input.ensurePolished();
      const auto p = input.palette();
      double a = luminance(p.color(QPalette::Highlight)),
             b = luminance(p.color(QPalette::HighlightedText));
      QVERIFY2((qMax(a, b) + .05) / (qMin(a, b) + .05) >= 4.5,
               qPrintable(QString("Theme %1 selection contrast").arg(theme)));
      const auto c = colors(theme);
      // Test the rendered palette, not only token pairs: missing stylesheet
      // placeholders previously shifted every shell color to the wrong token.
      QWidget rail;
      rail.setObjectName("studioHeader");
      rail.setStyleSheet(appStyle(theme));
      rail.ensurePolished();
      QCOMPARE(rail.palette().color(QPalette::Window), QColor(c.rail));
      QPushButton selected("Practice");
      selected.setProperty("role", "section");
      selected.setCheckable(true);
      selected.setChecked(true);
      selected.setStyleSheet(appStyle(theme));
      selected.ensurePolished();
      selected.resize(180, 48);
      selected.show();
      // Qt applies pseudo-state colors while painting; the base palette only
      // describes the unselected state. Sample the rounded selected border.
      const auto rendered = selected.grab().toImage();
      QCOMPARE(rendered.pixelColor(rendered.width() / 2, rendered.height() - 1),
               QColor(c.action));
      const auto contrast = [&](QString foreground, QString background) {
        double x = luminance(QColor(foreground)),
               y = luminance(QColor(background));
        return (qMax(x, y) + .05) / (qMin(x, y) + .05);
      };
      for (const auto &foreground :
           {c.text, c.muted, c.accent, c.warm, c.negative})
        for (const auto &background : {c.background, c.surface, c.rail})
          QVERIFY2(contrast(foreground, background) >= 4.5,
                   qPrintable(QString("Theme %1 text %2 on %3")
                                  .arg(theme)
                                  .arg(foreground, background)));
      QVERIFY(contrast(c.posterInk, c.poster) >= 4.5);
      if (theme == 2 || theme == 3)
        QVERIFY(contrast(c.series1, c.series2) >= 3.0);
    }
  }
  void timerWaitsForCompletion() {
    PracticeClock clock;
    clock.intervalMs = 30;
    QSignalSpy spy(&clock, &PracticeClock::due);
    clock.request();
    QCOMPARE(spy.count(), 1);
    QVERIFY(clock.pending);
    QVERIFY(!clock.timer.isActive());
    QTest::qWait(80);
    QCOMPARE(spy.count(), 1);
    clock.request();
    QCOMPARE(spy.count(), 1);
    clock.complete();
    QVERIFY(clock.timer.isActive());
    QTRY_COMPARE(spy.count(), 2);
    clock.setPaused(true);
    clock.complete();
    QVERIFY(!clock.timer.isActive());
    clock.setPaused(false);
    QVERIFY(clock.timer.isActive());
  }
  void revealAndDismiss() {
    Card card;
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Recall;
    c.reading = true;
    int n = 0, last = -9;
    connect(&card, &Card::completed, [&](Challenge, int result, qint64) {
      ++n;
      last = result;
    });
    card.present(c, 5);
    QVERIFY(card.testAttribute(Qt::WA_ShowWithoutActivating));
    QTest::mouseClick(card.findChild<QPushButton *>("revealAnswer"),
                      Qt::LeftButton);
    QCOMPARE(n, 0);
    QTest::mouseClick(card.findChild<QPushButton *>("markRemembered"),
                      Qt::LeftButton);
    QCOMPARE(n, 1);
    QCOMPARE(last, 1);
    QVERIFY(!card.isVisible());
    card.close();
    QCOMPARE(n, 1);
    card.present(c, 5);
    QTest::mouseClick(card.findChild<QPushButton *>("dismiss"), Qt::LeftButton);
    QCOMPARE(n, 2);
    QCOMPARE(last, -1);
  }
  void typingWaitsForDone() {
    Card card;
    Challenge c;
    c.subject = demoSubjects().first();
    c.mode = Mode::Typing;
    c.reading = true;
    int n = 0, last = -9;
    connect(&card, &Card::completed, [&](Challenge, int result, qint64) {
      ++n;
      last = result;
    });
    card.present(c, 5);
    auto *input = card.findChild<QLineEdit *>("readingInput");
    QTest::mouseClick(input, Qt::LeftButton);
    QTest::keyClicks(input, "nihon");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(n, 0);
    QTest::mouseClick(card.findChild<QPushButton *>("finishCard"),
                      Qt::LeftButton);
    QCOMPARE(n, 1);
    QCOMPARE(last, 1);
  }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
