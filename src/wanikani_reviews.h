#pragma once
#include "core.h"
#include <QtNetwork>
#include <functional>
#include <optional>

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

// Parses one /v2/subjects resource, radicals included.
Subject parseReviewSubject(const QJsonObject &resource,
                           const QStringList &synonyms = {});
// Reviews unlock on the hour, so a new hour is due even inside five minutes.
bool reviewRefreshDue(const QDateTime &lastAttempt, const QDateTime &now);
extern const QString reviewAccessDenied;

class WaniKaniReviews : public QObject {
  Q_OBJECT
public:
  explicit WaniKaniReviews(const QString &dataDir, QObject *parent = nullptr,
                           QNetworkAccessManager *transport = nullptr);
  void checkAccess(const QString &token);
  void refresh(const QString &token);
  void stop();
  std::optional<Challenge> next();
  void answer(const Challenge &challenge, bool correct);
  int waiting() const { return int(queue.size()); }
  QString waitingText() const;
  bool refreshing() const { return loading; }
  QDateTime lastAttempt() const { return attemptedAt; }
signals:
  void accessChecked(bool canSubmit, QString message);
  void refreshed(bool ok, QString message);
  void submitted(QString message);

private:
  friend class WaniKaniReviewsTest;
  struct Reply {
    int status = 0;
    bool networkError = false;
    QByteArray body, retryAfter, reset;
  };
  using Handler = std::function<void(const Reply &)>;
  QString directory, token;
  QNetworkAccessManager network;
  QNetworkAccessManager *transport;
  QVector<ReviewItem> queue;
  QJsonObject progress;
  // Sent, rejected, or found stale before sending; never asked or sent again.
  QSet<QString> settled;
  QString submitting;
  QTimer retryTimer;
  QRandomGenerator random{QRandomGenerator::global()->generate()};
  QDateTime attemptedAt;
  bool loading = false;
  int epoch = 0, probeEpoch = 0, session = 0, submitFailures = 0;
  int submitRetryMs = 30000, getRetryMs = 3000;
  qint64 notBefore = 0;
  void send(const QUrl &url, std::optional<QByteArray> body, Handler done);
  void collect(QList<QUrl> pages, QJsonArray rows, int attempt,
               std::function<void(QJsonArray)> done);
  void reload();
  void build(const QJsonArray &assignments, const QJsonArray &subjects,
             const QJsonArray &materials);
  void fetchPicture(int subjectId, QList<QUrl> urls);
  void saveProgress();
  void keepProgress(const ReviewItem &item);
  void submitNext();
  void post(const ReviewItem &item);
  void retryLater(const Reply &r);
  void settle(const QString &key);
  void failRefresh(const QString &message);
};
