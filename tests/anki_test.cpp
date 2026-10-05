#include "anki.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

// Deliberately synthetic, above the 32-bit range to exercise Anki ID preservation.
static QJsonObject sample(qint64 id = 5000000000001LL) {
  auto f = [](QString text) { return QJsonObject{{"value", text}}; };
  return {
      {"cardId", id},
      {"note", id},
      {"ord", 0},
      {"deckName", "Kaishi 1.5k"},
      {"modelName", "Kaishi 1.5k"},
      {"queue", 2},
      {"type", 2},
      {"reps", 10},
      {"interval", 25},
      {"lapses", 2},
      {"fields", QJsonObject{{"Word", f("<b>猫</b>")},
                             {"Word Reading", f("ねこ")},
                             {"Word Meaning", f("cat")},
                             {"Sentence", f("猫です。")},
                             {"Sentence Meaning", f("It is a cat.")},
                             {"Notes", f("<script>bad()</script>A &amp; B")}}}};
}
class FakeAnki : public QObject {
public:
  QTcpServer server;
  QStringList actions;
  QString query, profile = "Test profile", failure;
  QJsonArray cards = {sample()};
  int maxBatch = 0;
  FakeAnki() {
    if (!server.listen(QHostAddress::LocalHost))
      qFatal("Cannot bind test server");
    connect(&server, &QTcpServer::newConnection, this, [this] {
      auto *socket = server.nextPendingConnection();
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        QByteArray input =
            socket->property("input").toByteArray() + socket->readAll();
        socket->setProperty("input", input);
        int split = input.indexOf("\r\n\r\n");
        if (split < 0)
          return;
        auto length =
            QRegularExpression("Content-Length: (\\d+)",
                               QRegularExpression::CaseInsensitiveOption)
                .match(QString::fromUtf8(input.left(split)))
                .captured(1)
                .toInt();
        if (input.size() - split - 4 < length ||
            socket->property("answered").toBool())
          return;
        socket->setProperty("answered", true);
        auto request =
            QJsonDocument::fromJson(input.mid(split + 4, length)).object();
        const auto action = request["action"].toString();
        actions << action;
        auto params = request["params"].toObject();
        QJsonValue result;
        if (action == "getActiveProfile")
          result = profile;
        else if (action == "deckNames")
          result = QJsonArray{"Kaishi 1.5k", "Unsupported"};
        else if (action == "getMediaDirPath")
          result = "/missing/media";
        else if (action == "findCards") {
          query = params["query"].toString();
          QJsonArray ids;
          for (const auto &c : cards)
            ids.append(c.toObject()["cardId"]);
          result = ids;
        } else if (action == "cardsInfo") {
          const auto ids = params["cards"].toArray();
          maxBatch = qMax(maxBatch, int(ids.size()));
          QJsonArray batch;
          for (const auto &id : ids)
            for (const auto &c : cards)
              if (id == c.toObject()["cardId"])
                batch.append(c);
          result = batch;
        } else if (action == "getReviewsOfCards") {
          QJsonObject histories;
          for (const auto &id : params["cards"].toArray())
            histories[QString::number(id.toInteger())] = QJsonArray{QJsonObject{
                {"id", QDateTime::currentMSecsSinceEpoch() - 3600000},
                {"ease", 1},
                {"type", 1}}};
          result = histories;
        }
        const auto body =
            QJsonDocument(
                QJsonObject{
                    {"result", result},
                    {"error", action == failure
                                  ? QJsonValue("Deliberate test failure")
                                  : QJsonValue()}})
                .toJson();
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: "
                      "application/json\r\nContent-Length: " +
                      QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
    });
  }
  QUrl url() const {
    return QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
  }
};
class AnkiTest : public QObject {
  Q_OBJECT
private slots:
  void fieldAdapterAndHistory() {
    auto c = sample();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QJsonArray reviews{
        QJsonObject{{"id", now - 1000}, {"ease", 1}, {"type", 1}},
        QJsonObject{{"id", now - 2000}, {"ease", 0}, {"type", 4}}};
    auto s = kaishiSubject(c, reviews, -42, "Profile", {});
    QVERIFY(s);
    QCOMPARE(s->id, -42);
    QCOMPARE(s->sourceId, "5000000000001");
    QCOMPARE(s->characters, "猫");
    QCOMPARE(s->notes, "A & B");
    QCOMPARE(s->sourceData["review_count"].toInt(), 1);
    QVERIFY(s->sourceData["recent_trouble"].toDouble() > .9);
    QCOMPARE(s->startedAt.toMSecsSinceEpoch(), now - 1000);
    auto pitchCard = c;
    auto pitchFields = c["fields"].toObject();
    pitchFields["Pitch Accent"] = QJsonObject{
        {"value",
         "ネ<span style=\"display:inline-block;position:relative;\"><span "
         "style=\"display:inline;\">コ</span><span "
         "style=\"border-top-style:solid;\"></span></span>"}};
    pitchCard["fields"] = pitchFields;
    auto pitch = kaishiSubject(pitchCard, reviews, -1, "Profile", {})
                     ->sourceData["pitch"]
                     .toObject();
    QCOMPARE(pitch["text"].toString(), "ネコ");
    QCOMPARE(pitch["high"].toArray(), QJsonArray({false, true}));
    Challenge ch;
    ch.subject = *s;
    ch.reading = true;
    QVERIFY(ch.accepts("neko"));
    QVERIFY(!ch.accepts("inu"));
    auto alternate = c;
    auto fields = alternate["fields"].toObject();
    fields["Word Reading"] = QJsonObject{{"value", "なに・なん"}};
    alternate["fields"] = fields;
    auto both = kaishiSubject(alternate, reviews, -1, "Profile", {});
    QVERIFY(both);
    QCOMPARE(both->readings, QStringList({"なに", "なん"}));
    for (int queue : {-1, -2, -3}) {
      auto bad = c;
      bad["queue"] = queue;
      QVERIFY(!kaishiSubject(bad, reviews, -1, "Profile", {}));
    }
    c["type"] = 0;
    QVERIFY(!kaishiSubject(c, reviews, -1, "Profile", {}));
    c = sample();
    c["ord"] = 1;
    QVERIFY(!kaishiSubject(c, reviews, -1, "Profile", {}));
    c = sample();
    c["fields"] = QJsonObject{{"Front", "猫"}, {"Back", "cat"}};
    QVERIFY(!kaishiSubject(c, reviews, -1, "Profile", {}));
    QTemporaryDir dir;
    Store store(dir.path());
    QVERIFY(store.record(ch, true, 100));
    QSqlQuery q(store.db);
    QVERIFY(q.exec("SELECT subject_id,event_json FROM attempts"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), -42);
    const auto e = QJsonDocument::fromJson(q.value(1).toByteArray()).object();
    QCOMPARE(e["source"].toString(), "anki");
    QCOMPARE(e["source_id"].toString(), s->sourceId);
    const auto history = store.historyHtml();
    QVERIFY(history.contains("Source</th>"));
    QVERIFY(history.contains("Anki<br><small>Kaishi 1.5k</small></td>"));
  }
  void safeMediaAndText() {
    QCOMPARE(ankiPlainText("<img "
                           "src='https://example.invalid/x'><b>猫</"
                           "b><br>cat<script>doEvil()</script>"),
             "猫\ncat");
    QVERIFY(ankiMediaPath("/tmp", "../secret").isEmpty());
    QVERIFY(ankiMediaPath("/tmp", "https://example.com/a.mp3").isEmpty());
    QTemporaryDir dir;
    QFile file(dir.filePath("test.mp3"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("test");
    file.close();
    QCOMPARE(ankiMediaPath(dir.path(), "test.mp3"),
             QFileInfo(file).canonicalFilePath());
    QFile playlist(dir.filePath("test.m3u"));
    QVERIFY(playlist.open(QIODevice::WriteOnly));
    playlist.close();
    QVERIFY(ankiMediaPath(dir.path(), "test.m3u").isEmpty());
  }
  void onlyReadActions() {
    FakeAnki server;
    AnkiClient client(nullptr, server.url());
    bool called = false;
    client.request("answerCards", {}, [&](QJsonValue, QString error) {
      called = true;
      QVERIFY(!error.isEmpty());
    });
    QVERIFY(called);
    QVERIFY(server.actions.isEmpty());
    AnkiClient remote(nullptr, QUrl("https://example.invalid"));
    called = false;
    remote.request("deckNames", {}, [&](QJsonValue, QString error) {
      called = true;
      QVERIFY(!error.isEmpty());
    });
    QVERIFY(called);
  }
  void importIdentityFailureAndOptOut() {
    FakeAnki server;
    QTemporaryDir dir;
    Store store(dir.path());
    AnkiSource source(store, nullptr, server.url());
    QSignalSpy done(&source, &AnkiSource::finished);
    source.importDecks("Test profile", {"Kaishi 1.5k"});
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(done.last()[0].toBool());
    auto pool = source.subjects("Test profile", {"Kaishi 1.5k"});
    QCOMPARE(pool.size(), 1);
    QVERIFY(pool.first().id < 0);
    const int id = pool.first().id;
    QVERIFY(source.evidence(pool)[skillKey(id, true)].recent > 0);
    QVERIFY(source.subjects("Test profile", {}).isEmpty());
    QVERIFY(source.subjects("Different", {"Kaishi 1.5k"}).isEmpty());
    QVERIFY(server.query.contains("-is:new -is:suspended -is:buried"));
    source.importDecks("Test profile", {"Kaishi 1.5k"});
    QTRY_COMPARE(done.size(), 2);
    QCOMPARE(source.subjects("Test profile", {"Kaishi 1.5k"}).first().id, id);
    server.failure = "getReviewsOfCards";
    source.importDecks("Test profile", {"Kaishi 1.5k"});
    QTRY_COMPARE(done.size(), 3);
    QVERIFY(!done.last()[0].toBool());
    QCOMPARE(source.subjects("Test profile", {"Kaishi 1.5k"}).size(), 1);
    server.failure.clear();
    server.profile = "Changed";
    source.importDecks("Test profile", {"Kaishi 1.5k"});
    QTRY_COMPARE(done.size(), 4);
    QVERIFY(!done.last()[0].toBool());
    QCOMPARE(source.subjects("Test profile", {"Kaishi 1.5k"}).size(), 1);
    server.profile = "Test profile";
    server.cards = {};
    source.importDecks("Test profile", {"Kaishi 1.5k"});
    QTRY_COMPARE(done.size(), 5);
    QVERIFY(done.last()[0].toBool());
    QVERIFY(source.subjects("Test profile", {"Kaishi 1.5k"}).isEmpty());
    server.cards = {sample()};
    source.importDecks("Test profile", {"Kaishi 1.5k"});
    QTRY_COMPARE(done.size(), 6);
    QCOMPARE(source.subjects("Test profile", {"Kaishi 1.5k"}).first().id, id);
    QSqlQuery q(store.db);
    QVERIFY(q.exec("SELECT COUNT(*) FROM attempts"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
  }
  void boundedBatchesAndUnsupported() {
    FakeAnki server;
    server.cards = {};
    for (int i = 0; i < 70; ++i)
      server.cards.append(sample(5000000000001LL + i));
    auto unsupported = sample(5000000001000LL);
    unsupported["fields"] = QJsonObject{};
    server.cards.append(unsupported);
    QTemporaryDir dir;
    Store store(dir.path());
    AnkiSource source(store, nullptr, server.url());
    QSignalSpy done(&source, &AnkiSource::finished);
    source.importDecks("Test profile", {"Kaishi 1.5k"});
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(done.last()[0].toBool());
    QCOMPARE(source.subjects("Test profile", {"Kaishi 1.5k"}).size(), 70);
    QCOMPARE(source.metadata()["skipped"].toInt(), 1);
    QCOMPARE(server.maxBatch, 32);
  }
};
QTEST_MAIN(AnkiTest)
#include "anki_test.moc"
