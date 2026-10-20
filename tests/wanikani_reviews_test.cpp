#include "wanikani_reviews.h"
#include <QtTest>

namespace {
Subject subject(const QString &kind, QStringList meanings,
                QStringList readings = {}) {
  Subject s;
  s.id = 1;
  s.kind = kind;
  s.characters = "人";
  s.meanings = meanings;
  s.readings = readings;
  return s;
}
ReviewVerdict meaning(const Subject &s, const QString &text) {
  return checkReviewAnswer(s, false, text).verdict;
}
ReviewVerdict reading(const Subject &s, const QString &text) {
  return checkReviewAnswer(s, true, text).verdict;
}
} // namespace

class WaniKaniReviewsTest : public QObject {
  Q_OBJECT
private slots:
  void exactAndSynonymMeanings() {
    const auto s = subject("vocabulary", {"Person", "Human Being", "Dude"});
    QCOMPARE(meaning(s, "person"), ReviewVerdict::Correct);
    QCOMPARE(meaning(s, "  HUMAN being "), ReviewVerdict::Correct);
    QCOMPARE(meaning(s, "dude"), ReviewVerdict::Correct);
    QCOMPARE(meaning(s, "tree"), ReviewVerdict::Incorrect);
  }
  void typoToleranceFollowsCandidateLength() {
    QCOMPARE(meaningTypoTolerance(3), 0);
    QCOMPARE(meaningTypoTolerance(4), 1);
    QCOMPARE(meaningTypoTolerance(5), 1);
    QCOMPARE(meaningTypoTolerance(6), 2);
    QCOMPARE(meaningTypoTolerance(7), 2);
    QCOMPARE(meaningTypoTolerance(8), 3);
    QCOMPARE(meaningTypoTolerance(14), 4);
    // Up to three letters: exact only.
    QCOMPARE(meaning(subject("kanji", {"Eye"}), "eyz"), ReviewVerdict::Incorrect);
    // Four or five letters: one edit.
    const auto house = subject("kanji", {"House"});
    QCOMPARE(meaning(house, "hause"), ReviewVerdict::Correct);
    QCOMPARE(meaning(house, "hauze"), ReviewVerdict::Incorrect);
    // Six or seven letters: two edits.
    const auto flower = subject("kanji", {"Flower"});
    QCOMPARE(meaning(flower, "fxxwer"), ReviewVerdict::Correct);
    QCOMPARE(meaning(flower, "xxxwer"), ReviewVerdict::Incorrect);
    // Eight or more: 2 + length / 7.
    const auto mountain = subject("kanji", {"Mountain"});
    QCOMPARE(meaning(mountain, "mxxxtain"), ReviewVerdict::Correct);
    QCOMPARE(meaning(mountain, "mxxxxain"), ReviewVerdict::Incorrect);
    const auto electricity = subject("vocabulary", {"Electricity!"});
    QCOMPARE(meaning(electricity, "elxxxricity"), ReviewVerdict::Correct);
  }
  void digitsGetNoTolerance() {
    const auto s = subject("kanji", {"Mountain"});
    QCOMPARE(meaning(s, "mountai1"), ReviewVerdict::Incorrect);
    QCOMPARE(meaning(subject("vocabulary", {"10 Days"}), "10 days"),
             ReviewVerdict::Correct);
  }
  void blacklistBeatsTypoTolerance() {
    auto s = subject("kanji", {"Week"});
    QCOMPARE(meaning(s, "weak"), ReviewVerdict::Correct);
    s.blacklist = {"Weak"};
    QCOMPARE(meaning(s, "weak"), ReviewVerdict::Incorrect);
    QCOMPARE(meaning(s, "week"), ReviewVerdict::Correct);
  }
  void kanaInMeaningAsksAgain() {
    const auto s = subject("vocabulary", {"Person"}, {"ひと"});
    const auto check = checkReviewAnswer(s, false, "ひと");
    QCOMPARE(check.verdict, ReviewVerdict::Retry);
    QCOMPARE(check.hint, QString("WaniKani wants the meaning, not the reading."));
    QCOMPARE(meaning(s, "人"), ReviewVerdict::Retry);
  }
  void readings() {
    const auto s = subject("vocabulary", {"Person"}, {"ひと"});
    QCOMPARE(reading(s, "hito"), ReviewVerdict::Correct);
    QCOMPARE(reading(s, "ひと"), ReviewVerdict::Correct);
    QCOMPARE(reading(s, "ヒト"), ReviewVerdict::Correct);
    QCOMPARE(reading(s, "じん"), ReviewVerdict::Incorrect);
    const auto latin = checkReviewAnswer(s, true, "hitq");
    QCOMPARE(latin.verdict, ReviewVerdict::Retry);
    QCOMPARE(latin.hint, QString("Type the reading in kana."));
    const auto katakana = subject("vocabulary", {"Bread"}, {"パン"});
    QCOMPARE(reading(katakana, "pan"), ReviewVerdict::Correct);
  }
  void kanjiOtherReadingAsksForWantedType() {
    auto s = subject("kanji", {"Person"}, {"じん", "にん"});
    s.readingKind = "onyomi";
    s.otherReadings = {"ひと"};
    const auto check = checkReviewAnswer(s, true, "hito");
    QCOMPARE(check.verdict, ReviewVerdict::Retry);
    QCOMPARE(check.hint, QString("WaniKani wants the on’yomi reading."));
    QCOMPARE(reading(s, "nin"), ReviewVerdict::Correct);
    QCOMPARE(reading(s, "かみ"), ReviewVerdict::Incorrect);
    s.kind = "vocabulary";
    QCOMPARE(reading(s, "hito"), ReviewVerdict::Incorrect);
  }
  void challengeDelegatesOnlyForReviews() {
    Challenge c;
    c.subject = subject("kanji", {"House"});
    c.mode = Mode::Typing;
    QVERIFY(!c.accepts("hause"));
    QVERIFY(c.readingRetryHint("いえ").isEmpty());
    c.wanikaniReview = true;
    QVERIFY(c.accepts("hause"));
    QCOMPARE(c.readingRetryHint("いえ"),
             QString("WaniKani wants the meaning, not the reading."));
    QVERIFY(c.readingRetryHint("tree").isEmpty());
  }
  void itemFlowCountsMissesNotRetries() {
    ReviewItem item;
    item.subject = subject("kanji", {"Person"}, {"じん"});
    QVERIFY(item.needsReading());
    item.record(false, false);
    item.record(false, false);
    QVERIFY(!item.meaningDone);
    item.record(false, true);
    item.record(true, false);
    QVERIFY(!item.complete());
    item.record(true, true);
    QVERIFY(item.complete());
    QCOMPARE(item.meaningMisses, 2);
    QCOMPARE(item.readingMisses, 1);
  }
  void radicalAndKanaVocabularyAreMeaningOnly() {
    for (const auto kind : {"radical", "kana_vocabulary"}) {
      ReviewItem item;
      item.subject = subject(kind, {"Ground"});
      QVERIFY(!item.needsReading());
      item.record(true, false);
      QCOMPARE(item.readingMisses, 0);
      item.record(false, true);
      QVERIFY(item.complete());
    }
  }
  void stageLabels() {
    QCOMPARE(reviewStageLabel(4), QString("Apprentice 4"));
    QCOMPARE(reviewStageLabel(5), QString("Guru 1"));
    QCOMPARE(reviewStageLabel(7), QString("Master"));
    QCOMPARE(reviewStageLabel(9), QString("Burned"));
  }
};

QTEST_GUILESS_MAIN(WaniKaniReviewsTest)
#include "wanikani_reviews_test.moc"
