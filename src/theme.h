#pragma once
#include <QtGui>

// Appearance families, each ordered day then night.
struct Colors {
  QString background, surface, hover, border, text, muted, accent, warm,
      negative, ink;
  QString action, actionInk, rail, poster, posterInk, series2, series3, series1;
};
inline constexpr int themeCount = 4;
inline constexpr int defaultTheme = 3; // Paper · dark
inline Colors colors(int index) {
  static const QList<Colors> palettes = {
      // Ink: warm mineral light, forest-charcoal night, restrained earth actions.
      {"#f5f0e5", "#fffaf0", "#e9e1d1", "#878c83", "#252a27", "#5e645d",
       "#35624e", "#79571f", "#9e383e", "#fffefa", "#874b36", "#fffaf4",
       "#e9dfcc", "#e4d9c2", "#292d29", "#35624e", "#79571f", "#874b36"},
      {"#101a17", "#202d27", "#35473c", "#7b867d", "#eeeee6", "#b5bfb4",
       "#a2c6a6", "#dec17b", "#eca3a0", "#181b19", "#d6b99a", "#2e261e",
       "#101a17", "#35493d", "#f1f0e7", "#a2c6a6", "#dec17b", "#d6b99a"},
      // Office Paper: neutral surfaces, with color for feedback and subject cues.
      {"#ffffff", "#eeeeee", "#dddddd", "#808080", "#202020", "#555555",
       "#315d42", "#555555", "#9a3030", "#ffffff", "#303030", "#ffffff",
       "#f4f4f4", "#e5e5e5", "#202020", "#858585", "#555555", "#222222"},
      {"#202020", "#303030", "#424242", "#909090", "#f2f2f2", "#bfbfbf",
       "#a8cdb4", "#c6c6c6", "#efa7a7", "#202020", "#dddddd", "#202020",
       "#181818", "#383838", "#f2f2f2", "#909090", "#bbbbbb", "#ffffff"}};
  auto c = palettes[qBound(0, index, themeCount - 1)];
  if (c.series1.isEmpty())
    c.series1 = c.accent;
  return c;
}
inline int pairedTheme(int index, bool night) {
  return index - (index % 2) + int(night);
}

// WaniKani subject hues are accents, mixed into the current theme's ink.
inline QColor wanikaniSubjectHue(const QString &kind, int theme) {
  return kind == "kanji" ? QColor(theme % 2 ? "#f26da0" : "#cd397a")
                         : QColor(theme % 2 ? "#9367f3" : "#6838c7");
}
inline QColor wanikaniSubjectInk(const QString &kind, int theme) {
  const QColor ink(colors(theme).text), hue = wanikaniSubjectHue(kind, theme);
  return QColor::fromRgbF(ink.redF() * .48 + hue.redF() * .52,
                         ink.greenF() * .48 + hue.greenF() * .52,
                         ink.blueF() * .48 + hue.blueF() * .52);
}
inline int migratedTheme(int oldIndex) {
  if (oldIndex == 4 || oldIndex == 5)
    return oldIndex - 4;
  if (oldIndex == 8 || oldIndex == 9)
    return oldIndex - 6;
  // Removed themes fall back to Cherry Farm, retaining day/night intent.
  return oldIndex == 1 || oldIndex == 2 || oldIndex == 7 ? 1 : 0;
}

inline int simplifiedTheme(int previousIndex) {
  return previousIndex == 6 || previousIndex == 7
             ? previousIndex - 4
             : qBound(0, previousIndex, 9) % 2;
}
