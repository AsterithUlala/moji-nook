#include "wanikani_reviews.h"

namespace {
bool hasJapanese(const QString &text) {
  for (const QChar c : text) {
    const ushort u = c.unicode();
    if ((u >= 0x3040 && u <= 0x30ff) || (u >= 0x3400 && u <= 0x4dbf) ||
        (u >= 0x4e00 && u <= 0x9fff) || (u >= 0xff66 && u <= 0xff9f))
      return true;
  }
  return false;
}
bool hasLatin(const QString &text) {
  for (const QChar c : text)
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
      return true;
  return false;
}
int distance(const QString &a, const QString &b) {
  QVector<int> row(b.size() + 1);
  for (int j = 0; j <= b.size(); ++j)
    row[j] = j;
  for (int i = 1; i <= a.size(); ++i) {
    int diagonal = row[0];
    row[0] = i;
    for (int j = 1; j <= b.size(); ++j) {
      const int above = row[j];
      row[j] = qMin(qMin(row[j] + 1, row[j - 1] + 1),
                    diagonal + (a[i - 1] == b[j - 1] ? 0 : 1));
      diagonal = above;
    }
  }
  return row[b.size()];
}
QString readingTypeName(const QString &kind) {
  return kind == "onyomi"    ? QString("on’yomi")
         : kind == "kunyomi" ? QString("kun’yomi")
         : kind == "nanori"  ? QString("nanori")
                             : QString();
}
} // namespace

int meaningTypoTolerance(int length) {
  return length <= 3 ? 0 : length <= 5 ? 1 : length <= 7 ? 2 : 2 + length / 7;
}

QString reviewStageLabel(int stage) {
  if (stage >= 1 && stage <= 4)
    return QString("Apprentice %1").arg(stage);
  if (stage == 5 || stage == 6)
    return QString("Guru %1").arg(stage - 4);
  return stage <= 0 ? QString("Lesson") : stageName(stage);
}

ReviewCheck checkReviewAnswer(const Subject &subject, bool reading,
                              const QString &text) {
  if (text.trimmed().isEmpty())
    return {ReviewVerdict::Retry, "Type an answer first."};
  if (!reading) {
    if (hasJapanese(text))
      return {ReviewVerdict::Retry,
              "WaniKani wants the meaning, not the reading."};
    const auto answer = normalizeMeaning(text);
    for (const auto &m : subject.meanings)
      if (answer == normalizeMeaning(m))
        return {ReviewVerdict::Correct, {}};
    for (const auto &m : subject.blacklist)
      if (answer == normalizeMeaning(m))
        return {ReviewVerdict::Incorrect, {}};
    static const QRegularExpression digit("\\d");
    if (answer.contains(digit))
      return {ReviewVerdict::Incorrect, {}};
    for (const auto &m : subject.meanings) {
      const auto candidate = normalizeMeaning(m);
      if (distance(answer, candidate) <=
          meaningTypoTolerance(int(candidate.size())))
        return {ReviewVerdict::Correct, {}};
    }
    return {ReviewVerdict::Incorrect, {}};
  }
  const auto kana = toKana(text);
  if (hasLatin(kana))
    return {ReviewVerdict::Retry, "Type the reading in kana."};
  for (const auto &r : subject.readings)
    if (kana == toKana(r))
      return {ReviewVerdict::Correct, {}};
  if (subject.kind == "kanji") {
    const auto wanted = readingTypeName(subject.readingKind);
    for (const auto &r : subject.otherReadings)
      if (kana == toKana(r) && !wanted.isEmpty())
        return {ReviewVerdict::Retry,
                QString("WaniKani wants the %1 reading.").arg(wanted)};
  }
  return {ReviewVerdict::Incorrect, {}};
}
