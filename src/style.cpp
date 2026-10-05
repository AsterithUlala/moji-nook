#include "ui.h"
#include "theme.h"
#include "style.h"
QString recolor(QString source, int index) {
  const auto c = colors(index);
  QHash<QString, QString> replacements;
  auto add = [&](QStringList originals, const QString &value) {
    for (const auto &key : originals)
      replacements[key] = value;
  };
  add({"#15251f"}, c.background);
  add({"#293e33", "#20342a", "#203831", "#25332f", "#2e4438"}, c.surface);
  add({"#395744", "#466850", "#4b755b", "#355e4b"}, c.hover);
  add({"#476151", "#536e5c", "#93b69c", "#438765"}, c.border);
  add({"#ecf1ea", "#eaf4e9", "#e8eee9", "#ffffff"}, c.text);
  add({"#a9bcb0", "#b6c6bc", "#819388", "#a8b9b2", "#a8b9b2"}, c.muted);
  add({"#a6e2bf", "#acd4b5", "#c0e7c9", "#b5dfbd", "#b1d9b8", "#9cd9bc",
       "#66b78a"},
      c.accent);
  add({"#e8b28d", "#e2aa86"}, c.warm);
  add({"#f0bba0"}, c.negative);
  auto blend = [&](double amount) {
    QColor a(c.background), b(c.accent);
    return QColor::fromRgbF(a.redF() * (1 - amount) + b.redF() * amount,
                            a.greenF() * (1 - amount) + b.greenF() * amount,
                            a.blueF() * (1 - amount) + b.blueF() * amount)
        .name();
  };
  auto tint = [&](const QString &color) {
    const QColor a(c.background), b(color);
    return QColor::fromRgbF(a.redF()*.91 + b.redF()*.09,
                           a.greenF()*.91 + b.greenF()*.09,
                           a.blueF()*.91 + b.blueF()*.09).name();
  };
  replacements["#223d30"] = tint(c.accent);
  replacements["#432e2b"] = tint(c.negative);
  replacements["#355e4b"] = blend(.18);
  replacements["#438765"] = blend(.28);
  replacements["#66b78a"] = blend(.38);
  const QRegularExpression pattern("#[0-9a-fA-F]{6}");
  auto matches = pattern.globalMatch(source);
  QString result;
  qsizetype position = 0;
  while (matches.hasNext()) {
    auto match = matches.next();
    result += source.mid(position, match.capturedStart() - position);
    result += replacements.value(match.captured(), match.captured());
    position = match.capturedEnd();
  }
  return result + source.mid(position);
}
QPixmap symbolPixmap(bool cross, const QColor &color, int size, bool circle) {
  QPixmap pixmap(size * 2, size * 2);
  pixmap.setDevicePixelRatio(2);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  if (circle) {
    QColor fill = color;
    fill.setAlpha(25);
    painter.setBrush(fill);
    painter.setPen(QPen(color, 1.3));
    painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
  }
  painter.setPen(QPen(color, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (cross) {
    painter.drawLine(QPointF(size * .33, size * .33),
                     QPointF(size * .67, size * .67));
    painter.drawLine(QPointF(size * .67, size * .33),
                     QPointF(size * .33, size * .67));
  } else {
    QPainterPath path;
    path.moveTo(size * .27, size * .51);
    path.lineTo(size * .44, size * .67);
    path.lineTo(size * .74, size * .35);
    painter.drawPath(path);
  }
  return pixmap;
}

QString appStyle(int theme) {
  QString css = R"(
        QWidget { background:transparent; color:#ecf1ea; font-family:'Noto Sans'; font-size:14px; }
        QMainWindow { background:#15251f; }
        QWidget#settingsBody QLabel { font-size:14px; }
        QWidget#settingsBody QLabel#muted { color:#a9bcb0; font-size:13px; }
        QWidget#settingsBody QGroupBox { font-size:16px; }
        QWidget#settingsBody QCheckBox { background:transparent; font-size:14px; spacing:8px; }
        QWidget#settingsBody QCheckBox::indicator { width:18px; height:18px; border:1px solid #536e5c; border-radius:3px; }
        QWidget#settingsBody QCheckBox::indicator:checked { background:#395744; }
        QWidget#settingsBody QSpinBox, QWidget#settingsBody QComboBox { font-size:14px; }
        QLabel { background:transparent; }
        QLabel#eyebrow { color:#e8b28d; font-size:12px; font-weight:600; }
        QLabel#word { font-family:'Noto Sans CJK JP'; font-size:58px; font-weight:500; }
        QLabel#muted { color:#a9bcb0; font-size:12px; }
        QLabel#answer { color:#a6e2bf; font-size:23px; }
        QPushButton { background:#293e33; border:1px solid #476151; border-radius:8px; padding:10px 14px; }
        QPushButton:hover { background:#395744; border-color:#93b69c; }
        QPushButton:pressed { background:#466850; }
        QPushButton:disabled { color:#819388; border-color:#2e4438; }
        QPushButton[role="primary"] { background:#acd4b5; color:#15251f; border:1px solid #acd4b5; font-weight:600; }
        QPushButton[role="primary"]:hover { background:#c0e7c9; }
        QPushButton[role="quiet"] { border:none; background:transparent; color:#b6c6bc; padding:6px; }
        QPushButton[role="quiet"]:hover { color:#ffffff; background:#293e33; }
        QPushButton[role="miss"] { color:#f0bba0; }
        QPushButton#dismiss { padding:0; border:1px solid transparent; border-radius:8px; }
        QPushButton#dismiss:hover { border-color:#536e5c; }
        QLabel#cardMeta { color:#a9bcb0; font-size:12px; }
        QLabel#challengeType { color:#ecf1ea; background:#293e33; border-radius:6px; padding:5px 8px; font-size:12px; }
        QLabel#supplement { color:#ecf1ea; font-size:14px; }
        QLabel#example { color:#a9bcb0; background:#20342a; padding:12px; border-radius:9px; font-size:12px; }
        QLabel#cardHint { color:#a9bcb0; font-size:12px; }
        QGroupBox { background:transparent; border:0; border-radius:0; margin-top:24px; padding:20px 8px 16px; font-weight:600; }
        QGroupBox::title { subcontrol-origin:margin; left:8px; padding:0; }
        QPushButton:focus, QComboBox:focus, QSpinBox:focus { border:2px solid #b5dfbd; }
        QLabel#readingChoice { font-family:'Noto Sans CJK JP'; font-size:28px; font-weight:500; }
        QLabel#meaningChoice { font-size:16px; }
        QLineEdit#readingInput { font-family:'Noto Sans CJK JP'; font-size:24px; }
        QLabel#kanaPreview { font-family:'Noto Sans CJK JP'; font-size:26px; }
        QLineEdit, QSpinBox, QComboBox { padding:8px; background:#20342a; border:1px solid #536e5c; border-radius:6px; selection-background-color:#4b755b; }
        QComboBox { padding-right:36px; }
        QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:30px; border:0; background:transparent; }
        QComboBox::down-arrow { image:none; width:0; height:0; }
        QSpinBox { padding-right:38px; }
        QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin:border; width:20px; border:0; background:transparent; }
        QSpinBox::up-button { subcontrol-position:top right; }
        QSpinBox::down-button { subcontrol-position:bottom right; }
        QSpinBox::up-arrow, QSpinBox::down-arrow { image:none; width:0; height:0; }
        QComboBox:hover, QSpinBox:hover { border-color:#93b69c; }
        QComboBox:focus, QSpinBox:focus { border:2px solid #b5dfbd; }
        QComboBox:disabled, QSpinBox:disabled { color:#819388; background:#20342a; border-color:#395744; }
        QSlider { min-height:32px; background:transparent; }
        QSlider::groove:horizontal { height:8px; background:#20342a; border:1px solid #536e5c; border-radius:4px; }
        QSlider::sub-page:horizontal { background:#b1d9b8; border:0; border-radius:3px; margin:1px; }
        QSlider::add-page:horizontal { background:#20342a; border:0; border-radius:3px; margin:1px; }
        QSlider::handle:horizontal { background:#b5dfbd; border:2px solid #536e5c; width:20px; margin:-8px 0; border-radius:11px; }
        QSlider::handle:horizontal:hover { border-color:#eaf4e9; }
        QSlider::handle:horizontal:focus { border:2px solid #ffffff; }
        QSlider::groove:horizontal:disabled { background:#20342a; border-color:#395744; }
        QSlider::sub-page:horizontal:disabled { background:#b1d9b8; }
        QSlider::add-page:horizontal:disabled { background:#20342a; }
        QSlider::handle:horizontal:disabled { background:#819388; border-color:#395744; }
        QLineEdit:focus { border-color:#b5dfbd; }
        QTextBrowser { border:none; background:transparent; padding:12px; font-size:16px; }
        QWidget#settingsBody { background:transparent; }
        QWidget#settingsBody QGroupBox { border:none; }
        QTabWidget::pane { border:none; }
        QTabBar::tab { padding:12px 20px; color:#a9bcb0; background:transparent; border:0; border-right:1px solid #395744; border-bottom:2px solid transparent; }
        QTabBar::tab:hover { color:#eaf4e9; background:#293e33; }
        QTabBar::tab:selected { color:#eaf4e9; background:#20342a; border-bottom:2px solid #b1d9b8; font-weight:600; }
        QMenu { background:#20342a; border:1px solid #536e5c; padding:6px; }
        QMenu::item { padding:8px 20px; }
        QMenu::item:selected { background:#395744; }
        QToolTip { background:#20342a; color:#ecf1ea; border:1px solid #536e5c; padding:5px; }
        QScrollBar:vertical { width:10px; background:transparent; margin:0; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; background:transparent; border:0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
        QScrollBar:horizontal { height:10px; background:transparent; margin:0; }
        QScrollBar::handle:horizontal { background:#476151; border-radius:4px; min-width:25px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; background:transparent; border:0; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }
        QScrollBar::handle:vertical { background:#476151; border-radius:4px; min-height:25px; }
    )";
  css = recolor(css, theme);
  css += QString("QPushButton[role=\"primary\"] { color:%1; background:%2; "
                 "border-color:%2; } "
                 "QLineEdit,QComboBox,QSpinBox,QTextBrowser { "
                 "selection-color:%1; selection-background-color:%2; }")
             .arg(colors(theme).actionInk, colors(theme).action);
  const QColor accent(colors(theme).action);
  css += QString("QPushButton[role=\"primary\"]:hover { background:%1; }")
             .arg(QColor(colors(theme).actionInk).lightness() > 128
                      ? accent.darker(112).name()
                      : accent.lighter(108).name());
  const auto c = colors(theme);
  css += QString(R"(
    QLabel#wordKind { background:transparent; padding:0; font-size:13px; font-weight:500; }
    QLabel#wordKind[subjectKind="kanji"] { color:%1; }
    QLabel#wordKind[subjectKind="vocabulary"],
    QLabel#wordKind[subjectKind="kana_vocabulary"] { color:%2; }
  )").arg(wanikaniSubjectInk("kanji", theme).name(),
          wanikaniSubjectInk("vocabulary", theme).name());
  css += QString(R"(
    QWidget#studioHeader { background:$rail; border:0; border-bottom:1px solid $border; }
    QWidget#studioHeader QLabel { background:transparent; color:$text; }
    QLabel#studioBrand { font-size:20px; font-weight:600; }
    QWidget#studioHeader QPushButton {
      background:transparent; color:$muted; border:1px solid transparent;
      border-radius:6px; padding:10px 12px; font-size:14px;
    }
    QWidget#studioHeader QPushButton:hover { background:$hover; color:$text; }
    QWidget#studioHeader QPushButton:checked { background:$action; color:$actionInk; font-weight:600; }
    QWidget#studioHeader QPushButton:focus { border:1px solid $action; }
    QWidget#studioHeader QCheckBox { background:transparent; color:$muted; spacing:6px; font-size:13px; }
    QWidget#studioHeader QCheckBox::indicator { width:16px; height:16px; border:1px solid $muted; border-radius:4px; background:transparent; }
    QPushButton#studioMore::menu-indicator { image:none; }
    QLabel#homeHeading { font-size:28px; font-weight:600; }
    QFrame#practiceSession { background:$poster; border:0; border-radius:16px; }
    QFrame#practiceSession QLabel { background:transparent; color:$posterInk; }
    QFrame#practiceSession QLabel#studioHero { font-size:28px; font-weight:600; }
    QFrame#practiceSession QLabel#practiceSchedule { font-size:15px; }
    QFrame#practiceSession QLabel#homeCadence { font-size:13px; }
    QFrame#practiceSession QLabel#practiceSourceSummary { font-size:13px; }
    QFrame#practiceSession QPushButton[role="quiet"] { color:$posterInk; padding:8px 4px; text-align:left; }
    QFrame#practiceSession QPushButton[role="quiet"]:hover { background:$surface; color:$text; }
    QPushButton#studioBegin { font-size:16px; }
    QLabel#collectionHeading { font-size:24px; font-weight:600; }
    QLabel#homeWordCount { font-size:14px; color:$muted; }
    QLabel#homeWordsEmpty { font-size:19px; color:$muted; }
    QLabel#homeInspection { font-size:13px; color:$muted; }
    QPushButton#homeFocus { text-align:left; color:$action; border:1px solid $border; padding:10px; }
    QFrame#recentPractice { background:transparent; border:0; border-top:1px solid $border; }
    QTextBrowser#homeStats { background:transparent; padding:0; font-size:14px; }
    QPushButton#adjustPractice { text-align:left; font-size:13px; }
    QFrame#placementSettings { background:transparent; border:0; border-radius:0; }
    QFrame#placementSettings QLabel { background:transparent; }
    QLabel#controlHeading { font-size:15px; font-weight:600; color:$text; margin:0; padding:0; }
    QFrame#practiceSectionDivider { color:$border; margin:4px 0; }
    QWidget#practiceSettingsPanel, QWidget#wanikaniSettingsPanel { background:transparent; }
    QPushButton[role="control"] { background:$surface; color:$text; border:1px solid $border; border-radius:6px; padding:8px 12px; }
    QPushButton[role="control"]:hover { background:$hover; border-color:$action; }
    QPushButton[role="control"]:pressed { background:$action; color:$actionInk; }
    QLabel#connectionStatus[status="offline"], QLabel#ankiStatus[status="offline"] { color:$muted; }
    QLabel#connectionStatus[status="connected"], QLabel#ankiStatus[status="connected"] { color:$accent; }
    QLabel#connectionStatus[status="connecting"], QLabel#ankiStatus[status="connecting"] { color:$warm; }
    QLabel#connectionStatus[status="error"], QLabel#ankiStatus[status="error"] { color:$negative; }
    QPushButton[role="section"] { border:0; border-right:1px solid $border; border-bottom:2px solid transparent; background:transparent; color:$muted; border-radius:0; padding:11px 14px; }
    QPushButton[role="section"][lastSection="true"] { border-right:0; }
    QPushButton[role="section"]:hover { background:$surface; color:$text; }
    QPushButton[role="section"]:checked { background:$surface; color:$action; border-bottom:2px solid $action; font-weight:600; }
    QPushButton[role="section"]:focus { border:2px solid $action; padding:9px 12px 11px; }
    QLabel#practiceSourceWarning { color:$text; background:$surface; padding:12px; border:1px solid $warm; border-radius:8px; }
    QFrame#wordStage { background:$poster; border:0; border-radius:12px; }
    QFrame#wordStage QLabel#word { color:$posterInk; background:transparent; }
    QLabel#challengeType { background:transparent; padding:0; color:$text; font-size:13px; }
    QPushButton#cardBrand { font-size:16px; font-weight:500; color:$muted; padding:4px 0; border:0; background:transparent; }
    QPushButton#cardBrand:focus { border:0; color:$text; font-weight:600; }
    QPushButton#cardBrand:focus:pressed { border:1px solid $action; }
    QPushButton#listenPronunciation, QPushButton#exampleToggle { font-size:12px; }
  )")
             .replace("$posterInk", c.posterInk)
             .replace("$actionInk", c.actionInk)
             .replace("$poster", c.poster)
             .replace("$action", c.action)
             .replace("$accent", c.accent)
             .replace("$surface", c.surface)
             .replace("$border", c.border)
             .replace("$muted", c.muted)
             .replace("$hover", c.hover)
             .replace("$rail", c.rail)
             .replace("$warm", c.warm)
             .replace("$negative", c.negative)
             .replace("$text", c.text);
  // Pigment gradients add depth without changing control geometry or hit areas.
  const QColor pigment(c.action);
  const QString top = pigment.lighter(107).name();
  const QString bottom = pigment.darker(105).name();
  css += QString("QPushButton[role=\"primary\"] { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,stop:1 %2); }"
                 "QWidget#dashboardPaper, QWidget#dashboardPages, QTabWidget#dashboardTabs, "
                 "QWidget#studioHome, QWidget#homeWordCollection, "
                 "QScrollArea#homeScroll, QWidget#homeViewport { background:transparent; }"
                 "QWidget#wordTilesPage, QWidget#wordTileCanvas, QWidget#practiceModes, QWidget#modeIntro, QWidget#modeExercise, "
                 "QWidget#historyPanel, QWidget#settingsHub, QWidget#settingsBody, "
                 "QWidget#settingsViewport, QScrollArea#studioSettingsScroll, QWidget#submittedRow { background:transparent; }"
                 "QPushButton[role=\"choice\"] { padding:6px 14px; }"
                 "QFrame#practiceSession, QFrame#wordStage { background:transparent; }"
                 "QLabel#homeHeading { font-size:30px; }"
                 "QLabel#collectionHeading { font-size:23px; }"
                 "QPushButton[role=\"section\"]:checked { background:%3; }")
             .arg(top, bottom, c.poster);
  const QString cardBackground = QColor(c.background).darker(theme % 2 ? 112 : 100).name();
  css += QString("QWidget#practiceCard, QWidget#cardContent, QScrollArea#cardScroll, QWidget#cardViewport { background:%1; }"
                 "QPushButton[role=\"primary\"], QWidget#studioHeader QPushButton:checked { font-weight:500; }"
                 "QPushButton#markMissed { background:%1; color:%2; border-color:%3; font-weight:400; }"
                 "QPushButton#markMissed:hover { background:%4; }"
                 "QPushButton#markRemembered { font-weight:500; }")
             .arg(cardBackground, c.negative, c.border, c.surface);
  const auto gradient = [](QColor color) {
    return QString("qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,stop:1 %2)")
        .arg(color.lighter(107).name(), color.darker(105).name());
  };
  QColor quietTop(c.surface), quietBottom(c.surface);
  quietTop.setAlpha(32); quietBottom.setAlpha(14);
  const auto rgba = [](const QColor &color) {
    return QString("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
  };
  css += QString(R"(
    QPushButton, QPushButton[role="control"] { background:$surfaceWash; }
    QPushButton:hover, QPushButton[role="control"]:hover { background:$hoverWash; }
    QPushButton:pressed, QPushButton[role="control"]:pressed { background:$pressedWash; color:$text; }
    QPushButton:disabled { background:$surfaceWash; color:$muted; }
    QPushButton[role="quiet"] { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 $quietTop,stop:1 $quietBottom); }
    QPushButton[role="quiet"]:hover { background:$hoverWash; }
    QPushButton[role="quiet"]:pressed { background:$pressedWash; }
    QPushButton[role="section"] { background:transparent; border:1px solid transparent; border-radius:8px; }
    QPushButton[role="section"]:checked { background:$surfaceWash; border:1px solid $action; }
    QPushButton[role="section"]:focus { border:2px solid $action; }
    QWidget#studioHeader QPushButton:hover { background:$hoverWash; }
    QWidget#studioHeader QPushButton:checked { background:$actionWash; }
    QPushButton[role="primary"] { background:$actionWash; }
    QPushButton[role="primary"]:hover { background:$actionHoverWash; }
    QPushButton[role="primary"]:pressed { background:$actionPressedWash; color:$actionInk; }
    QPushButton[role="primary"]:disabled { background:$surfaceWash; color:$muted; border-color:$border; }
    QPushButton#cardBrand, QPushButton#dismiss { background:transparent; }
    QPushButton#cardBrand:pressed, QPushButton#dismiss:pressed { background:$pressedWash; }
    QListWidget { border:1px solid $border; border-radius:8px; padding:8px; }
    QLabel#homeWordsEmpty { background:rgba($emptyRed,$emptyGreen,$emptyBlue,26); border-radius:8px; }
  )").replace("$surfaceWash", gradient(QColor(c.surface)))
      .replace("$hoverWash", gradient(QColor(c.hover)))
      .replace("$pressedWash", gradient(QColor(c.surface).darker(110)))
      .replace("$actionHoverWash", gradient(pigment.lightness() > 128 ? pigment.lighter(108) : pigment.darker(108)))
      .replace("$actionPressedWash", gradient(pigment.darker(110)))
      .replace("$actionWash", gradient(pigment))
      .replace("$quietTop", rgba(quietTop)).replace("$quietBottom", rgba(quietBottom))
      .replace("$emptyRed", QString::number(QColor(c.surface).red()))
      .replace("$emptyGreen", QString::number(QColor(c.surface).green()))
      .replace("$emptyBlue", QString::number(QColor(c.surface).blue()))
      .replace("$actionInk", c.actionInk).replace("$action", c.action)
      .replace("$border", c.border).replace("$muted", c.muted).replace("$text", c.text);
  css += QString("QPushButton#markMissed { background:%1; } QPushButton#markMissed:hover { background:%2; } QPushButton#markMissed:pressed { background:%3; }")
      .arg(gradient(QColor(cardBackground)), gradient(QColor(c.surface)), gradient(QColor(cardBackground).darker(110)));
  return css;
}
QIcon appIcon() {
  QIcon icon;
  for (int size : {16, 22, 24, 32, 48, 64, 128, 256, 512})
    icon.addFile(QStringLiteral(":/brand/moji-nook-%1.png").arg(size), QSize(size, size));
  return icon;
}
QIcon brandMark(int theme) {
  const auto c = colors(theme);
  QIcon icon;
  for (int size : {16, 24, 32, 48, 64, 128}) {
    QPixmap mark(size, size);
    mark.fill(Qt::transparent);
    QPainter painter(&mark);
    for (const auto &layer : {QStringLiteral("support"), QStringLiteral("dominant")}) {
      QPixmap mask(QStringLiteral(":/brand/mark-%1-%2.png").arg(layer).arg(size));
      QPainter tint(&mask);
      tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
      tint.fillRect(mask.rect(), QColor(layer == "support" ? c.muted : c.text));
      tint.end();
      painter.drawPixmap(0, 0, mask);
    }
    painter.end();
    icon.addPixmap(mark);
  }
  return icon;
}
