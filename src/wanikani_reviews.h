#pragma once
#include "core.h"

enum class ReviewVerdict { Correct, Incorrect, Retry };
struct ReviewCheck {
  ReviewVerdict verdict;
  QString hint;
};
// WaniKani's answer rules for one half of a review item.
ReviewCheck checkReviewAnswer(const Subject &subject, bool reading,
                              const QString &text);
int meaningTypoTolerance(int length);
QString reviewStageLabel(int stage);

struct ReviewItem {
  int assignmentId = 0;
  QString availableAt; // identifies this review instance for saved progress
  Subject subject;
  bool meaningDone = false, readingDone = false;
  int meaningMisses = 0, readingMisses = 0;
  bool needsReading() const {
    return subject.kind != "radical" && subject.kind != "kana_vocabulary";
  }
  bool complete() const {
    return meaningDone && (readingDone || !needsReading());
  }
  QString key() const {
    return QString::number(assignmentId) + "@" + availableAt;
  }
  // An incorrect answer adds a miss and leaves that half to be asked again.
  void record(bool reading, bool correct) {
    if (reading && !needsReading())
      return;
    (reading ? readingDone : meaningDone) |= correct;
    if (!correct)
      ++(reading ? readingMisses : meaningMisses);
  }
};
