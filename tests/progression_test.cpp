#include "progression.h"
#include <QtTest>
#include <algorithm>

namespace {
Subject subject(int id = 1) {
  Subject s;
  s.id = id;
  s.characters = "日";
  s.readings = {"ひ"};
  s.meanings = {"sun"};
  s.source = id < 0 ? "anki" : "wanikani";
  return s;
}
bool appendAttempt(Store &store, int id, bool reading,
                   std::optional<bool> correct, int day,
                   QJsonObject metadata = {}, QString instant = {}) {
  QSqlQuery q(store.db);
  const QDate date = QDate(2026, 1, 1).addDays(day);
  if (instant.isNull())
    instant = date.toString(Qt::ISODate) + "T12:00:00.000Z";
  q.prepare(
      "INSERT INTO "
      "attempts(subject_id,characters,mode,dimension,correct,active_ms,created_"
      "at,local_day,event_json) VALUES(?,?,'Recall',?,?,0,?,?,?)");
  q.addBindValue(id);
  q.addBindValue("日");
  q.addBindValue(reading ? "Reading" : "Meaning");
  q.addBindValue(correct ? QVariant(*correct ? 1 : 0) : QVariant());
  q.addBindValue(instant);
  q.addBindValue(date.toString(Qt::ISODate));
  q.addBindValue(QString::fromUtf8(
      QJsonDocument(metadata).toJson(QJsonDocument::Compact)));
  return q.exec();
}
int countNotices(const QVector<ProgressNotice> &notices,
                 ProgressNotice::Kind kind) {
  return std::count_if(
      notices.cbegin(), notices.cend(),
      [kind](const ProgressNotice &n) { return n.kind == kind; });
}
} // namespace

