#include "core.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

// Floors need and format fit so every eligible skill stays drawable.
static constexpr double minFactor = 0.05;

static int editDistance(const QString &a, const QString &b) {
  QVector<int> row(b.size() + 1);
  for (int j = 0; j <= b.size(); ++j)
    row[j] = j;
  for (int i = 1; i <= a.size(); ++i) {
    int diagonal = row[0];
    row[0] = i;
    for (int j = 1; j <= b.size(); ++j) {
      int old = row[j];
      row[j] = qMin(qMin(row[j] + 1, row[j - 1] + 1),
                    diagonal + (a[i - 1] != b[j - 1]));
      diagonal = old;
    }
  }
  return row.last();
}

QStringList Picker::distractors(const Subject &s, bool reading,
                                const QVector<Subject> &pool) {
  struct Candidate {
    double score;
    QString text;
    QStringList aliases;
  };
  QVector<Candidate> candidates;
  auto norm = [reading](const QString &t) {
    return reading ? toKana(t) : normalizeMeaning(t);
  };
  QSet<QString> forbidden;
  for (const auto &a : reading ? s.readings : s.meanings)
    forbidden.insert(norm(a));
  const QString primary = norm(reading ? s.reading() : s.meaning());
  const auto words = reading ? QStringList() : primary.split(' ');
  for (const auto &other : pool) {
    if (other.id == s.id || other.kind != s.kind)
      continue;
    const auto aliases = reading ? other.readings : other.meanings;
    if (aliases.isEmpty())
      continue;
    bool overlap = false;
    QStringList normalizedAliases;
    for (const auto &a : aliases) {
      normalizedAliases.append(norm(a));
      if (forbidden.contains(normalizedAliases.last()))
        overlap = true;
    }
    if (overlap)
      continue;
    double score = 0;
    if (s.similar.contains(other.id) || other.similar.contains(s.id))
      score += 8;
    for (int c : s.components)
      if (other.components.contains(c))
        score += 2;
    for (QChar c : s.characters)
      if (c.unicode() >= 0x3400 && other.characters.contains(c))
        score += 3;
    if (reading) {
      int distance = editDistance(primary, normalizedAliases.first());
      if (distance == 1)
        score += 5;
      else if (distance == 2 && s.reading().size() >= 3)
        score += 2;
    } else {
      const auto otherWords = normalizedAliases.first().split(' ');
      for (const auto &w : words)
        if (w.size() > 2 && otherWords.contains(w))
          score += 2;
    }
    if (score >= 2)
      candidates.append({score + random.generateDouble(), aliases.first(),
                         normalizedAliases});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const auto &a, const auto &b) { return a.score > b.score; });
  QStringList out;
  for (const auto &c : candidates) {
    bool overlap = false;
    for (const auto &a : c.aliases)
      if (forbidden.contains(a))
        overlap = true;
    if (overlap)
      continue;
    out.append(c.text);
    for (const auto &a : c.aliases)
      forbidden.insert(a);
    if (out.size() == 3)
      break;
  }
  return out;
}

