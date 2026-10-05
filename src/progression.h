#pragma once
#include "core.h"

enum class TilePhase { Seen, Growing, Settled };
struct ProgressTile {
  int id = 0;
  QString characters, reading, meaning, source, subjectKind;
  bool eligible = false, settled = false;
  bool readingChallenge = false, meaningChallenge = false;
  int readingSuccesses = 0, meaningSuccesses = 0;
  double progress = 0;
  // Latest graded attempts.id, for recency ordering; not a timestamp.
  qint64 lastAttempt = 0;
  bool challenged() const { return readingChallenge || meaningChallenge; }
  TilePhase phase = TilePhase::Seen;
};
struct ProgressNotice {
  enum Kind { Developed, Completed, ChallengeCleared, Consistency };
  Kind kind = Developed;
  int subjectId = 0;
  QString characters;
  int practiceDays = 0;
};
struct ProgressState {
  QVector<ProgressTile> tiles;
  int settled = 0, total = 0, practiceDays = 0;
};
class Progression {
public:
  explicit Progression(const QSqlDatabase &database);
  bool refresh(const QVector<Subject> &pool = {}, bool notify = false);
  const ProgressState &state() const { return state_; }
  std::optional<ProgressTile> tile(int id) const;
  QVector<ProgressNotice> takeNotices();
  QString error;

private:
  QSqlDatabase db_;
  ProgressState state_;
  QVector<ProgressNotice> notices_;
  bool initialized_ = false;
};