class ProgressionTest : public QObject {
  Q_OBJECT
private slots:
  void selfRatingNeedsBothSkillsAndElapsedTime() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(p.refresh({subject()}));
    for (int day : {0, 7, 14})
      QVERIFY(appendAttempt(store, 1, true, true, day));
    QVERIFY(p.refresh({subject()}, true));
    QVERIFY(!p.tile(1)->settled);
    QCOMPARE(p.tile(1)->readingSuccesses, 3);
    QCOMPARE(p.tile(1)->meaningSuccesses, 0);
    QCOMPARE(p.tile(1)->phase, TilePhase::Seen);
    QVERIFY(p.takeNotices().isEmpty());
    for (int day : {0, 7, 14})
      QVERIFY(appendAttempt(store, 1, false, true, day));
    QVERIFY(p.refresh({subject()}, true));
    QVERIFY(p.tile(1)->settled);
    QCOMPARE(p.tile(1)->phase, TilePhase::Settled);
    QCOMPARE(p.state().settled, 1);
    QCOMPARE(p.state().practiceDays, 3);
    QCOMPARE(countNotices(p.takeNotices(), ProgressNotice::Completed), 1);
  }
  void spanDoesNotReplaceSuccessfulGap() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (int day : {0, 3, 6, 9, 12, 15})
      for (bool reading : {false, true})
        QVERIFY(appendAttempt(store, 1, reading, true, day));
    QVERIFY(p.refresh({subject()}));
    QVERIFY(!p.tile(1)->settled);
    for (bool reading : {false, true})
      QVERIFY(appendAttempt(store, 1, reading, true, 22));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->settled);
  }
  void clusteredAnswersAndRetriesCannotAccelerate() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(p.refresh({subject()}));
    for (bool reading : {false, true}) {
      QVERIFY(appendAttempt(store, 1, reading, std::nullopt, 0));
      QVERIFY(appendAttempt(store, 1, reading, false, 0));
      for (int retry = 0; retry < 10; ++retry)
        QVERIFY(appendAttempt(store, 1, reading, true, 0));
      for (int day : {1, 2})
        QVERIFY(appendAttempt(store, 1, reading, true, day));
    }
    QVERIFY(p.refresh({}, true));
    QCOMPARE(p.tile(1)->readingSuccesses, 2);
    QCOMPARE(p.tile(1)->meaningSuccesses, 2);
    QVERIFY(!p.tile(1)->settled);
    QVERIFY(!p.tile(1)->challenged());
    QCOMPARE(p.state().practiceDays, 3);
  }
  void oneBadDayCannotMarkAndSkillsClearSeparately() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(p.refresh({subject()}));
    for (int retry = 0; retry < 12; ++retry)
      QVERIFY(appendAttempt(store, 1, true, false, 0));
    QVERIFY(p.refresh({}, true));
    QVERIFY(!p.tile(1)->challenged());
    for (int day : {1, 2})
      QVERIFY(appendAttempt(store, 1, true, false, day));
    for (int day : {0, 1, 2})
      QVERIFY(appendAttempt(store, 1, false, false, day));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->readingChallenge);
    QVERIFY(p.tile(1)->meaningChallenge);
    QVERIFY(p.takeNotices().isEmpty());
    for (int day : {3, 4, 5})
      QVERIFY(appendAttempt(store, 1, true, true, day));
    QVERIFY(p.refresh({}, true));
    QVERIFY(!p.tile(1)->readingChallenge);
    QVERIFY(p.tile(1)->meaningChallenge);
    QCOMPARE(countNotices(p.takeNotices(), ProgressNotice::ChallengeCleared),
             0);
    for (int day : {3, 4, 5})
      QVERIFY(appendAttempt(store, 1, false, true, day));
    QVERIFY(p.refresh({}, true));
    QVERIFY(!p.tile(1)->challenged());
    QCOMPARE(countNotices(p.takeNotices(), ProgressNotice::ChallengeCleared),
             1);
  }
  void clearedMarksNeedFreshMissesAndNotTheMarkingDay() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(p.refresh({subject()}));
    for (int day : {0, 1, 2})
      QVERIFY(appendAttempt(store, 1, true, false, day));
    QVERIFY(appendAttempt(store, 1, true, true, 2)); // Retry on marking day.
    for (int day : {3, 4})
      QVERIFY(appendAttempt(store, 1, true, true, day));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->readingChallenge);
    QVERIFY(appendAttempt(store, 1, true, true, 5));
    QVERIFY(p.refresh({}, true));
    QVERIFY(!p.tile(1)->readingChallenge);
    for (int day : {6, 7}) {
      QVERIFY(appendAttempt(store, 1, true, false, day));
      QVERIFY(p.refresh({}, true));
      QVERIFY(!p.tile(1)->readingChallenge);
    }
    QVERIFY(appendAttempt(store, 1, true, false, 8));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->readingChallenge);
  }
  void challengeUsesLastSixDaysAndClearingIsNotAStreak() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (int day = 0; day <= 6; ++day)
      QVERIFY(
          appendAttempt(store, 1, true, day != 0 && day != 3 && day != 6, day));
    QVERIFY(p.refresh({subject()}));
    QVERIFY(!p.tile(1)->readingChallenge); // The day-zero miss has aged out.
    QVERIFY(appendAttempt(store, 1, true, false, 7));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->readingChallenge);
    for (int day : {8, 10, 12}) {
      QVERIFY(appendAttempt(store, 1, true, true, day));
      if (day < 12)
        QVERIFY(appendAttempt(store, 1, true, false, day + 1));
    }
    QVERIFY(p.refresh({}, true));
    QVERIFY(!p.tile(1)->readingChallenge);
  }
  void batchedMarkAndClearProducesOneClearingNotice() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(p.refresh());
    for (int day = 0; day < 6; ++day)
      QVERIFY(appendAttempt(store, 1, true, day >= 3, day));
    QVERIFY(p.refresh({subject()}, true));
    QVERIFY(!p.tile(1)->readingChallenge);
    QCOMPARE(countNotices(p.takeNotices(), ProgressNotice::ChallengeCleared),
             1);
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.takeNotices().isEmpty());
  }
  void completionPersistsAndRestartDoesNotReplay() {
    QTemporaryDir dir;
    Store store(dir.path());
    for (int day : {0, 7, 14})
      for (bool reading : {false, true})
        QVERIFY(appendAttempt(store, 1, reading, true, day));
    {
      Progression p(store.db);
      QVERIFY(p.refresh({subject()}, true));
      QVERIFY(p.tile(1)->settled);
      QVERIFY(p.takeNotices().isEmpty()); // Quiet historical backfill.
    }
    Progression restarted(store.db);
    QVERIFY(restarted.refresh({}, true));
    QVERIFY(restarted.tile(1)->settled);
    QVERIFY(restarted.takeNotices().isEmpty());
    for (int day : {100, 101, 102})
      QVERIFY(appendAttempt(store, 1, true, false, day));
    QVERIFY(restarted.refresh({}, true));
    QVERIFY(restarted.tile(1)->settled);
    QVERIFY(restarted.tile(1)->readingChallenge);
    QCOMPARE(restarted.tile(1)->progress, 1.0);
    QVERIFY(restarted.takeNotices().isEmpty());
  }
  void metadataNeverInventsOppositeRecall() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (int day : {0, 7, 14})
      QVERIFY(appendAttempt(store, 1, false, true, day));
    QVERIFY(p.refresh());
    QVERIFY(!p.tile(1)->eligible);
    QCOMPARE(p.tile(1)->readingSuccesses, 0);
    QJsonObject metadata{{"accepted_readings", QJsonArray{"ひ"}},
                         {"accepted_meanings", QJsonArray{"sun"}}};
    QVERIFY(appendAttempt(store, 2, false, true, 0, metadata));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(2)->eligible);
    QCOMPARE(p.tile(2)->readingSuccesses, 0);
    QCOMPARE(p.tile(2)->phase, TilePhase::Seen);
    QVERIFY(!p.tile(2)->settled);
    QVERIFY(p.takeNotices().isEmpty());
    // Demonstrable prompts in each legacy dimension are eligible evidence.
    for (int day : {0, 7, 14})
      QVERIFY(appendAttempt(store, 1, true, true, day));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->eligible);
    QVERIFY(p.tile(1)->settled);
    // An explicitly empty modern reading list does not prove usable reading.
    metadata["accepted_readings"] = QJsonArray{};
    for (int day : {0, 7, 14})
      for (bool reading : {false, true})
        QVERIFY(appendAttempt(store, 3, reading, true, day, metadata));
    QVERIFY(p.refresh({}, true));
    QVERIFY(!p.tile(3)->eligible);
    QVERIFY(!p.tile(3)->settled);
  }
  void distinctSourceIdsAndDismissals() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(appendAttempt(store, 2, true, std::nullopt, 0));
    QVERIFY(appendAttempt(store, 1, true, true, 0));
    QVERIFY(appendAttempt(store, -1, false, true, 0));
    QVERIFY(p.refresh({subject(1), subject(-1), subject(2)}));
    QCOMPARE(p.state().total, 2);
    QCOMPARE(p.state().practiceDays, 1);
    QVERIFY(!p.tile(2));
    QCOMPARE(p.tile(1)->source, QString("wanikani"));
    QCOMPARE(p.tile(-1)->source, QString("anki"));
    QCOMPARE(p.tile(1)->readingSuccesses, 1);
    QCOMPARE(p.tile(-1)->meaningSuccesses, 1);
  }
  void subjectKindPersistsAndLivePoolBackfillsIt() {
    QTemporaryDir dir;
    Store store(dir.path());
    QJsonObject metadata{{"subject_kind", "vocabulary"}};
    QVERIFY(appendAttempt(store, 1, true, true, 0, metadata));
    QVERIFY(appendAttempt(store, 2, true, true, 0));
    {
      Progression p(store.db);
      QVERIFY(p.refresh());
      QCOMPARE(p.tile(1)->subjectKind, QString("vocabulary"));
    }
    {
      Progression restarted(store.db);
      QVERIFY(restarted.refresh());
      QCOMPARE(restarted.tile(1)->subjectKind, QString("vocabulary"));
      QVERIFY(restarted.tile(2)->subjectKind.isEmpty());
      auto legacy = subject(2);
      legacy.kind = "kanji";
      QVERIFY(restarted.refresh({legacy}));
      QCOMPARE(restarted.tile(2)->subjectKind, QString("kanji"));
      auto live = subject();
      live.kind = "kanji";
      QVERIFY(restarted.refresh({live}));
      QCOMPARE(restarted.tile(1)->subjectKind, QString("kanji"));
      live.kind = "vocabulary";
      QVERIFY(restarted.refresh({live}));
      QCOMPARE(restarted.tile(1)->subjectKind, QString("vocabulary"));
    }
    Progression persisted(store.db);
    QVERIFY(persisted.refresh());
    QCOMPARE(persisted.tile(1)->subjectKind, QString("vocabulary"));
    QCOMPARE(persisted.tile(2)->subjectKind, QString("kanji"));
  }
  void meaningfulDevelopmentAndOccasionalConsistency() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(p.refresh({subject()}));
    QVERIFY(appendAttempt(store, 1, false, true, 0));
    QVERIFY(p.refresh({subject()}, true));
    QVERIFY(p.takeNotices().isEmpty());
    QVERIFY(appendAttempt(store, 1, true, true, 0));
    QVERIFY(p.refresh({}, true));
    QCOMPARE(p.tile(1)->phase, TilePhase::Growing);
    QCOMPARE(countNotices(p.takeNotices(), ProgressNotice::Developed), 1);
    QVERIFY(appendAttempt(store, 1, true, true, 1));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.takeNotices().isEmpty());
    for (int day = 2; day <= 6; ++day)
      QVERIFY(appendAttempt(store, 1, true, true, day));
    QVERIFY(p.refresh({}, true));
    const auto notices = p.takeNotices();
    QCOMPARE(countNotices(notices, ProgressNotice::Consistency), 1);
    QCOMPARE(notices.last().practiceDays, 7);
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.takeNotices().isEmpty());
  }
  void unknownTimestampCannotCompleteOrContributeAGap() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (int day : {0, 7, 14})
      for (bool reading : {false, true})
        QVERIFY(appendAttempt(
            store, 1, reading, true, day, {},
            QDate(2026, 1, 1).addDays(day).toString(Qt::ISODate)));
    QVERIFY(p.refresh({subject()}));
    QCOMPARE(p.state().practiceDays, 3);
    QCOMPARE(p.tile(1)->readingSuccesses, 3);
    QVERIFY(!p.tile(1)->settled);
    for (int day : {21, 28, 35})
      for (bool reading : {false, true})
        QVERIFY(appendAttempt(store, 1, reading, true, day));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->settled);
  }
  void unknownSuccessBreaksTemporalAdjacency() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (int day : {0, 4, 8, 12, 16})
      for (bool reading : {false, true}) {
        const QString timestamp = day == 4 ? QString("2026-01-05") : QString();
        QVERIFY(appendAttempt(store, 1, reading, true, day, {}, timestamp));
      }
    QVERIFY(p.refresh({subject()}));
    QCOMPARE(p.tile(1)->readingSuccesses, 5);
    QCOMPARE(p.tile(1)->meaningSuccesses, 5);
    QVERIFY(!p.tile(1)->settled);
    // A later demonstrably adjacent week-long gap still qualifies.
    for (bool reading : {false, true})
      QVERIFY(appendAttempt(store, 1, reading, true, 23));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->settled);
  }
  void gradingDaySurvivesRevealedAnswerCrossingMidnight() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(appendAttempt(store, 1, true, false, 0));
    QJsonObject metadata{{"graded_at", "2026-01-01T23:59:00Z"},
                         {"graded_local_day", "2026-01-01"}};
    QVERIFY(appendAttempt(store, 1, true, true, 1, metadata,
                          "2026-01-02T00:05:00Z"));
    QSqlQuery q(store.db);
    QVERIFY(q.exec("UPDATE attempts SET mode='Typing' WHERE id=(SELECT MAX(id) "
                   "FROM attempts)"));
    QVERIFY(p.refresh({subject()}));
    QCOMPARE(p.state().practiceDays, 1);
    QCOMPARE(p.tile(1)->readingSuccesses, 0);
    metadata["graded_at"] = "2026-01-02T23:59:00Z";
    metadata["graded_local_day"] = "2026-01-02";
    QVERIFY(appendAttempt(store, 1, true, true, 2, metadata,
                          "2026-01-03T00:05:00Z"));
    QVERIFY(p.refresh({}, true));
    QCOMPARE(p.state().practiceDays, 2);
    QCOMPARE(p.tile(1)->readingSuccesses, 1);
  }
  void historicalGradingDaysUseRecordedZoneAndOffset() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (int id : {1, 2, 3, 4, 5})
      QVERIFY(appendAttempt(store, id, true, false, 0));
    QJsonObject metadata{{"graded_at", "2026-01-02T04:59:00Z"},
                         {"timezone", "America/New_York"},
                         {"utc_offset_seconds", 0}};
    QVERIFY(appendAttempt(store, 1, true, true, 1, metadata));
    metadata["timezone"] = "Unknown/Invalid";
    metadata["utc_offset_seconds"] = -18000;
    QVERIFY(appendAttempt(store, 2, true, true, 1, metadata));
    metadata.remove("utc_offset_seconds");
    QVERIFY(appendAttempt(store, 3, true, true, 1, metadata));
    metadata["graded_local_day"] = "2026-01-02";
    metadata["timezone"] = "America/New_York";
    QVERIFY(appendAttempt(store, 4, true, true, 1, metadata));
    metadata["graded_local_day"] = "not-a-day";
    QVERIFY(appendAttempt(store, 5, true, true, 1, metadata));
    QVERIFY(p.refresh(
        {subject(1), subject(2), subject(3), subject(4), subject(5)}));
    QCOMPARE(p.tile(1)->readingSuccesses, 0); // Named historical zone wins.
    QCOMPARE(p.tile(2)->readingSuccesses, 0); // Stored offset fallback.
    QCOMPARE(p.tile(3)->readingSuccesses, 1); // No invented local zone.
    QCOMPARE(p.tile(4)->readingSuccesses, 1); // Explicit valid day wins.
    QCOMPARE(p.tile(5)->readingSuccesses, 0); // Invalid day uses named zone.
    QCOMPARE(p.state().practiceDays, 2);
  }
  void temporalEvidencePrefersActualGradingInstant() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (int day : {0, 7, 14})
      for (bool reading : {false, true}) {
        const QJsonObject metadata{
            {"graded_at", "2026-01-01T12:00:00Z"},
            {"graded_local_day",
             QDate(2026, 1, 1).addDays(day).toString(Qt::ISODate)}};
        QVERIFY(appendAttempt(store, 1, reading, true, day, metadata));
      }
    QVERIFY(p.refresh({subject()}));
    QCOMPARE(p.state().practiceDays, 3);
    QCOMPARE(p.tile(1)->readingSuccesses, 3);
    QVERIFY(
        !p.tile(1)->settled); // Completion/save timestamps do not supply span.
  }
  void actualElapsedTimeMatters() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    for (bool reading : {false, true}) {
      QVERIFY(appendAttempt(store, 1, reading, true, 0, {},
                            "2026-01-01T23:00:00Z"));
      QVERIFY(appendAttempt(store, 1, reading, true, 7, {},
                            "2026-01-08T23:00:00Z"));
      QVERIFY(appendAttempt(store, 1, reading, true, 14, {},
                            "2026-01-15T22:59:59Z"));
    }
    QVERIFY(p.refresh({subject()}));
    QVERIFY(!p.tile(1)->settled);
    for (bool reading : {false, true})
      QVERIFY(appendAttempt(store, 1, reading, true, 15));
    QVERIFY(p.refresh({}, true));
    QVERIFY(p.tile(1)->settled);
  }
  void failedTransactionDoesNotAdvanceOrPublish() {
    QTemporaryDir dir;
    Store store(dir.path());
    Progression p(store.db);
    QVERIFY(p.refresh({subject()}));
    QSqlQuery q(store.db);
    QVERIFY(q.exec(
        "CREATE TRIGGER fail_progress BEFORE INSERT ON progression_tiles BEGIN "
        "SELECT RAISE(ABORT,'fixture failure'); END"));
    for (bool reading : {false, true})
      QVERIFY(appendAttempt(store, 1, reading, true, 0));
    QVERIFY(!p.refresh({subject()}, true));
    QVERIFY(p.error.contains("fixture failure"));
    QCOMPARE(p.state().total, 0);
    QCOMPARE(p.state().practiceDays, 0);
    QVERIFY(p.takeNotices().isEmpty());
    QVERIFY(q.exec("SELECT attempt_id FROM progression_checkpoint"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    QVERIFY(q.exec("SELECT COUNT(*) FROM progression_daily"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    QVERIFY(q.exec("DROP TRIGGER fail_progress"));
    QVERIFY(p.refresh({subject()}, true));
    QCOMPARE(p.state().total, 1);
    QCOMPARE(p.tile(1)->readingSuccesses, 1);
    QCOMPARE(countNotices(p.takeNotices(), ProgressNotice::Developed), 1);
  }
};
QTEST_GUILESS_MAIN(ProgressionTest)
#include "progression_test.moc"
