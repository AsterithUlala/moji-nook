#include "sync.h"
#include <QtTest>
#include <cstring>

struct Response {
  int status = 200;
  QByteArray body;
  QList<QPair<QByteArray, QByteArray>> headers;
};
class FakeReply : public QNetworkReply {
public:
  FakeReply(const QNetworkRequest &request, const Response &response,
            QObject *parent)
      : QNetworkReply(parent), bytes(response.body) {
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
    for (const auto &header : response.headers)
      setRawHeader(header.first, header.second);
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this] {
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
class FakeNetwork : public QNetworkAccessManager {
public:
  QList<Response> responses;
  QList<QNetworkRequest> requests;

protected:
  QNetworkReply *createRequest(Operation operation,
                               const QNetworkRequest &request,
                               QIODevice *) override {
    Q_ASSERT(operation == GetOperation);
    requests.append(request);
    return new FakeReply(request,
                         responses.isEmpty() ? Response{500, {}, {}}
                                             : responses.takeFirst(),
                         this);
  }
};
class SyncTest : public QObject {
  Q_OBJECT
  static QJsonObject row(int id, const QJsonObject &data = {}) {
    return {{"id", id}, {"data", data}};
  }
  static Response collection(QJsonArray rows = {},
                             QJsonValue next = QJsonValue(),
                             QString stamp = "2026-09-01T12:00:00.123456Z") {
    return {
        200,
        QJsonDocument(QJsonObject{{"data", rows},
                                  {"pages", QJsonObject{{"next_url", next}}},
                                  {"data_updated_at", stamp}})
            .toJson(),
        {}};
  }
  static Response user(QString id = "account") {
    return {200,
            QJsonDocument(
                QJsonObject{
                    {"data",
                     QJsonObject{{"id", id},
                                 {"subscription",
                                  QJsonObject{{"max_level_granted", 60}}}}}})
                .toJson(),
            {{"ETag", "\"user-v1\""}}};
  }
  static void age(Store &store) {
    auto cache = store.cache();
    QVERIFY(cache["personal_full_at"].isString());
    cache["synced_at"] =
        QDateTime::currentDateTimeUtc().addSecs(-120).toString(Qt::ISODate);
    QVERIFY(store.saveCache(cache));
  }
private slots:
  void baselineThenDeltaAndCooldown() {
    QTemporaryDir dir;
    Store store(dir.path());
    FakeNetwork network;
    Sync sync(store, nullptr, &network);
    sync.requestDelay = 0;
    QSignalSpy done(&sync, &Sync::finished);
    network.responses = {
        user(),
        collection({row(1, {{"srs_stage", 1}, {"started_at", "2026-01-01"}})}),
        collection(),
        collection({row(2)},
                   "https://api.wanikani.com/v2/subjects?page_after_id=2"),
        collection({row(3)}, QJsonValue(), "2026-09-02T12:00:00Z"),
        collection()};
    sync.start("test-token");
    sync.start("test-token");
    QTRY_COMPARE(done.count(), 1);
    QVERIFY(done.last().first().toBool());
    QCOMPARE(network.requests.size(), 6);
    auto cache = store.cache();
    QCOMPARE(cache["subjects"].toArray().size(), 2);
    QCOMPARE(cache["sync_cursors"].toObject()["subjects"].toString(),
             QString("2026-09-01T11:59:59.123Z"));
    QVERIFY(!QUrlQuery(network.requests[1].url()).hasQueryItem("started"));
    QVERIFY(!QUrlQuery(network.requests[1].url()).hasQueryItem("hidden"));
    sync.start("test-token");
    QCOMPARE(done.count(), 2);
    QCOMPARE(network.requests.size(), 6);
    age(store);
    network.responses = {
        {304, {}, {}},
        collection({row(1, {{"srs_stage", 0}, {"started_at", QJsonValue()}})}),
        collection({}, QJsonValue(), QString()),
        collection({row(2, {{"hidden_at", "2026-09-02"}})}),
        collection()};
    sync.start("test-token");
    QTRY_COMPARE(done.count(), 3);
    QVERIFY(done.last().first().toBool());
    QCOMPARE(network.requests.size(), 11);
    QCOMPARE(network.requests[6].rawHeader("If-None-Match"),
             QByteArray("\"user-v1\""));
    for (int i = 7; i < 11; ++i)
      QVERIFY2(
          QUrlQuery(network.requests[i].url()).hasQueryItem("updated_after"),
          qPrintable(network.requests[i].url().toString()));
    cache = store.cache();
    QCOMPARE(cache["assignments"].toArray().size(), 1);
    QCOMPARE(cache["assignments"]
                 .toArray()[0]
                 .toObject()["data"]
                 .toObject()["srs_stage"]
                 .toInt(),
             0);
    QCOMPARE(cache["subjects"].toArray().size(), 2);
    QVERIFY(!cache["subjects"]
                 .toArray()[0]
                 .toObject()["data"]
                 .toObject()["hidden_at"]
                 .toString()
                 .isEmpty());
    QCOMPARE(cache["last_sync_requests"].toInt(), 5);
    QCOMPARE(cache["sync_cursors"].toObject()["study_materials"].toString(),
             QString("2026-09-01T11:59:59.123Z"));
    // A failed later page must not commit any partial delta or cursor.
    age(store);
    const auto before = store.cache();
    network.responses = {
        {304, {}, {}},
        collection({row(9)},
                   "https://api.wanikani.com/v2/assignments?page_after_id=9"),
        {200, "bad json", {}}};
    sync.start("test-token");
    QTRY_COMPARE(done.count(), 4);
    QVERIFY(!done.last().first().toBool());
    QCOMPARE(store.cache(), before);
    // Different tokens do not reuse the conditional account response.
    network.responses = {user("someone-else")};
    sync.start("changed-token");
    QTRY_COMPARE(done.count(), 5);
    QVERIFY(!done.last().first().toBool());
    QVERIFY(network.requests.last().rawHeader("If-None-Match").isEmpty());
    QCOMPARE(store.cache(), before);
  }
  void reconciliationAndUnsafePagination() {
    QTemporaryDir dir;
    Store store(dir.path());
    FakeNetwork network;
    Sync sync(store, nullptr, &network);
    sync.requestDelay = 0;
    QSignalSpy done(&sync, &Sync::finished);
    network.responses = {user(), collection({row(1)}), collection({row(2)}),
                         collection({row(3)}), collection()};
    sync.start("test");
    QTRY_COMPARE(done.count(), 1);
    QVERIFY(done.last().first().toBool());
    age(store);
    auto old = store.cache();
    old["personal_full_at"] = "2020-01-01T00:00:00Z";
    QVERIFY(store.saveCache(old));
    network.responses = {
        {304, {}, {}}, collection(), collection(), collection(), collection()};
    sync.start("test");
    QTRY_COMPARE(done.count(), 2);
    QVERIFY(done.last().first().toBool());
    QVERIFY(store.cache()["assignments"].toArray().isEmpty());
    QVERIFY(store.cache()["study_materials"].toArray().isEmpty());
    QCOMPARE(store.cache()["subjects"].toArray().size(), 1);
    QVERIFY(
        !QUrlQuery(network.requests[6].url()).hasQueryItem("updated_after"));
    QVERIFY(QUrlQuery(network.requests[8].url()).hasQueryItem("updated_after"));
    age(store);
    const auto before = store.cache();
    network.responses = {{304, {}, {}},
                         collection({}, "https://example.com/v2/assignments")};
    sync.start("test");
    QTRY_COMPARE(done.count(), 3);
    QVERIFY(!done.last().first().toBool());
    QCOMPARE(store.cache(), before);
    QCOMPARE(network.requests.last().url().host(), QString("api.wanikani.com"));
  }
  void rateLimitWaits() {
    QCOMPARE(syncRetrySeconds("600", "0", 100), qint64(600));
    QCOMPARE(syncRetrySeconds("2", "1000", 100), qint64(901));
    const auto now = QDateTime::fromString("2026-09-13T00:00:00Z", Qt::ISODate);
    QCOMPARE(
        syncRetrySeconds(now.addSecs(300).toString(Qt::RFC2822Date).toLatin1(),
                         {}, now.toSecsSinceEpoch()),
        qint64(300));
    QTemporaryDir dir;
    Store store(dir.path());
    FakeNetwork network;
    Sync sync(store, nullptr, &network);
    sync.requestDelay = 0;
    network.responses = {{429, {}, {{"Retry-After", "600"}}}};
    sync.start("test");
    QTRY_VERIFY(sync.notBefore >= QDateTime::currentSecsSinceEpoch() + 598);
    QCOMPARE(network.requests.size(), 1);
    QVERIFY(store.cache().isEmpty());
  }
  void exhaustedBudgetPausesBeforeNextRequest() {
    QTemporaryDir dir;
    Store store(dir.path());
    FakeNetwork network;
    Sync sync(store, nullptr, &network);
    sync.requestDelay = 0;
    auto response = user();
    response.headers.append(QPair<QByteArray, QByteArray>{"RateLimit-Remaining", "0"});
    response.headers.append(
        {"RateLimit-Reset",
         QByteArray::number(QDateTime::currentSecsSinceEpoch() + 180)});
    network.responses = {response, collection()};
    sync.start("test");
    QTRY_VERIFY(sync.notBefore > QDateTime::currentSecsSinceEpoch() + 175);
    QCOMPARE(network.requests.size(), 1);
    QVERIFY(sync.busy());
  }
};
QTEST_GUILESS_MAIN(SyncTest)
#include "sync_test.moc"
