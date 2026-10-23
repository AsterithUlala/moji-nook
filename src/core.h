#pragma once
#include <QtCore>
#include <QtSql>
#include <optional>

struct RelatedKanji {
  QString characters, meaning, reading;
};

struct Subject {
  int id = 0, stage = 0, level = 0;
  QString kind, characters, url, contextJa, contextEn, readingKind;
  QStringList meanings, readings;
  QList<int> components, similar;
  QDateTime startedAt, reviewedAt;
  int intervalHours = 0; // Source-reported review interval; 0 uses the stage.
  QHash<QString, QString> kanjiReadingTypes;
  // Positive IDs are WaniKani; negative IDs are durable local source mappings.
  QString source = "wanikani", sourceId, deck;
  QJsonObject sourceData;
  QString wordAudio, sentenceAudio, picture, notes;
  QString meaningMnemonic, readingMnemonic;
  QList<RelatedKanji> similarKanji;
  QString meaning() const { return meanings.value(0); }
  QString reading() const { return readings.value(0); }
};

QString normalizeMeaning(QString text);
QString toKana(QString text);
QString mnemonicHtml(const QString &text);
int editDistance(const QString &a, const QString &b);
double stageWeight(int stage);
QString stageName(int stage);
int srsIntervalHours(int stage);
QVector<Subject> parseSubjects(const QJsonArray &subjects,
                               const QJsonArray &assignments,
                               const QJsonArray &materials, int maxLevel = 60);

enum class Mode { Recall, Typing, Choice };
QString modeName(Mode mode);
struct Challenge {
  Subject subject;
  Mode mode = Mode::Recall;
  bool reading = false;
  QStringList choices;
  QString selectionReason = "Broad recall";
  QJsonObject event;
  QString answer() const {
    return reading ? subject.readings.join(" / ")
                   : subject.meanings.join(" / ");
  }
  bool accepts(const QString &text) const;
  QString readingRetryHint(const QString &text) const;
  bool nearMiss(const QString &text) const;
};

struct SkillEvidence {
  double recent = 0, persistent = 0;
};
using Evidence = QHash<QString, SkillEvidence>;
inline QString skillKey(int id, bool reading) {
  return QString::number(id) + (reading ? ":reading" : ":meaning");
}

struct GradedAttempt {
  int subjectId = 0;
  bool reading = false, correct = false, strong = false;
  QDateTime at;
  QDate localDay;
};
struct SkillMemory {
  double halfLifeDays = 0;
  QDateTime lastSeen;
  QDate lastCorrectDay;
  int missesToday = 0;
  QDateTime lastMissAt;
  // The last graded answer was a miss, so the memory is not as fresh as
  // lastSeen suggests.
  bool lapsed = false;
};
using Memory = QHash<QString, SkillMemory>;
Memory estimateMemory(const QVector<Subject> &pool,
                      const QVector<GradedAttempt> &attempts,
                      const QDateTime &now, const QDate &today);
double recallProbability(const SkillMemory &memory, const QDateTime &now);
bool rested(const SkillMemory &memory, const QDateTime &now,
            const QDate &today);

class Store {
public:
  explicit Store(const QString &directory);
  ~Store();
  bool ready() const { return error.isEmpty(); }
  QString error, directory;
  QSqlDatabase db;
  QJsonObject cache() const;
  bool saveCache(const QJsonObject &value);
  bool record(const Challenge &challenge, std::optional<bool> correct,
              qint64 activeMs);
  QList<int> recentSubjects() const;
  QString statsHtml(const QVector<Subject> &pool, bool detailed = false) const;
  bool exportCsv(const QString &path) const;
  bool observeReviews(const QJsonArray &rows, const QString &at);
  Evidence evidence() const;
  // Graded attempts recorded after `after` (all of them when invalid).
  QVector<GradedAttempt> gradedAttempts(const QDateTime &after = {}) const;
  QString insightsHtml(const QVector<Subject> &pool) const;
  QString historyHtml(int page = 0) const;
};

struct SelectionMix {
  int newest = 30, recent = 25, persistent = 20, burned = 10; // Broad: rest.
  int recall = 50, typed = 25;                                // Choice: rest.
  QVector<double> stageWeights;
};

class Picker {
public:
  explicit Picker(quint32 seed = QRandomGenerator::global()->generate())
      : random(seed) {}
  Challenge next(const QVector<Subject> &pool, const QList<int> &recent,
                 const Evidence &evidence, const Memory &memory,
                 const SelectionMix &mix, const QDateTime &now);

private:
  QRandomGenerator random;
  QStringList distractors(const Subject &subject, bool reading,
                          const QVector<Subject> &pool);
};
