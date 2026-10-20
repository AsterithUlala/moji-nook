#include "wanikani_reviews.h"
#include <QtTest>
#include <cstring>

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
struct Response {
  int status = 200;
  QByteArray body;
  int delayMs = 0;
};
class FakeReply : public QNetworkReply {
public:
  FakeReply(const QNetworkRequest &request, const Response &response,
            QObject *parent)
      : QNetworkReply(parent), bytes(response.body) {
    setRequest(request);
    setUrl(request.url());
    if (response.status)
      setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
    else
      setError(QNetworkReply::ConnectionRefusedError, "refused");
    open(QIODevice::ReadOnly);
    QTimer::singleShot(response.delayMs, this, [this] {
      setFinished(true);
      emit readyRead();
      emit finished();
    });
  }
  void abort() override {}
  qint64 bytesAvailable() const override {
    return bytes.size() - position + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char *data, qint64 size) override {
    const auto n = qMin(size, qint64(bytes.size()) - position);
    if (n <= 0)
      return -1;
    std::memcpy(data, bytes.constData() + position, size_t(n));
    position += n;
    return n;
  }

private:
  QByteArray bytes;
  qint64 position = 0;
};
struct Sent {
  QNetworkAccessManager::Operation operation;
  QNetworkRequest request;
  QByteArray body;
  QString path() const { return request.url().path(); }
};
// Routes by method and path; anything unrouted fails like a server error.
class FakeNetwork : public QNetworkAccessManager {
public:
  QHash<QString, QList<Response>> routes;
  QList<Sent> sent;
  int open = 0, maxOpen = 0;
  QList<Sent> posts() const {
    QList<Sent> out;
    for (const auto &s : sent)
      if (s.operation == PostOperation)
        out.append(s);
    return out;
  }

protected:
  QNetworkReply *createRequest(Operation operation,
                               const QNetworkRequest &request,
                               QIODevice *data) override {
    sent.append({operation, request, data ? data->readAll() : QByteArray()});
    const QString key = (operation == PostOperation  ? "POST "
                         : operation == GetOperation ? "GET "
                                                     : "OTHER ") +
                        request.url().host() + request.url().path();
    auto &queue = routes[key];
    const auto response = queue.size() > 1   ? queue.takeFirst()
                          : queue.size() == 1 ? queue.first()
                                              : Response{500, {}};
    auto *reply = new FakeReply(request, response, this);
    if (operation == PostOperation) {
      maxOpen = qMax(maxOpen, ++open);
      connect(reply, &QNetworkReply::finished, this, [this] { --open; });
    }
    return reply;
  }
};
QByteArray json(const QJsonObject &o) {
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
Response page(const QJsonArray &rows, const QString &next = {}) {
  return {200, json({{"data", rows},
                     {"pages", QJsonObject{{"next_url", next.isEmpty()
                                                            ? QJsonValue()
                                                            : QJsonValue(next)}}}})};
}
QJsonObject assignment(int id, int subject, const QString &at, int stage = 4,
                       bool hidden = false) {
  return {{"id", id},
          {"object", "assignment"},
          {"data", QJsonObject{{"subject_id", subject},
                               {"available_at", at},
                               {"srs_stage", stage},
                               {"hidden", hidden}}}};
}
// GET /v2/assignments/{id}, which every submission checks first.
Response current(int id, int subject, const QString &at, bool hidden = false) {
  return {200, json({{"id", id},
                     {"object", "assignment"},
                     {"data", QJsonObject{{"subject_id", subject},
                                          {"available_at", at.isEmpty() ? QJsonValue() : QJsonValue(at)},
                                          {"hidden", hidden}}}})};
}
QJsonObject meaningEntry(const QString &m, bool primary = true) {
  return {{"meaning", m}, {"primary", primary}, {"accepted_answer", true}};
}
QJsonObject readingEntry(const QString &r, const QString &type,
                         bool accepted = true, bool primary = true) {
  return {{"reading", r},
          {"primary", primary},
          {"accepted_answer", accepted},
          {"type", type}};
}
QJsonObject kanjiHouse() {
  return {{"id", 100},
          {"object", "kanji"},
          {"data",
           QJsonObject{
               {"characters", "家"},
               {"level", 5},
               {"meanings", QJsonArray{meaningEntry("House"),
                                       meaningEntry("Home", false)}},
               {"auxiliary_meanings",
                QJsonArray{QJsonObject{{"type", "blacklist"},
                                       {"meaning", "Family"}}}},
               {"readings", QJsonArray{readingEntry("か", "onyomi"),
                                       readingEntry("け", "onyomi", true,
                                                    false),
                                       readingEntry("いえ", "kunyomi", false,
                                                    false)}}}}};
}
QJsonObject vocabSchool() {
  return {{"id", 200},
          {"object", "vocabulary"},
          {"data", QJsonObject{{"characters", "学校"},
                               {"level", 4},
                               {"meanings", QJsonArray{meaningEntry("School")}},
                               {"readings", QJsonArray{readingEntry(
                                                "がっこう", "")}}}}};
}
QJsonObject radical(int id, const QJsonValue &characters) {
  return {{"id", id},
          {"object", "radical"},
          {"data",
           QJsonObject{
               {"characters", characters},
               {"level", 1},
               {"meanings", QJsonArray{meaningEntry("Ground")}},
               {"character_images",
                QJsonArray{
                    QJsonObject{{"url", "https://files.wanikani.com/a.svg"},
                                {"content_type", "image/svg+xml"},
                                {"metadata", QJsonObject{}}},
                    QJsonObject{{"url", "https://files.wanikani.com/big.png"},
                                {"content_type", "image/png"},
                                {"metadata",
                                 QJsonObject{{"dimensions", "1024x1024"}}}},
                    QJsonObject{{"url", "https://files.wanikani.com/128.png"},
                                {"content_type", "image/png"},
                                {"metadata",
                                 QJsonObject{{"dimensions", "128x128"}}}}}}}}};
}
QJsonObject kanaVocab() {
  return {{"id", 400},
          {"object", "kana_vocabulary"},
          {"data", QJsonObject{{"characters", "ちょっと"},
                               {"level", 3},
                               {"meanings", QJsonArray{meaningEntry("A Little")}}}}};
}
QJsonObject synonym(int subject, const QString &m) {
  return {{"id", 9000 + subject},
          {"data", QJsonObject{{"subject_id", subject},
                               {"meaning_synonyms", QJsonArray{m}}}}};
}
const QString api = "api.wanikani.com";
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
  void articlesAreNotDropped() {
    const auto dog = subject("kanji", {"Dog"});
    QCOMPARE(meaning(dog, "dog"), ReviewVerdict::Correct);
    QCOMPARE(meaning(dog, "the dog"), ReviewVerdict::Incorrect);
    QCOMPARE(meaning(dog, "a dog"), ReviewVerdict::Incorrect);
    const auto go = subject("vocabulary", {"To Go"});
    QCOMPARE(meaning(go, "to go"), ReviewVerdict::Correct);
    QCOMPARE(meaning(go, "to goo"), ReviewVerdict::Correct);
    // A verb answered without "to" asks again instead of passing.
    const auto bare = checkReviewAnswer(go, false, "go");
    QCOMPARE(bare.verdict, ReviewVerdict::Retry);
    QCOMPARE(bare.hint, QString("This meaning is a verb. Start it with “to”."));
    QCOMPARE(meaning(go, "went"), ReviewVerdict::Incorrect);
    QCOMPARE(meaning(go, "do"), ReviewVerdict::Incorrect);
    QCOMPARE(checkReviewAnswer(subject("vocabulary", {"To Remember"}), false, "rememebr").verdict,
             ReviewVerdict::Retry);
    // Practice cards keep their lenient matching.
    QCOMPARE(normalizeMeaning("the dog"), QString("dog"));
  }
  void kanaInMeaningAsksAgain() {
    const auto s = subject("vocabulary", {"Person"}, {"ひと"});
    const auto check = checkReviewAnswer(s, false, "ひと");
    QCOMPARE(check.verdict, ReviewVerdict::Retry);
    QCOMPARE(check.hint, QString("WaniKani wants the meaning, not the reading."));
    QCOMPARE(meaning(s, "人"), ReviewVerdict::Retry);
  }
  void romajiReadingInMeaningAsksAgain() {
    const auto dog = subject("kanji", {"Dog"}, {"いぬ"});
    const auto check = checkReviewAnswer(dog, false, "inu");
    QCOMPARE(check.verdict, ReviewVerdict::Retry);
    QCOMPARE(check.hint, QString("WaniKani wants the meaning, not the reading."));
    QCOMPARE(meaning(dog, "inn"), ReviewVerdict::Incorrect);
    QCOMPARE(meaning(dog, "dog"), ReviewVerdict::Correct);
    auto kana = subject("kana_vocabulary", {"A Little"});
    kana.characters = "ちょっと";
    QCOMPARE(meaning(kana, "chotto"), ReviewVerdict::Retry);
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
  void nonKanaInReadingAsksAgain() {
    const auto s = subject("vocabulary", {"Feeling"}, {"かん"});
    for (const auto text : {"か ん", "かん。", "kan1", "かん!", "感"}) {
      const auto check = checkReviewAnswer(s, true, text);
      QVERIFY2(check.verdict == ReviewVerdict::Retry, text);
      QCOMPARE(check.hint, QString("Type the reading in kana."));
    }
    QCOMPARE(reading(s, "  かん "), ReviewVerdict::Correct);
    QCOMPARE(reading(s, "けん"), ReviewVerdict::Incorrect);
    QCOMPARE(reading(subject("vocabulary", {"Cake"}, {"ケーキ"}), "ke-ki"),
             ReviewVerdict::Correct);
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
  void refreshDueOnFiveMinutesOrNewHour() {
    const auto last = QDateTime(QDate(2026, 10, 19), QTime(9, 58), QTimeZone::UTC);
    QVERIFY(reviewRefreshDue({}, last));
    QVERIFY(!reviewRefreshDue(last, last.addSecs(60)));
    QVERIFY(reviewRefreshDue(last, last.addSecs(130)));
    QVERIFY(reviewRefreshDue(last, last.addSecs(300)));
    const auto early = QDateTime(QDate(2026, 10, 19), QTime(9, 1), QTimeZone::UTC);
    QVERIFY(!reviewRefreshDue(early, early.addSecs(240)));
  }
  void refreshParsesQueue() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(12, 200, "2026-10-19T10:00:00.000000Z"),
              assignment(13, 999, "2026-10-19T08:00:00.000000Z", 2, true)},
             "https://api.wanikani.com/v2/assignments?page_after_id=13"),
        page({assignment(11, 100, "2026-10-19T09:00:00.000000Z"),
              assignment(14, 300, "2026-10-19T11:00:00.000000Z", 1),
              assignment(15, 400, "2026-10-19T11:00:00.000000Z", 6)})};
    network.routes["GET " + api + "/v2/subjects"] = {
        page({kanjiHouse(), vocabSchool(), radical(300, "一"), kanaVocab()})};
    network.routes["GET " + api + "/v2/study_materials"] = {
        page({synonym(200, "Schoolhouse")})};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    QVERIFY(refreshed.last()[0].toBool());
    QCOMPARE(refreshed.last()[1].toString(), QString("4 reviews waiting"));
    QCOMPARE(reviews.waiting(), 4);
    QList<int> order;
    for (const auto &item : reviews.queue)
      order.append(item.assignmentId);
    QCOMPARE(order, (QList<int>{11, 12, 14, 15}));
    const auto &house = reviews.queue[0];
    QCOMPARE(house.subject.kind, QString("kanji"));
    QCOMPARE(house.subject.meanings, (QStringList{"House", "Home"}));
    QCOMPARE(house.subject.blacklist, QStringList{"Family"});
    QCOMPARE(house.subject.readings, (QStringList{"か", "け"}));
    QCOMPARE(house.subject.otherReadings, QStringList{"いえ"});
    QCOMPARE(house.subject.readingKind, QString("onyomi"));
    QCOMPARE(house.subject.stage, 4);
    QVERIFY(reviews.queue[1].subject.meanings.contains("Schoolhouse"));
    QVERIFY(!reviews.queue[2].needsReading());
    QVERIFY(!reviews.queue[3].needsReading());
    const auto subjects = network.sent[2].request.url();
    QCOMPARE(subjects.path(), QString("/v2/subjects"));
    QCOMPARE(QUrlQuery(subjects).queryItemValue("ids"), QString("200,100,300,400"));
    for (const auto &s : network.sent) {
      QCOMPARE(s.operation, QNetworkAccessManager::GetOperation);
      QCOMPARE(s.request.url().host(), api);
      QCOMPARE(s.request.rawHeader("Authorization"), QByteArray("Bearer test-token"));
      QCOMPARE(s.request.rawHeader("Wanikani-Revision"), QByteArray("20170710"));
      QVERIFY(!s.request.header(QNetworkRequest::UserAgentHeader)
                   .toString()
                   .contains("read-only"));
    }
    // Every question is typed and belongs to an unanswered half.
    for (int i = 0; i < 40; ++i) {
      const auto c = reviews.next();
      QVERIFY(c);
      QVERIFY(c->wanikaniReview);
      QCOMPARE(c->mode, Mode::Typing);
      QVERIFY(c->event["wanikani_review"].toBool());
      if (c->subject.kind == "radical" || c->subject.kind == "kana_vocabulary")
        QVERIFY(!c->reading);
    }
  }
  void paginationCannotLeaveTheApi() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 100, "2026-10-19T09:00:00Z")},
             "https://evil.example/v2/assignments?page_after_id=11")};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    QVERIFY(!refreshed.last()[0].toBool());
    for (const auto &s : network.sent)
      QCOMPARE(s.request.url().host(), api);
  }
  void accessProbe() {
    QTemporaryDir dir;
    FakeNetwork network;
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy checked(&reviews, &WaniKaniReviews::accessChecked);
    network.routes["POST " + api + "/v2/reviews"] = {{422, R"({"error":"param is missing"})"}};
    reviews.checkAccess("test-token");
    QTRY_COMPARE(checked.count(), 1);
    QVERIFY(checked.last()[0].toBool());
    QCOMPARE(network.sent.last().body, QByteArray(R"({"review":{}})"));
    network.routes["POST " + api + "/v2/reviews"] = {{401, {}}};
    reviews.checkAccess("test-token");
    QTRY_COMPARE(checked.count(), 2);
    QVERIFY(!checked.last()[0].toBool());
    QCOMPARE(checked.last()[1].toString(), reviewAccessDenied);
  }
  void completedItemIsSubmittedOnce() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 100, "2026-10-19T09:00:00.000000Z")}), page({})};
    network.routes["GET " + api + "/v2/subjects"] = {page({kanjiHouse()})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET " + api + "/v2/assignments/11"] = {current(11, 100, "2026-10-19T09:00:00.000000Z")};
    network.routes["POST " + api + "/v2/reviews"] = {
        {201, json({{"resources_updated",
                     QJsonObject{{"assignment",
                                  QJsonObject{{"data", QJsonObject{{"srs_stage", 5}}}}}}}})}};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    QSignalSpy submitted(&reviews, &WaniKaniReviews::submitted);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    auto ask = [&](bool reading) {
      for (int i = 0; i < 50; ++i) {
        auto c = reviews.next();
        if (c && c->reading == reading)
          return *c;
      }
      return Challenge{};
    };
    reviews.answer(ask(false), false);
    reviews.answer(ask(true), false);
    reviews.answer(ask(true), false);
    reviews.answer(ask(false), true);
    QVERIFY(network.posts().isEmpty());
    const auto last = ask(true);
    reviews.answer(last, true);
    reviews.answer(last, true);
    QTRY_COMPARE(submitted.count(), 1);
    QCOMPARE(submitted.last()[0].toString(), QString("家 · Apprentice 4 → Guru 1"));
    QCOMPARE(network.posts().size(), 1);
    QCOMPARE(network.posts()[0].body,
             QByteArray(R"({"review":{"assignment_id":11,"incorrect_meaning_answers":1,"incorrect_reading_answers":2}})"));
    QCOMPARE(network.posts()[0].request.rawHeader("Authorization"),
             QByteArray("Bearer test-token"));
    // Sent only right after WaniKani confirmed this review is still current.
    int post = 0;
    while (network.sent[post].operation != QNetworkAccessManager::PostOperation)
      ++post;
    QCOMPARE(network.sent[post - 1].path(), QString("/v2/assignments/11"));
    QCOMPARE(reviews.waiting(), 0);
    // An emptied queue refreshes to pick up newly unlocked reviews.
    QTRY_COMPARE(refreshed.count(), 2);
    QVERIFY(!reviews.next());
    for (const auto &s : network.sent) {
      QVERIFY(s.operation == QNetworkAccessManager::GetOperation ||
              s.operation == QNetworkAccessManager::PostOperation);
      QVERIFY(!s.path().contains("/start"));
    }
    QCOMPARE(network.posts().size(), 1);
  }
  void failedSubmissionKeepsItemAndRetries() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 300, "2026-10-19T09:00:00Z", 1)})};
    network.routes["GET " + api + "/v2/subjects"] = {page({radical(300, "一")})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET " + api + "/v2/assignments/11"] = {current(11, 300, "2026-10-19T09:00:00Z")};
    network.routes["POST " + api + "/v2/reviews"] = {{503, {}}, {0, {}}, {201, "{}"}};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    reviews.submitRetryMs = 200;
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    QSignalSpy submitted(&reviews, &WaniKaniReviews::submitted);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    reviews.answer(*reviews.next(), true);
    QTRY_COMPARE(network.posts().size(), 1);
    QTest::qWait(5);
    QCOMPARE(reviews.waiting(), 1);
    QVERIFY(reviews.queue[0].complete());
    QVERIFY(!reviews.next());
    QFile saved(dir.path() + "/wanikani_review_progress.json");
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(saved.readAll().contains("11@2026-10-19T09:00:00Z"));
    // A refresh while the retry waits must not send a parallel copy.
    reviews.refresh("test-token");
    QTRY_COMPARE(submitted.count(), 1);
    QCOMPARE(network.posts().size(), 3);
    QCOMPARE(network.maxOpen, 1);
    QCOMPARE(submitted.last()[0].toString(), QString("一 · sent to WaniKani"));
    // WaniKani may still list it briefly; a sent review is never asked again.
    QTRY_COMPARE(refreshed.count(), 3);
    QCOMPARE(reviews.waiting(), 0);
    for (const auto &post : network.posts())
      QCOMPARE(post.body, QByteArray(R"({"review":{"assignment_id":11,"incorrect_meaning_answers":0,"incorrect_reading_answers":0}})"));
  }
  void refreshDuringSubmissionDoesNotResend() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 300, "2026-10-19T09:00:00Z", 1)})};
    network.routes["GET " + api + "/v2/subjects"] = {page({radical(300, "一")})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET " + api + "/v2/assignments/11"] = {current(11, 300, "2026-10-19T09:00:00Z")};
    network.routes["POST " + api + "/v2/reviews"] = {{201, "{}", 300}};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    QSignalSpy submitted(&reviews, &WaniKaniReviews::submitted);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    reviews.answer(*reviews.next(), true);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 2);
    QCOMPARE(submitted.count(), 0);
    QTRY_COMPARE(submitted.count(), 1);
    QTest::qWait(50);
    QCOMPARE(network.posts().size(), 1);
  }
  void rejectedSubmissionDropsUnavailableItem() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 300, "2026-10-19T09:00:00Z", 1)}), page({})};
    network.routes["GET " + api + "/v2/subjects"] = {page({radical(300, "一")})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET " + api + "/v2/assignments/11"] = {current(11, 300, "2026-10-19T09:00:00Z")};
    network.routes["POST " + api + "/v2/reviews"] = {{422, "{}"}};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    reviews.answer(*reviews.next(), true);
    QTRY_COMPARE(refreshed.count(), 2);
    QCOMPARE(reviews.waiting(), 0);
    QCOMPARE(network.posts().size(), 1);
    QFile saved(dir.path() + "/wanikani_review_progress.json");
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(!saved.readAll().contains("11@"));
  }
  void rejectedSubmissionIsNotRetriedOnRefresh() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 300, "2026-10-19T09:00:00Z", 1)})};
    network.routes["GET " + api + "/v2/subjects"] = {page({radical(300, "一")})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET " + api + "/v2/assignments/11"] = {
        current(11, 300, "2026-10-19T09:00:00Z")};
    network.routes["POST " + api + "/v2/reviews"] = {{422, "{}"}};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    reviews.answer(*reviews.next(), true);
    QTRY_COMPARE(network.posts().size(), 1);
    // WaniKani keeps listing it; every scheduled refresh must not re-post.
    for (int i = 0; i < 3; ++i) {
      reviews.refresh("test-token");
      QTest::qWait(50);
    }
    QCOMPARE(network.posts().size(), 1);
    QCOMPARE(reviews.waiting(), 0);
    QVERIFY(!reviews.next());
  }
  void stopCancelsRateLimitedSubmission() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 300, "2026-10-19T09:00:00Z", 1)})};
    network.routes["GET " + api + "/v2/subjects"] = {page({radical(300, "一")})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET " + api + "/v2/assignments/11"] = {
        current(11, 300, "2026-10-19T09:00:00Z")};
    network.routes["POST " + api + "/v2/reviews"] = {{201, "{}"}};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    QSignalSpy checked(&reviews, &WaniKaniReviews::accessChecked);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    reviews.notBefore = QDateTime::currentSecsSinceEpoch() + 1;
    reviews.answer(*reviews.next(), true);
    reviews.stop();
    const auto before = network.sent.size();
    QTest::qWait(1500);
    QCOMPARE(network.sent.size(), before);
    QCOMPARE(checked.count(), 0);
    // Turned on again, the saved item goes out with the token.
    reviews.refresh("test-token");
    QTRY_COMPARE(network.posts().size(), 1);
    QCOMPARE(network.posts()[0].request.rawHeader("Authorization"),
             QByteArray("Bearer test-token"));
  }
  void staleReviewIsNotSubmitted() {
    // WaniKani moved on (reviewed elsewhere, now due again, or not due yet):
    // the answers given here belong to a review that no longer exists.
    struct Case {
      QString listed, current;
      bool hidden;
    };
    const QList<Case> cases = {
        {"2026-10-19T09:00:00.000000Z", "2026-10-19T21:00:00.000000Z", false},
        {"2026-10-19T09:00:00.000000Z", "", false},
        {"2026-10-19T09:00:00.000000Z", "2026-10-19T09:00:00.000000Z", true},
        {"2099-01-01T00:00:00.000000Z", "2099-01-01T00:00:00.000000Z", false}};
    for (const auto &c : cases) {
      QTemporaryDir dir;
      FakeNetwork network;
      network.routes["GET " + api + "/v2/assignments"] = {
          page({assignment(11, 300, c.listed, 1)}), page({})};
      network.routes["GET " + api + "/v2/subjects"] = {page({radical(300, "一")})};
      network.routes["GET " + api + "/v2/study_materials"] = {page({})};
      network.routes["GET " + api + "/v2/assignments/11"] = {
          current(11, 300, c.current, c.hidden)};
      network.routes["POST " + api + "/v2/reviews"] = {{201, "{}"}};
      WaniKaniReviews reviews(dir.path(), nullptr, &network);
      QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
      reviews.refresh("test-token");
      QTRY_COMPARE(refreshed.count(), 1);
      reviews.answer(*reviews.next(), false);
      reviews.answer(*reviews.next(), true);
      QTRY_COMPARE(reviews.waiting(), 0);
      QTest::qWait(20);
      QVERIFY2(network.posts().isEmpty(), qPrintable(c.current));
      QFile saved(dir.path() + "/wanikani_review_progress.json");
      QVERIFY(saved.open(QIODevice::ReadOnly));
      QVERIFY(!saved.readAll().contains("11@"));
    }
  }
  void progressSurvivesRestart() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 100, "2026-10-19T09:00:00Z")})};
    network.routes["GET " + api + "/v2/subjects"] = {page({kanjiHouse()})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET " + api + "/v2/assignments/11"] = {current(11, 100, "2026-10-19T09:00:00Z")};
    network.routes["POST " + api + "/v2/reviews"] = {{201, "{}"}};
    {
      WaniKaniReviews first(dir.path(), nullptr, &network);
      QSignalSpy refreshed(&first, &WaniKaniReviews::refreshed);
      first.refresh("test-token");
      QTRY_COMPARE(refreshed.count(), 1);
      Challenge c = *first.next();
      c.reading = false;
      first.answer(c, false);
      first.answer(c, true);
    }
    WaniKaniReviews second(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&second, &WaniKaniReviews::refreshed);
    second.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    QCOMPARE(second.queue[0].meaningMisses, 1);
    QVERIFY(second.queue[0].meaningDone);
    for (int i = 0; i < 20; ++i)
      QVERIFY(second.next()->reading);
    second.answer(*second.next(), true);
    QTRY_COMPARE(network.posts().size(), 1);
    QCOMPARE(network.posts()[0].body,
             QByteArray(R"({"review":{"assignment_id":11,"incorrect_meaning_answers":1,"incorrect_reading_answers":0}})"));
    // A different review instance of the same assignment starts fresh.
    QTemporaryDir other;
    QVERIFY(QFile::copy(dir.path() + "/wanikani_review_progress.json",
                        other.path() + "/wanikani_review_progress.json"));
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 100, "2026-10-20T09:00:00Z")})};
    WaniKaniReviews third(other.path(), nullptr, &network);
    QSignalSpy thirdRefreshed(&third, &WaniKaniReviews::refreshed);
    third.refresh("test-token");
    QTRY_COMPARE(thirdRefreshed.count(), 1);
    QCOMPARE(third.queue[0].meaningMisses, 0);
  }
  void answerAfterStopIsStillSaved() {
    QTemporaryDir dir;
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 100, "2026-10-19T09:00:00Z")})};
    network.routes["GET " + api + "/v2/subjects"] = {page({kanjiHouse()})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    {
      WaniKaniReviews first(dir.path(), nullptr, &network);
      QSignalSpy refreshed(&first, &WaniKaniReviews::refreshed);
      first.refresh("test-token");
      QTRY_COMPARE(refreshed.count(), 1);
      Challenge c = *first.next();
      // A 401 on submit, or turning the mode off, empties the queue while
      // the card is still open.
      first.stop();
      first.answer(c, false);
    }
    WaniKaniReviews second(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&second, &WaniKaniReviews::refreshed);
    second.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    QCOMPARE(second.queue[0].meaningMisses + second.queue[0].readingMisses, 1);
    QVERIFY(!second.queue[0].meaningDone && !second.queue[0].readingDone);
  }
  void missingSubjectKeepsSavedProgress() {
    QTemporaryDir dir;
    QFile seed(dir.path() + "/wanikani_review_progress.json");
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write(json({{"version", 1},
                     {"items", QJsonObject{{"11@2026-10-19T09:00:00Z",
                                            QJsonObject{{"assignment_id", 11},
                                                        {"available_at", "2026-10-19T09:00:00Z"},
                                                        {"meaning_misses", 2}}}}}}));
    seed.close();
    FakeNetwork network;
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 100, "2026-10-19T09:00:00Z")})};
    network.routes["GET " + api + "/v2/subjects"] = {page({})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    QCOMPARE(reviews.waiting(), 0);
    QVERIFY(seed.open(QIODevice::ReadOnly));
    QVERIFY(seed.readAll().contains("11@2026-10-19T09:00:00Z"));
  }
  void imageOnlyRadicalDownloadsWithoutToken() {
    QTemporaryDir dir;
    FakeNetwork network;
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::black);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    network.routes["GET " + api + "/v2/assignments"] = {
        page({assignment(11, 300, "2026-10-19T09:00:00Z", 1)})};
    network.routes["GET " + api + "/v2/subjects"] = {page({radical(300, QJsonValue())})};
    network.routes["GET " + api + "/v2/study_materials"] = {page({})};
    network.routes["GET files.wanikani.com/128.png"] = {{200, png}};
    QTemporaryDir svgDir;
    FakeNetwork svgNetwork;
    for (const auto &route : {"/v2/assignments", "/v2/subjects", "/v2/study_materials"})
      svgNetwork.routes["GET " + api + route] = network.routes["GET " + api + route];
    // The SVG's CSS variable must not leave the stroke unpainted.
    svgNetwork.routes["GET files.wanikani.com/a.svg"] = {
        {200, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><style>.b{fill:none;stroke:var(--color-text, #000);stroke-width:4px}</style><path d=\"M0 5h10\" class=\"b\"/></svg>"}};
    WaniKaniReviews fromSvg(svgDir.path(), nullptr, &svgNetwork);
    fromSvg.refresh("test-token");
    QTRY_VERIFY(fromSvg.next().has_value());
    QImage drawn(fromSvg.next()->subject.picture);
    QCOMPARE(drawn.size(), QSize(128, 128));
    QVERIFY(qAlpha(drawn.pixel(64, 64)) > 128);
    QStringList fileRequests;
    for (const auto &s : svgNetwork.sent)
      if (s.request.url().host() == "files.wanikani.com") {
        fileRequests.append(s.path());
        QVERIFY(s.request.rawHeader("Authorization").isEmpty());
      }
    QCOMPARE(fileRequests, QStringList{"/a.svg"});
    WaniKaniReviews reviews(dir.path(), nullptr, &network);
    QSignalSpy refreshed(&reviews, &WaniKaniReviews::refreshed);
    reviews.refresh("test-token");
    QTRY_COMPARE(refreshed.count(), 1);
    QTRY_VERIFY(reviews.next().has_value());
    const auto c = *reviews.next();
    QVERIFY(c.subject.characters.isEmpty());
    QCOMPARE(c.subject.picture, dir.path() + "/wanikani_radicals/300.png");
    QVERIFY(QFileInfo::exists(c.subject.picture));
    QStringList pngRequests;
    for (const auto &s : network.sent)
      if (s.request.url().host() == "files.wanikani.com") {
        pngRequests.append(s.path());
        QVERIFY(s.request.rawHeader("Authorization").isEmpty());
      }
    // An unusable SVG falls back to the PNG nearest 128 px.
    QCOMPARE(pngRequests, (QStringList{"/a.svg", "/128.png"}));
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
