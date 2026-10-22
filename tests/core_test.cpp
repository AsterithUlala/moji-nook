#include "core.h"
#include <QtTest>

class CoreTest : public QObject {
  Q_OBJECT
private slots:
  void rollingAccuracyAndReadableHistory() {
    QTemporaryDir dir;
    Store store(dir.path());
    Challenge c;
    c.subject.characters = "<同>";
    c.subject.id = 1;
    const auto now = QDateTime::currentDateTimeUtc();
    for (int age : {1, 48, 240}) {
      QVERIFY(store.record(c, age == 1, 100));
      QSqlQuery q(store.db);
      q.prepare("UPDATE attempts SET created_at=? WHERE id=(SELECT MAX(id) "
                "FROM attempts)");
      q.addBindValue(now.addSecs(-age * 3600).toString(Qt::ISODateWithMs));
      QVERIFY(q.exec());
    }
    const auto overview = store.statsHtml({});
    QVERIFY(overview.contains("100%"));
    QVERIFY(overview.contains("50%"));
    QVERIFY(!overview.contains("33%"));
    QVERIFY(store.statsHtml({}, true).contains("33%"));
    const auto history = store.historyHtml(0);
    QVERIFY(history.contains("&lt;同&gt;"));
    QVERIFY(history.contains("✓ Correct"));
    QVERIFY(history.contains("× Miss"));
    QVERIFY(!history.contains(now.addSecs(-3600).toString(Qt::ISODateWithMs)));
    QTemporaryDir emptyDir;
    Store empty(emptyDir.path());
    QVERIFY(empty.statsHtml({}).contains("—"));
  }
  void historyPagingAndSeparatedViews() {
    QTemporaryDir dir;
    Store store(dir.path());
    Challenge c;
    c.subject.stage = 1;
    c.subject.meanings = {"one"};
    for (int i = 0; i < 55; ++i) {
      c.subject.id = i + 1;
      c.subject.characters = QString("ITEM_%1_END").arg(i);
      QVERIFY(store.record(c, true, 100));
    }
    const auto first = store.historyHtml(0), second = store.historyHtml(1);
    QVERIFY(first.contains("ITEM_54_END"));
    QVERIFY(!first.contains("ITEM_0_END"));
    QVERIFY(second.contains("ITEM_0_END"));
    QVERIFY(!second.contains("ITEM_54_END"));
    QCOMPARE(store.historyHtml(-1), first);
    QVERIFY(store.historyHtml(2).contains("will appear here"));
  }
  void observationsAndRecovery() {
    QTemporaryDir dir;
    Store store(dir.path());
    QVERIFY(store.ready());
    auto row = [](int errors, int correct = 10) {
      return QJsonObject{
          {"data", QJsonObject{{"subject_id", 1},
                               {"reading_incorrect", errors},
                               {"meaning_incorrect", 0},
                               {"reading_correct", correct},
                               {"meaning_correct", 10},
                               {"reading_current_streak", 0},
                               {"meaning_current_streak", 10},
                               {"created_at", "2025-01-01T00:00:00Z"}}}};
    };
    const auto now = QDateTime::currentDateTimeUtc();
    QVERIFY(store.observeReviews({row(5)},
                                 now.addSecs(-120).toString(Qt::ISODate)));
    QVERIFY(store.evidence().value(skillKey(1, true)).persistent > 0);
    QCOMPARE(store.evidence().value(skillKey(1, true)).recent, 0.0);
    QVERIFY(
        store.observeReviews({row(7)}, now.addSecs(-60).toString(Qt::ISODate)));
    double boost = store.evidence().value(skillKey(1, true)).recent;
    QVERIFY(boost > 1);
    QCOMPARE(store.evidence().value(skillKey(1, false)).recent, 0.0);
    QVERIFY(
        store.observeReviews({row(7)}, now.addSecs(-60).toString(Qt::ISODate)));
    QSqlQuery q(store.db);
    QVERIFY(q.exec("SELECT COUNT(*) FROM wk_changes"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    Challenge c;
    c.subject.id = 1;
    c.reading = true;
    QVERIFY(store.record(c, true, 100));
    QVERIFY(store.evidence().value(skillKey(1, true)).recent < boost * 0.6);
    QVERIFY(store.observeReviews({row(0, 0)}, now.toString(Qt::ISODate)));
    QVERIFY(q.exec("SELECT reset,reading_errors FROM wk_changes ORDER BY id "
                   "DESC LIMIT 1"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    QVERIFY(q.value(1).isNull());
    QCOMPARE(store.evidence().value(skillKey(1, true)).recent, 0.0);
    QJsonObject bad{{"data", QJsonObject{{"subject_id", 2}}}};
    QVERIFY(!store.observeReviews({row(10), bad},
                                  now.addSecs(60).toString(Qt::ISODate)));
    QVERIFY(q.exec("SELECT COUNT(*) FROM wk_changes"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
  }
  void targetedMixAndDimensions() {
    QVector<Subject> pool;
    for (int i = 1; i <= 12; ++i) {
      Subject s;
      s.id = i;
      s.stage = 1;
      s.kind = "kanji";
      s.meanings = {QString::number(i)};
      s.readings = {QString("か%1").arg(i)};
      pool.append(s);
    }
    Evidence evidence;
    evidence[skillKey(1, true)] = {5, 0};
    evidence[skillKey(2, false)] = {0, 5};
    Picker picker(78);
    int recent = 0, persistent = 0, broad = 0;
    for (int i = 0; i < 6000; ++i) {
      auto c = picker.next(pool, {}, evidence);
      if (c.selectionReason == "Recent struggles") {
        ++recent;
        QCOMPARE(c.subject.id, 1);
        QVERIFY(c.reading);
      } else if (c.selectionReason == "Persistent trouble") {
        ++persistent;
        QCOMPARE(c.subject.id, 2);
        QVERIFY(!c.reading);
      } else
        ++broad;
    }
    QVERIFY(recent > 2200 && recent < 2600);
    // Meaning-only trouble cannot supply a typed-reading card; those slots fall
    // back.
    QVERIFY(persistent > 750 && persistent < 1050);
    QVERIFY(broad > 2500);
    for (int i = 0; i < 100; ++i) {
      auto c = picker.next(pool, {1}, evidence, 100, 0);
      QCOMPARE(c.selectionReason, QString("Broad recall"));
      QVERIFY(c.subject.id != 1);
    }
  }
  void eventMigrationAndSnapshots() {
    QTemporaryDir dir;
    {
      Store old(dir.path());
      QSqlQuery q(old.db);
      QVERIFY(q.exec("DROP INDEX attempts_graded"));
      QVERIFY(q.exec("ALTER TABLE attempts DROP COLUMN event_json"));
      QVERIFY(q.exec("INSERT INTO attempts(subject_id,created_at,local_day) "
                     "VALUES(99,'2025-01-01T00:00:00Z','2025-01-01')"));
    }
    Store store(dir.path());
    QVERIFY(store.ready());
    Challenge c;
    c.subject.id = 1;
    c.subject.level = 3;
    c.subject.stage = 5;
    c.subject.kind = "vocabulary";
    c.subject.readings = {"めだま"};
    c.reading = true;
    c.mode = Mode::Typing;
    c.event = {{"submitted_answer", "medama"}, {"session_position", 2}};
    QVERIFY(store.record(c, true, 1500));
    QSqlQuery q(store.db);
    QVERIFY(q.exec("SELECT event_json FROM attempts ORDER BY id"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).isNull());
    QVERIFY(q.next());
    auto e = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
    QCOMPARE(e["schema_version"].toInt(), 2);
    QCOMPARE(e["subject_level"].toInt(), 3);
    QCOMPARE(e["submitted_answer"].toString(), QString("medama"));
    QVERIFY(e.contains("utc_offset_seconds"));
    QVERIFY(e.contains("completed_at"));
    QCOMPARE(e["session_position"].toInt(), 2);
    c.subject.id = -7;
    c.subject.characters = "=SUM(A1)";
    c.event["graded_local_day"] = "2026-01-02";
    QVERIFY(store.record(c, false, 900));
    QVERIFY(q.exec("SELECT local_day FROM attempts ORDER BY id DESC LIMIT 1"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QString("2026-01-02"));
    QVERIFY(store.exportCsv(dir.path() + "/events.csv"));
    QFile f(dir.path() + "/events.csv");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const auto csv = f.readAll();
    QVERIFY(csv.contains("event_json"));
    QVERIFY(csv.contains("\n\"-7\",\"'=SUM(A1)\""));
  }
  void reviewedAtFromAssignments() {
    QJsonArray subjects, assignments;
    const QList<QJsonObject> data = {
        {{"srs_stage", 5}, {"available_at", "2026-03-08T11:00:00.000000Z"}},
        {{"srs_stage", 9},
         {"burned_at", "2025-06-01T09:30:00.000000Z"},
         {"available_at", QJsonValue()}},
        {{"srs_stage", 2}, {"available_at", QJsonValue()}},
        {{"srs_stage", 9}}};
    for (int i = 0; i < data.size(); ++i) {
      QJsonObject word{{"characters", "日"}, {"level", 1}};
      word["meanings"] = QJsonArray{QJsonObject{
          {"meaning", "Sun"}, {"accepted_answer", true}, {"primary", true}}};
      subjects.append(
          QJsonObject{{"id", i + 1}, {"object", "kanji"}, {"data", word}});
      auto assignment = data[i];
      assignment["subject_id"] = i + 1;
      assignment["started_at"] = "2025-01-01T00:00:00.000000Z";
      assignments.append(QJsonObject{{"data", assignment}});
    }
    const auto pool = parseSubjects(subjects, assignments, {});
    QCOMPARE(pool.size(), 4);
    auto at = [](const char *text) {
      return QDateTime::fromString(text, Qt::ISODateWithMs);
    };
    QCOMPARE(srsIntervalHours(5), 167);
    QCOMPARE(pool[0].reviewedAt, at("2026-03-01T12:00:00Z"));
    QCOMPARE(pool[1].reviewedAt, at("2025-06-01T09:30:00Z"));
    QCOMPARE(pool[2].reviewedAt, at("2025-01-01T00:00:00Z"));
    QCOMPARE(pool[3].reviewedAt, at("2025-01-01T00:00:00Z"));
  }
  void recallSeedsAndDecay() {
    const auto now = QDateTime::fromString("2026-03-01T12:00:00Z", Qt::ISODate);
    Subject guru;
    guru.id = 1;
    guru.stage = 5;
    guru.meanings = {"One"};
    guru.readings = {"いち"};
    guru.reviewedAt = now;
    Subject burned = guru;
    burned.id = 2;
    burned.stage = 9;
    Subject anki = guru;
    anki.id = -3;
    anki.stage = 4;
    anki.intervalHours = 167;
    Subject unknown = guru;
    unknown.id = 4;
    unknown.readings.clear();
    unknown.reviewedAt = {};
    const auto memory =
        estimateMemory({guru, burned, anki, unknown}, {}, now, now.date());
    QCOMPARE(memory.size(), 7);
    QVERIFY(!memory.contains(skillKey(4, true)));
    const auto g = memory.value(skillKey(1, false));
    QCOMPARE(g.lastSeen, now);
    // WaniKani's next Guru review, a week later, lands at about 90% recall.
    QVERIFY(qAbs(recallProbability(g, now.addSecs(167 * 3600)) - 0.9) < 1e-9);
    QCOMPARE(memory.value(skillKey(1, true)).halfLifeDays, g.halfLifeDays);
    QCOMPARE(memory.value(skillKey(2, false)).halfLifeDays, 5 * 365.0);
    QCOMPARE(memory.value(skillKey(-3, false)).halfLifeDays, g.halfLifeDays);
    QCOMPARE(recallProbability(memory.value(skillKey(4, false)), now), 0.5);
    QCOMPARE(recallProbability(SkillMemory{}, now), 0.5);
    const SkillMemory twoDays{2, now.addDays(-2)};
    QCOMPARE(recallProbability(twoDays, now), 0.5);
    QCOMPARE(recallProbability(twoDays, now.addDays(2)), 0.25);
  }
  void recallReplay() {
    const auto reviewed =
        QDateTime::fromString("2026-03-01T12:00:00Z", Qt::ISODate);
    Subject s;
    s.id = 1;
    s.stage = 5;
    s.meanings = {"One"};
    s.reviewedAt = reviewed;
    const double seed = estimateMemory({s}, {}, reviewed, reviewed.date())
                            .value(skillKey(1, false))
                            .halfLifeDays;
    auto attempt = [&](double days, bool correct, bool strong) {
      const auto at = reviewed.addMSecs(qint64(days * 86400000));
      return GradedAttempt{1, false, correct, strong, at, at.date()};
    };
    auto replay = [&](const QVector<GradedAttempt> &attempts) {
      const auto now = reviewed.addDays(400);
      return estimateMemory({s}, attempts, now, now.date())
          .value(skillKey(1, false));
    };
    // One half-life after the review, p = 0.5.
    const auto strong = replay({attempt(seed, true, true)});
    QVERIFY(qAbs(strong.halfLifeDays / seed - 2.0) < 1e-6);
    QVERIFY(qAbs(replay({attempt(seed, true, false)}).halfLifeDays / seed -
                 1.4) < 1e-6);
    QCOMPARE(strong.lastSeen, attempt(seed, true, true).at);
    QVERIFY(replay({attempt(1.0 / 1440, true, true)}).halfLifeDays / seed <
            1.001);
    QVERIFY(qAbs(replay({attempt(1, false, true)}).halfLifeDays / seed - 0.35) <
            1e-9);
    QVector<GradedAttempt> misses;
    for (int day = 1; day <= 10; ++day)
      misses.append(attempt(day, false, true));
    QCOMPARE(replay(misses).halfLifeDays, 10.0 / 1440);
    const auto early = replay({attempt(-1, false, true)});
    QCOMPARE(early.halfLifeDays, seed);
    QCOMPARE(early.lastSeen, reviewed);
    const auto chronological =
        replay({attempt(1, false, true), attempt(30, true, true)});
    const auto shuffled =
        replay({attempt(30, true, true), attempt(1, false, true)});
    QCOMPARE(shuffled.halfLifeDays, chronological.halfLifeDays);
    QCOMPARE(shuffled.lastSeen, attempt(30, true, true).at);
    const auto later = attempt(500, false, true);
    QCOMPARE(replay({later}).halfLifeDays, seed);
  }
  void recallRest() {
    const auto now = QDateTime(QDate(2026, 3, 1), QTime(12, 0));
    const QDate today = now.date();
    const SkillMemory fragile{1, now.addDays(-1)};
    QVERIFY(!rested(fragile, now, today));
    auto with = [&](auto change) {
      SkillMemory m = fragile;
      change(m);
      return rested(m, now, today);
    };
    QVERIFY(with([&](SkillMemory &m) { m.lastCorrectDay = today; }));
    QVERIFY(
        !with([&](SkillMemory &m) { m.lastCorrectDay = today.addDays(-1); }));
    QVERIFY(!with([](SkillMemory &m) { m.missesToday = 2; }));
    QVERIFY(with([](SkillMemory &m) { m.missesToday = 3; }));
    QVERIFY(with([&](SkillMemory &m) { m.lastMissAt = now.addSecs(-9 * 60); }));
    QVERIFY(
        !with([&](SkillMemory &m) { m.lastMissAt = now.addSecs(-11 * 60); }));
    QVERIFY(rested(SkillMemory{100, now.addDays(-1)}, now, today));
    QVERIFY(!rested(SkillMemory{100, now.addDays(-10)}, now, today));

    Subject s;
    s.id = 1;
    s.stage = 1;
    s.meanings = {"One"};
    s.reviewedAt = now.addDays(-10);
    auto at = [&](int hours) {
      const auto t = now.addSecs(hours * 3600);
      return GradedAttempt{1, false, false, true, t, t.date()};
    };
    auto missYesterday = at(-24), missMorning = at(-3), correctNoon = at(-1);
    correctNoon.correct = true;
    auto m = estimateMemory({s}, {missYesterday, missMorning}, now, today)
                 .value(skillKey(1, false));
    QCOMPARE(m.missesToday, 1);
    QCOMPARE(m.lastMissAt, missMorning.at);
    QVERIFY(!rested(m, now, today));
    m = estimateMemory({s}, {missYesterday, missMorning, correctNoon}, now,
                       today)
            .value(skillKey(1, false));
    QCOMPARE(m.lastCorrectDay, today);
    QVERIFY(rested(m, now, today));
    const auto tomorrow = now.addDays(1);
    QVERIFY(
        !rested(estimateMemory({s}, {missYesterday, missMorning, correctNoon},
                               tomorrow, tomorrow.date())
                    .value(skillKey(1, false)),
                tomorrow, tomorrow.date()));
  }
  void gradedAttemptStrength() {
    QTemporaryDir dir;
    Store store(dir.path());
    QVERIFY(store.ready());
    struct Row {
      QString mode;
      QVariant correct, event;
      bool strong;
    };
    auto json = [](const QJsonObject &o) {
      return QVariant(QString::fromUtf8(QJsonDocument(o).toJson()));
    };
    const QVector<Row> rows = {
        {"Typed reading", 1, json({{"grading", "checked"}}), true},
        {"Typed reading", 1,
         json({{"grading", "checked"}, {"overridden", true}}), false},
        {"Four choices", 0,
         json({{"grading", "checked"}, {"overridden", false}}), true},
        {"Recall", 1, json({{"grading", "self-rated"}}), false},
        {"Recall", 1,
         json({{"grading", "self-rated"}, {"recall_attempt_matched", true}}),
         true},
        {"Recall", 0,
         json({{"grading", "self-rated"}, {"recall_attempt_matched", false}}),
         false},
        {"Recall", 1, json({{"mode", "Recall"}}), false},
        {"Four choices", 0, json({{"mode", "Four choices"}}), true},
        {"Typed reading", 1, QVariant(QMetaType::fromType<QString>()), true},
        {"Recall", 1, QString("{broken"), false},
        {"Recall", QVariant(QMetaType::fromType<int>()),
         json({{"grading", "self-rated"}}), false},
    };
    const auto now = QDateTime::currentDateTimeUtc();
    QSqlQuery q(store.db);
    q.prepare("INSERT INTO attempts(subject_id,mode,dimension,correct,"
              "created_at,local_day,event_json) VALUES(?,?,?,?,?,?,?)");
    auto insert = [&](int id, const Row &row, const QDateTime &at) {
      q.addBindValue(id);
      q.addBindValue(row.mode);
      q.addBindValue(id % 2 ? "Reading" : "Meaning");
      q.addBindValue(row.correct);
      q.addBindValue(at.toString(Qt::ISODateWithMs));
      q.addBindValue(at.toLocalTime().date().toString(Qt::ISODate));
      q.addBindValue(row.event);
      return q.exec();
    };
    // Inserted newest first; results come back oldest first.
    for (int i = 0; i < rows.size(); ++i)
      QVERIFY(insert(i + 1, rows[i], now.addSecs(-i * 60)));
    QVERIFY(insert(99, rows[0], now.addDays(-31)));
    const auto graded = store.gradedAttempts();
    QCOMPARE(graded.size(), rows.size() - 1);
    for (const auto &a : graded) {
      const int i = a.subjectId - 1;
      QCOMPARE(a.strong, rows[i].strong);
      QCOMPARE(a.correct, rows[i].correct.toInt() == 1);
      QCOMPARE(a.reading, a.subjectId % 2 == 1);
      QCOMPARE(a.at.toString(Qt::ISODateWithMs),
               now.addSecs(-i * 60).toString(Qt::ISODateWithMs));
      QCOMPARE(a.localDay, now.addSecs(-i * 60).toLocalTime().date());
    }
    QCOMPARE(graded.first().subjectId, rows.size() - 1);
    Challenge c;
    c.subject.id = 50;
    c.mode = Mode::Typing;
    c.reading = true;
    c.event["overridden"] = true;
    QVERIFY(store.record(c, true, 100));
    c.event.remove("overridden");
    QVERIFY(store.record(c, true, 100));
    const auto recorded = store.gradedAttempts();
    QCOMPARE(recorded.size(), graded.size() + 2);
    QVERIFY(!recorded[recorded.size() - 2].strong);
    QVERIFY(recorded.last().strong);
  }
  void kana() {
    QCOMPARE(toKana("gakkou"), QString("がっこう"));
    QCOMPARE(
        toKana("shimbun"),
        QString("しmぶん")); // Do not silently reinterpret an invalid syllable.
    QCOMPARE(toKana("shinbun"), QString("しんぶん"));
    QCOMPARE(toKana("konnichiha"), QString("こんにちは"));
    QCOMPARE(toKana("kan'i"), QString("かんい"));
    QCOMPARE(toKana("カンジ"), QString("かんじ"));
    QCOMPARE(toKana("ｶﾝｼﾞ"), QString("かんじ"));
    QCOMPARE(toKana("matcha"), QString("まっちゃ"));
    QCOMPARE(toKana("nya"), QString("にゃ"));
    QCOMPARE(toKana("nna"), QString("んな"));
    QCOMPARE(toKana("shin'you"), QString("しんよう"));
  }
  void excludesUnstartedHiddenAndRestricted() {
    QJsonArray subjects, assignments;
    for (int id = 1; id <= 5; ++id) {
      QJsonObject data{
          {"characters", "日"},
          {"level", id == 4 ? 61 : 1},
          {"hidden_at", id == 3 ? QJsonValue("2026-01-01") : QJsonValue()}};
      data["meanings"] = QJsonArray{QJsonObject{
          {"meaning", "Sun"}, {"accepted_answer", true}, {"primary", true}}};
      data["readings"] = QJsonArray{QJsonObject{{"reading", "にち"},
                                                {"type", "onyomi"},
                                                {"primary", true},
                                                {"accepted_answer", true}},
                                    QJsonObject{{"reading", "ひ"},
                                                {"type", "kunyomi"},
                                                {"primary", false},
                                                {"accepted_answer", false}}};
      subjects.append(QJsonObject{{"id", id},
                                  {"object", id == 5 ? "radical" : "kanji"},
                                  {"data", data}});
      assignments.append(QJsonObject{
          {"data",
           QJsonObject{{"subject_id", id},
                       {"srs_stage", id == 2 ? 0 : 1},
                       {"started_at",
                        id == 2 ? QJsonValue() : QJsonValue("2026-01-01")}}}});
    }
    auto p = parseSubjects(subjects, assignments, {});
    QCOMPARE(p.size(), 1);
    QCOMPARE(p[0].id, 1);
    QCOMPARE(p[0].readings, QStringList{"にち"});
    QCOMPARE(p[0].kanjiReadingTypes.value("ひ"), QString("kunyomi"));
    QCOMPARE(p[0].startedAt.date(), QDate(2026, 1, 1));
  }
  void weightsAndCooldown() {
    QCOMPARE(stageWeight(0), 0.0);
    QCOMPARE(stageWeight(1), 4.0);
    QCOMPARE(stageWeight(9), 0.5);
    Subject a;
    a.id = 1;
    a.stage = 1;
    a.meanings = {"One"};
    a.characters = "一";
    a.kind = "kanji";
    Subject b = a;
    b.id = 2;
    b.stage = 9;
    b.characters = "二";
    b.meanings = {"Two"};
    Picker picker(17);
    int recent = 0;
    for (int i = 0; i < 9000; ++i)
      if (picker.next({a, b}, {}).subject.id == 1)
        ++recent;
    QVERIFY(recent > 7700 && recent < 8300);
    for (int i = 0; i < 30; ++i)
      QCOMPARE(picker.next({a, b}, {1}).subject.id, 2);
    QCOMPARE(picker.next({a}, {1}).subject.id, 1);
    int burned = 0;
    for (int i = 0; i < 2000; ++i) {
      auto c = picker.next({a, b}, {}, {}, 0, 0, 100, 0, {.1, 3, 2, 1, 10});
      QCOMPARE(c.mode, Mode::Recall);
      burned += c.subject.id == 2;
    }
    QVERIFY(burned > 1950);
    a.readings = {"いち"};
    for (int i = 0; i < 30; ++i)
      QCOMPARE(picker.next({a}, {}, {}, 0, 0, 0, 100).mode, Mode::Typing);
  }
  void newestLearnedSelection() {
    QVector<Subject> pool;
    Evidence evidence;
    const auto start =
        QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
    for (int i = 0; i < 40; ++i) {
      Subject s;
      s.id = i + 1;
      s.stage = 1;
      s.meanings = {"Sun"};
      s.readings = {"にち"};
      s.startedAt = start.addSecs(i);
      pool.append(s);
      evidence[skillKey(s.id, true)] = {1, 1};
    }
    Picker picker(79);
    QSet<int> selected;
    for (int i = 0; i < 1000; ++i) {
      auto c = picker.next(pool, {40}, {}, 0, 0, 0, 100, {}, 100);
      QCOMPARE(c.selectionReason, QString("Newest 30"));
      QCOMPARE(c.mode, Mode::Typing);
      QVERIFY(c.subject.id >= 11 && c.subject.id < 40);
      selected.insert(c.subject.id);
    }
    QCOMPARE(selected.size(), 29);
    QHash<QString, int> counts;
    for (int i = 0; i < 10000; ++i)
      ++counts[picker.next(pool, {}, evidence, 25, 20, 50, 25, {}, 30)
                   .selectionReason];
    QVERIFY(qAbs(counts["Newest 30"] - 3000) < 200);
    QVERIFY(qAbs(counts["Recent struggles"] - 2500) < 200);
    QVERIFY(qAbs(counts["Persistent trouble"] - 2000) < 200);
    QVERIFY(qAbs(counts["Broad recall"] - 2500) < 200);
    auto s = pool.first();
    QCOMPARE(
        picker.next({s}, {}, {}, 0, 0, 100, 0, {}, 100).selectionReason,
        QString("Newest 30"));
    s.startedAt = {};
    QCOMPARE(
        picker.next({s}, {}, {}, 0, 0, 100, 0, {}, 100).selectionReason,
        QString("Broad recall"));
  }
  void choiceAmbiguityAndModeMix() {
    QVector<Subject> pool;
    QStringList readings = {"かん", "さん", "たん", "なん",
                            "はん", "ばん", "ぱん", "まん"};
    for (int i = 0; i < readings.size(); ++i) {
      Subject s;
      s.id = i + 1;
      s.stage = 1;
      s.kind = "kanji";
      s.characters = QString::number(i);
      s.meanings = {QString("Meaning %1").arg(i)};
      s.readings = {readings[i]};
      s.components = {1};
      pool.append(s);
    }
    // A different item with the same accepted reading must never be an
    // incorrect choice.
    pool[1].readings = pool[0].readings;
    Picker picker(44);
    int modes[3] = {};
    for (int i = 0; i < 4000; ++i) {
      auto c = picker.next(pool, {});
      ++modes[int(c.mode)];
      if (c.mode == Mode::Choice) {
        QCOMPARE(c.choices.size(), 4);
        int valid = 0;
        for (const auto &x : c.choices)
          if (c.accepts(x))
            ++valid;
        QCOMPARE(valid, 1);
      }
    }
    QVERIFY(modes[0] > 1800 && modes[0] < 2200);
    QVERIFY(modes[1] > 850 && modes[1] < 1150);
    QVERIFY(modes[2] > 850 && modes[2] < 1150);
  }
  void answersAndPersistence() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Challenge c;
    c.subject.id = 1;
    c.subject.stage = 1;
    c.subject.characters = "日本";
    c.subject.meanings = {"Japan"};
    c.subject.readings = {"にほん", "にっぽん"};
    c.reading = true;
    QVERIFY(c.accepts("nihon"));
    QVERIFY(c.accepts("ニッポン"));
    QVERIFY(!c.accepts("にほ"));
    QVERIFY(!c.accepts(""));
    {
      Store store(dir.path());
      QVERIFY(store.ready());
      QVERIFY(store.record(c, std::nullopt, 0));
      QVERIFY(store.record(c, false, 100));
      QVERIFY(store.record(c, true, 100));
      QVERIFY(store.exportCsv(dir.path() + "/export.csv"));
      auto html = store.statsHtml({c.subject});
      QVERIFY(html.contains("Last 24 hours"));
      QVERIFY(html.contains("Last 7 days"));
      QVERIFY(html.contains("1 / 2"));
      QVERIFY(html.contains("50%"));
      QVERIFY(store.saveCache(QJsonObject{{"test", true}}));
    }
    Store reopened(dir.path());
    QVERIFY(reopened.cache()["test"].toBool());
    QCOMPARE(reopened.recentSubjects().size(), 3);
  }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