Challenge Picker::next(const QVector<Subject> &pool, const QList<int> &recent,
                       const Evidence &evidence, const Memory &memory,
                       const SelectionMix &mix, const QDateTime &now) {
  if (pool.isEmpty())
    throw std::invalid_argument("No learned subjects");
  QSet<int> excluded;
  const int cooldown = qMin(5, qMax(0, int(pool.size()) - 1));
  for (int id : recent) {
    if (excluded.size() >= cooldown)
      break;
    if (std::any_of(pool.begin(), pool.end(),
                    [id](const auto &s) { return s.id == id; }))
      excluded.insert(id);
  }
  Challenge c;
  const int recallShare = qBound(0, mix.recall, 100);
  const int typedShare = qBound(0, mix.typed, 100 - recallShare);
  const int mode = random.bounded(100);
  c.mode = mode < recallShare                ? Mode::Recall
           : mode < recallShare + typedShare ? Mode::Typing
                                             : Mode::Choice;

  struct Skill {
    const Subject *subject;
    bool reading;
    QString key;
    double recall;
    bool rested;
  };
  QVector<Skill> skills;
  const QDate today = now.toLocalTime().date();
  // Local-time arithmetic converts zones on every call; convert once.
  const QDateTime utc = now.toUTC();
  for (const auto &s : pool)
    if (!excluded.contains(s.id))
      for (bool reading : {false, true})
        if (!reading || !s.readings.isEmpty()) {
          const auto key = skillKey(s.id, reading);
          const auto m = memory.value(key);
          skills.append({&s, reading, key, recallProbability(m, utc),
                         rested(m, utc, today)});
        }
  if (c.mode == Mode::Typing &&
      std::none_of(skills.begin(), skills.end(),
                   [](const Skill &k) { return k.reading; }))
    c.mode = Mode::Recall;

  QVector<const Subject *> newest;
  for (const auto &s : pool)
    if (s.startedAt.isValid())
      newest.append(&s);
  std::sort(newest.begin(), newest.end(),
            [](const Subject *a, const Subject *b) {
              return a->startedAt == b->startedAt ? a->id < b->id
                                                  : a->startedAt > b->startedAt;
            });
  // Establish the newest 30 before applying cooldown, never backfill older
  // items.
  QSet<int> newestIds;
  for (int i = 0; i < qMin(30, int(newest.size())); ++i)
    newestIds.insert(newest[i]->id);
  auto stageWeight = [&mix](int stage) {
    if (stage < 1 || stage > 9 || mix.stageWeights.size() != 5)
      return ::stageWeight(stage);
    const int group = stage <= 4 ? 0 : stage <= 6 ? 1 : stage - 5;
    return qBound(.1, mix.stageWeights[group], 10.0);
  };
  using Weight = std::function<double(const Skill &)>;
  struct Bucket {
    QString reason;
    int share;
    Weight weight; // Zero leaves a skill out of the bucket.
  };
  const QVector<Bucket> buckets = {
      {"Newest 30", mix.newest,
       [&](const Skill &k) {
         return newestIds.contains(k.subject->id) ? 1. : 0.;
       }},
      {"Recent struggles", mix.recent,
       [&](const Skill &k) { return evidence.value(k.key).recent; }},
      {"Persistent trouble", mix.persistent,
       [&](const Skill &k) { return evidence.value(k.key).persistent; }},
      // Anki keeps scheduling long-interval cards; only WaniKani burns items.
      {"Burned review", mix.burned,
       [](const Skill &k) {
         return k.subject->stage == 9 && k.subject->source == "wanikani" ? 1.
                                                                         : 0.;
       }},
      {"Broad recall", 100,
       [&](const Skill &k) { return stageWeight(k.subject->stage); }},
  };
  const Bucket *bucket = &buckets.last();
  int roll = random.bounded(100), left = 100;
  for (const auto &b : buckets) {
    const int share = qBound(0, b.share, left);
    if (roll < share) {
      bucket = &b;
      break;
    }
    roll -= share;
    left -= share;
  }

  // Four choices suit fragile skills; typing suits sturdier ones.
  auto formatFit = [&c](double p) {
    return c.mode == Mode::Choice ? 1 - p : c.mode == Mode::Typing ? p : 1.;
  };
  auto draw = [&](const Weight &base, bool restGates) -> const Skill * {
    QVector<std::pair<const Skill *, double>> candidates;
    double total = 0;
    for (const auto &k : skills) {
      if ((c.mode == Mode::Typing && !k.reading) || (restGates && k.rested))
        continue;
      const double w = base(k) * qMax(minFactor, 1 - k.recall) *
                       qMax(minFactor, formatFit(k.recall));
      if (w > 0 && std::isfinite(w)) {
        candidates.append({&k, w});
        total += w;
      }
    }
    double target = random.generateDouble() * total;
    for (const auto &[k, w] : candidates)
      if ((target -= w) < 0)
        return k;
    return candidates.isEmpty() ? nullptr : candidates.last().first;
  };
  const Skill *pick = draw(bucket->weight, true);
  if (!pick && bucket != &buckets.last()) {
    bucket = &buckets.last();
    pick = draw(bucket->weight, true);
  }
  // Everything is resting: practice whatever needs it most.
  if (!pick)
    pick = draw([](const Skill &) { return 1.; }, false);
  c.subject = *pick->subject;
  c.reading = pick->reading;
  c.selectionReason = bucket->reason;
  if (c.mode == Mode::Choice) {
    c.choices = distractors(c.subject, c.reading, pool);
    if (c.choices.size() != 3) {
      c.mode = Mode::Recall;
      c.choices.clear();
    } else {
      c.choices.append(c.reading ? c.subject.reading() : c.subject.meaning());
      std::shuffle(c.choices.begin(), c.choices.end(), random);
    }
  }
  return c;
}
