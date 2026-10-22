#include "core.h"
#include <algorithm>
#include <cmath>

// Heuristics, kept together so they can be tuned against logged estimates.
static constexpr struct {
  double scheduledRecall = 0.9; // WaniKani schedules a review near this recall.
  double burnedIntervalHours = 8760;
  double strongGrowth = 2.0, weakGrowth = 0.8, missShrink = 0.35;
  double minHalfLifeDays = 10.0 / (24 * 60), maxHalfLifeDays = 5 * 365.0;
  double unknownRecall = 0.5;
  int restMissesPerDay = 3;
  qint64 restAfterMissSecs = 10 * 60;
  double restAboveRecall = 0.95;
} kRecall{};

static double clampHalfLife(double days) {
  return qBound(kRecall.minHalfLifeDays, days, kRecall.maxHalfLifeDays);
}

static double seedHalfLifeDays(const Subject &s) {
  const double hours = s.intervalHours > 0 ? s.intervalHours
                       : s.stage == 9      ? kRecall.burnedIntervalHours
                                           : srsIntervalHours(s.stage);
  return clampHalfLife(hours / 24 * std::log(2.0) /
                       -std::log(kRecall.scheduledRecall));
}

double recallProbability(const SkillMemory &m, const QDateTime &now) {
  if (!m.lastSeen.isValid() || m.halfLifeDays <= 0)
    return kRecall.unknownRecall;
  const double days = qMax<qint64>(0, m.lastSeen.msecsTo(now)) / 86400000.0;
  return std::exp2(-days / m.halfLifeDays);
}

bool rested(const SkillMemory &m, const QDateTime &now, const QDate &today) {
  return m.lastCorrectDay == today ||
         m.missesToday >= kRecall.restMissesPerDay ||
         (m.lastMissAt.isValid() &&
          m.lastMissAt.secsTo(now) < kRecall.restAfterMissSecs) ||
         recallProbability(m, now) > kRecall.restAboveRecall;
}

Memory estimateMemory(const QVector<Subject> &pool,
                      const QVector<GradedAttempt> &attempts,
                      const QDateTime &now, const QDate &today) {
  Memory out;
  QHash<int, const Subject *> subjects;
  for (const auto &s : pool) {
    subjects.insert(s.id, &s);
    for (bool reading : {false, true})
      if (!reading || !s.readings.isEmpty())
        out.insert(skillKey(s.id, reading),
                   {seedHalfLifeDays(s), s.reviewedAt});
  }
  QVector<const GradedAttempt *> ordered;
  ordered.reserve(attempts.size());
  for (const auto &a : attempts)
    ordered.append(&a);
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const auto *a, const auto *b) { return a->at < b->at; });
  for (const auto *a : ordered) {
    const Subject *s = subjects.value(a->subjectId);
    // The source's own review already reflects anything practiced before it.
    if (!s || a->at > now ||
        (s->reviewedAt.isValid() && a->at <= s->reviewedAt))
      continue;
    auto it = out.find(skillKey(a->subjectId, a->reading));
    if (it == out.end())
      continue;
    const double p = recallProbability(*it, a->at);
    const double growth = a->strong ? kRecall.strongGrowth : kRecall.weakGrowth;
    it->halfLifeDays =
        clampHalfLife(it->halfLifeDays *
                      (a->correct ? 1 + growth * (1 - p) : kRecall.missShrink));
    it->lastSeen = a->at;
    if (a->correct) {
      it->lastCorrectDay = a->localDay;
    } else {
      it->lastMissAt = a->at;
      it->missesToday += a->localDay == today;
    }
  }
  return out;
}
