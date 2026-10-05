#include "core.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
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

Challenge Picker::next(const QVector<Subject> &pool,
                       const QHash<int, double> &boosts,
                       const QList<int> &recent, const Evidence &evidence,
                       int recentShare, int persistentShare, int recallShare,
                       int typedShare, const QVector<double> &masteryWeights,
                       int newestShare) {
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
  double total = 0;
  auto weight = [&masteryWeights](int stage) {
    if (stage < 1 || stage > 9 || masteryWeights.size() != 5)
      return stageWeight(stage);
    const int group = stage <= 4 ? 0 : stage <= 6 ? 1 : stage - 5;
    return qBound(.1, masteryWeights[group], 10.0);
  };
  for (const auto &s : pool)
    if (!excluded.contains(s.id))
      total += weight(s.stage) * boosts.value(s.id, 1);
  double roll = random.generateDouble() * total;
  Challenge c;
  for (const auto &s : pool)
    if (!excluded.contains(s.id)) {
      c.subject = s;
      roll -= weight(s.stage) * boosts.value(s.id, 1);
      if (roll < 0)
        break;
    }
  const int mode = random.bounded(100);
  recallShare = qBound(0, recallShare, 100);
  typedShare = qBound(0, typedShare, 100 - recallShare);
  c.mode = mode < recallShare                ? Mode::Recall
           : mode < recallShare + typedShare ? Mode::Typing
                                             : Mode::Choice;
  c.reading = !c.subject.readings.isEmpty() &&
              (c.mode == Mode::Typing || random.bounded(2) == 0);
  const int bucket = random.bounded(100);
  newestShare = qBound(0, newestShare, 100);
  recentShare = qBound(0, recentShare, 100 - newestShare);
  persistentShare = qBound(0, persistentShare, 100 - newestShare - recentShare);
  const bool useRecent = bucket < newestShare + recentShare;
  if (bucket < newestShare) {
    QVector<const Subject *> newest;
    for (const auto &s : pool)
      if (s.startedAt.isValid())
        newest.append(&s);
    std::sort(
        newest.begin(), newest.end(), [](const Subject *a, const Subject *b) {
          return a->startedAt == b->startedAt ? a->id < b->id
                                              : a->startedAt > b->startedAt;
        });
    // Establish the newest 30 before applying cooldown, never backfill older
    // items.
    if (newest.size() > 30)
      newest.resize(30);
    newest.erase(std::remove_if(newest.begin(), newest.end(),
                                [&excluded](const Subject *s) {
                                  return excluded.contains(s->id);
                                }),
                 newest.end());
    if (!newest.isEmpty()) {
      c.subject = *newest[random.bounded(int(newest.size()))];
      c.reading = !c.subject.readings.isEmpty() &&
                  (c.mode == Mode::Typing || random.bounded(2) == 0);
      c.selectionReason = "Newest 30";
    }
  } else if (bucket < newestShare + recentShare + persistentShare &&
             !evidence.isEmpty()) {
    struct Candidate {
      const Subject *subject;
      bool reading;
      double weight;
    };
    QVector<Candidate> candidates;
    double sum = 0;
    for (const auto &s : pool) {
      if (excluded.contains(s.id))
        continue;
      for (bool reading : {false, true}) {
        if ((reading && s.readings.isEmpty()) ||
            (!reading && c.mode == Mode::Typing))
          continue;
        const auto e = evidence.value(skillKey(s.id, reading));
        const double weight = useRecent ? e.recent : e.persistent;
        if (weight > 0 && std::isfinite(weight)) {
          candidates.append({&s, reading, weight});
          sum += weight;
        }
      }
    }
    double target = random.generateDouble() * sum;
    for (const auto &candidate : candidates) {
      target -= candidate.weight;
      if (target < 0) {
        c.subject = *candidate.subject;
        c.reading = candidate.reading;
        c.selectionReason =
            useRecent ? "Recent struggles" : "Persistent trouble";
        break;
      }
    }
  }
  if (c.mode == Mode::Typing && !c.reading)
    c.mode = Mode::Recall;
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
