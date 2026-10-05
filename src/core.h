#pragma once
#include <QtCore>
#include <QtSql>
#include <optional>

struct Subject {
  int id = 0, stage = 0, level = 0;
  QString kind, characters, url, contextJa, contextEn, readingKind;
  QStringList meanings, readings;
  QList<int> components, similar;
  QDateTime startedAt;
  QHash<QString, QString> kanjiReadingTypes;
  // Positive IDs are WaniKani; negative IDs are durable local source mappings.
  QString source = "wanikani", sourceId, deck;
  QJsonObject sourceData;
  QString wordAudio, sentenceAudio, picture, notes;
  QString meaning() const { return meanings.value(0); }
  QString reading() const { return readings.value(0); }
};

QString normalizeMeaning(QString text);
QString toKana(QString text);
double stageWeight(int stage);
QString stageName(int stage);
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
};

struct SkillEvidence {
  double recent = 0, persistent = 0;
};
using Evidence = QHash<QString, SkillEvidence>;
inline QString skillKey(int id, bool reading) {
  return QString::number(id) + (reading ? ":reading" : ":meaning");
}

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
  QHash<int, double> missBoosts() const;
  QList<int> recentSubjects() const;
  QString statsHtml(const QVector<Subject> &pool, bool detailed = false) const;
  bool exportCsv(const QString &path) const;
  bool observeReviews(const QJsonArray &rows, const QString &at);
  Evidence evidence() const;
  QString insightsHtml(const QVector<Subject> &pool) const;
  QString historyHtml(int page = 0) const;
};

class Picker {
public:
  explicit Picker(quint32 seed = QRandomGenerator::global()->generate())
      : random(seed) {}
  Challenge next(const QVector<Subject> &pool, const QHash<int, double> &boosts,
                 const QList<int> &recent, const Evidence &evidence = {},
                 int recentShare = 40, int persistentShare = 20,
                 int recallShare = 50, int typedShare = 25,
                 const QVector<double> &masteryWeights = {},
                 int newestShare = 0);

private:
  QRandomGenerator random;
  QStringList distractors(const Subject &subject, bool reading,
                          const QVector<Subject> &pool);
};
