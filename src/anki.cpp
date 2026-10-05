#include "anki.h"
#include <QTextDocumentFragment>
#include <cmath>

AnkiClient::AnkiClient(QObject *parent, QUrl url)
    : QObject(parent), network(this), endpoint(url) {
  network.setProxy(QNetworkProxy::NoProxy);
}
void AnkiClient::request(const QString &action, const QJsonObject &params,
                         Callback done) {
  static const QSet<QString> allowed = {"getActiveProfile",  "deckNames",
                                        "findCards",         "cardsInfo",
                                        "getReviewsOfCards", "getMediaDirPath"};
  if (!allowed.contains(action) || endpoint.scheme() != "http" ||
      (endpoint.host() != "127.0.0.1" && endpoint.host() != "localhost")) {
    done({}, "Only read-only, local AnkiConnect requests are allowed.");
    return;
  }
  QNetworkRequest request(endpoint);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(15000);
  auto *reply =
      network.post(request, QJsonDocument(QJsonObject{{"action", action},
                                                      {"version", 6},
                                                      {"params", params}})
                                .toJson());
  // Cap even malformed/custom-template responses; never download media here.
  connect(reply, &QNetworkReply::readyRead, reply, [reply] {
    if (reply->bytesAvailable() > 16 * 1024 * 1024)
      reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [reply, done] {
    const auto bytes = reply->readAll();
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    QString error;
    if (reply->error() != QNetworkReply::NoError)
      error = "Open Anki with AnkiConnect enabled, then try again. Cached "
              "practice is unchanged.";
    else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                     .toInt() != 200 ||
             parse.error != QJsonParseError::NoError || !doc.isObject() ||
             !doc.object().contains("result") ||
             !doc.object().contains("error"))
      error = "AnkiConnect returned an invalid response. Cache unchanged.";
    else if (!doc.object()["error"].isNull())
      error = "AnkiConnect: " + doc.object()["error"].toString();
    reply->deleteLater();
    done(doc.object()["result"], error);
  });
}

QString ankiPlainText(QString html) {
  static const QRegularExpression script(
      "<(script|style)\\b[^>]*>.*?</\\1>",
      QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression sound("\\[sound:[^\\]]*\\]");
  html.remove(script);
  html.remove(sound);
  auto text = QTextDocumentFragment::fromHtml(html).toPlainText();
  text.remove(QChar::ObjectReplacementCharacter);
  return text.trimmed();
}
QString ankiMediaPath(const QString &directory, const QString &filename) {
  if (directory.isEmpty() || filename.isEmpty() || filename.contains('/') ||
      filename.contains('\\') || filename == "." || filename == "..")
    return {};
  const QString root = QFileInfo(directory).canonicalFilePath();
  const QFileInfo file(QDir(directory).filePath(filename));
  static const QSet<QString> extensions = {"mp3", "ogg",  "opus", "wav",
                                           "m4a", "flac", "aac",  "png",
                                           "jpg", "jpeg", "webp", "gif"};
  if (!extensions.contains(file.suffix().toLower()))
    return {};
  const QString path = file.canonicalFilePath();
  if (root.isEmpty() || !file.isFile() || file.size() > 16 * 1024 * 1024 ||
      !path.startsWith(root + QDir::separator()))
    return {};
  return path;
}
static QString field(const QJsonObject &c, const QString &name) {
  return c["fields"].toObject()[name].toObject()["value"].toString();
}
static QString audioName(const QString &value) {
  return QRegularExpression("\\[sound:([^\\]]+)\\]").match(value).captured(1);
}
static QJsonObject kaishiPitch(const QString &html) {
  const QString text = ankiPlainText(html);
  static const QRegularExpression kana("^[\\x{30a1}-\\x{30fa}ー]+$");
  if (text.isEmpty() || text.size() > 24 || !kana.match(text).hasMatch())
    return {};
  // Recognize only Kaishi's known pitch spans; never render arbitrary HTML/CSS.
  static const QRegularExpression region(
      "<span[^>]*style=\"[^\"]*display:inline-block[^\"]*\"[^>]*><span[^>]*>([^"
      "<]+)</span><span[^>]*style=\"([^\"]*)\"[^>]*></span></span>");
  QJsonArray high;
  for (int i = 0; i < text.size(); ++i)
    high.append(false);
  auto matches = region.globalMatch(html);
  bool found = false, finalDrop = false;
  while (matches.hasNext()) {
    const auto match = matches.next();
    if (!match.captured(2).contains("border-top-style:solid"))
      continue;
    const int start = ankiPlainText(html.left(match.capturedStart())).size();
    const int end = start + match.captured(1).size();
    if (end > text.size())
      return {};
    for (int i = start; i < end; ++i)
      high[i] = true;
    finalDrop = end == text.size() &&
                match.captured(2).contains("border-right-style:solid");
    found = true;
  }
  return found ? QJsonObject{{"text", text},
                             {"high", high},
                             {"final_drop", finalDrop}}
               : QJsonObject{};
}
std::optional<Subject> kaishiSubject(const QJsonObject &c,
                                     const QJsonArray &reviews, int localId,
                                     const QString &profile,
                                     const QString &mediaDirectory) {
  const auto fields = c["fields"].toObject();
  if (!fields.contains("Word") || !fields.contains("Word Reading") ||
      !fields.contains("Word Meaning") || c["ord"].toInt(-1) != 0 ||
      c["type"].toInt() == 0 || c["queue"].toInt(-1) < 0 ||
      c["reps"].toInt() < 1 || c["cardId"].toInteger() <= 0)
    return std::nullopt;
  Subject s;
  s.id = localId;
  s.source = "anki";
  s.sourceId = QString::number(c["cardId"].toInteger());
  s.deck = c["deckName"].toString();
  s.kind = "vocabulary";
  s.characters = ankiPlainText(field(c, "Word"));
  const QString reading = ankiPlainText(field(c, "Word Reading"));
  const QString meaning = ankiPlainText(field(c, "Word Meaning"));
  QStringList readings = reading.split(QRegularExpression("\\s*[・、,/／]\\s*"),
                                       Qt::SkipEmptyParts);
  static const QRegularExpression kana(
      "^[\\x{3041}-\\x{3096}\\x{30a1}-\\x{30fa}ー]+$");
  if (s.characters.isEmpty() || s.characters.size() > 32 || meaning.isEmpty() ||
      meaning.size() > 600 || readings.isEmpty())
    return std::nullopt;
  for (const auto &alias : readings)
    if (!kana.match(alias).hasMatch())
      return std::nullopt;
  s.readings = readings;
  // Keep the deck's complete definition. Splitting commas breaks qualifiers.
  s.meanings = {meaning};
  s.contextJa = ankiPlainText(field(c, "Sentence"));
  s.contextEn = ankiPlainText(field(c, "Sentence Meaning"));
  s.notes = ankiPlainText(field(c, "Notes"));
  const int interval = c["interval"].toInt();
  // Weight buckets only, never presented as WaniKani SRS stages.
  s.stage = c["type"].toInt() != 2 ? 1
            : interval < 7         ? 4
            : interval < 21        ? 5
            : interval < 60        ? 7
            : interval < 180       ? 8
                                   : 9;
  int again = 0, rated = 0;
  double recent = 0;
  qint64 first = 0;
  const auto now = QDateTime::currentDateTimeUtc();
  for (const auto &value : reviews) {
    const auto r = value.toObject();
    const int ease = r["ease"].toInt(), type = r["type"].toInt(-1);
    const qint64 id = r["id"].toInteger();
    // Manual/rescheduling entries are not recall evidence.
    if (ease < 1 || ease > 4 || type < 0 || type > 2 || id <= 0)
      continue;
    if (!first || id < first)
      first = id;
    ++rated;
    if (ease == 1) {
      ++again;
      double days =
          QDateTime::fromMSecsSinceEpoch(id, QTimeZone::UTC).secsTo(now) /
          86400.0;
      if (days >= 0 && days <= 7)
        recent += std::exp(-days / 3);
    }
  }
  if (first)
    s.startedAt = QDateTime::fromMSecsSinceEpoch(first, QTimeZone::UTC);
  s.sourceData = {
      {"profile", profile},
      {"card_id", s.sourceId},
      {"note_id", QString::number(c["note"].toInteger())},
      {"deck", s.deck},
      {"note_type", c["modelName"]},
      {"interval_days", interval},
      {"queue", c["queue"]},
      {"type", c["type"]},
      {"reps", c["reps"]},
      {"lapses", c["lapses"]},
      {"due", c["due"]},
      {"factor", c["factor"]},
      {"mod", c["mod"]},
      {"review_count", rated},
      {"again_count", again},
      {"recent_trouble", recent},
      {"persistent_trouble", again / double(rated + 8) * std::log1p(again)},
      {"word_furigana", ankiPlainText(field(c, "Word Furigana"))},
      {"sentence_furigana", ankiPlainText(field(c, "Sentence Furigana"))},
      {"pitch_notes", ankiPlainText(field(c, "Pitch Accent Notes"))},
      {"frequency", ankiPlainText(field(c, "Frequency"))},
      {"pitch", kaishiPitch(field(c, "Pitch Accent"))},
      {"evidence_scope", "whole-card"}};
  s.wordAudio =
      ankiMediaPath(mediaDirectory, audioName(field(c, "Word Audio")));
  s.sentenceAudio =
      ankiMediaPath(mediaDirectory, audioName(field(c, "Sentence Audio")));
  const auto match =
      QRegularExpression("\\bsrc\\s*=\\s*[\"']([^\"']+)[\"']",
                         QRegularExpression::CaseInsensitiveOption)
          .match(field(c, "Picture"));
  s.picture = ankiMediaPath(mediaDirectory, match.captured(1));
  return s;
}

AnkiSource::AnkiSource(Store &s, QObject *parent, QUrl endpoint)
    : QObject(parent), store(s), client(this, endpoint) {
  QSqlQuery q(store.db);
  for (const auto &sql :
       {"CREATE TABLE IF NOT EXISTS anki_items (id INTEGER PRIMARY KEY "
        "AUTOINCREMENT, profile TEXT NOT NULL, card_id TEXT NOT NULL, deck "
        "TEXT NOT NULL, data_json TEXT NOT NULL, reviews_json TEXT NOT NULL, "
        "active INTEGER NOT NULL DEFAULT 1, UNIQUE(profile,card_id))",
        "CREATE TABLE IF NOT EXISTS anki_meta (key TEXT PRIMARY KEY, data_json "
        "TEXT NOT NULL)"})
    if (!q.exec(sql))
      error = q.lastError().text();
}
void AnkiSource::finish(bool ok, const QString &message) {
  running = false;
  emit finished(ok, message);
}
void AnkiSource::discover() {
  if (running)
    return;
  running = true;
  client.request(
      "getActiveProfile", {}, [this](QJsonValue value, QString error) {
        if (!error.isEmpty() || !value.isString() || value.toString().isEmpty())
          return finish(false,
                        error.isEmpty() ? "No active Anki profile." : error);
        const QString active = value.toString();
        client.request(
            "deckNames", {}, [this, active](QJsonValue value, QString error) {
              if (!error.isEmpty() || !value.isArray())
                return finish(false, error.isEmpty() ? "Invalid Anki deck list."
                                                     : error);
              QStringList names;
              for (const auto &name : value.toArray())
                names << name.toString();
              emit decksFound(active, names);
              finish(true, "Connected to Anki · " + active);
            });
      });
}
void AnkiSource::importDecks(const QString &selectedProfile,
                             const QStringList &selectedDecks) {
  if (running)
    return;
  if (!error.isEmpty())
    return finish(false, error);
  if (selectedProfile.isEmpty() || selectedDecks.isEmpty())
    return finish(false, "Choose at least one deck first.");
  running = true;
  profile = selectedProfile;
  decks = selectedDecks;
  cards = {};
  reviews = {};
  ids = {};
  cursor = 0;
  mediaDirectory.clear();
  emit progress("Reading Anki…");
  client.request(
      "getActiveProfile", {}, [this](QJsonValue value, QString error) {
        if (!error.isEmpty())
          return finish(false, error);
        if (value.toString() != profile)
          return finish(
              false, "Anki profile changed. Refresh decks and choose again.");
        client.request(
            "getMediaDirPath", {}, [this](QJsonValue value, QString error) {
              if (!error.isEmpty())
                return finish(false, error);
              mediaDirectory = value.toString();
              QStringList queries;
              for (QString deck : decks) {
                deck.replace('\\', "\\\\");
                deck.replace('"', "\\\"");
                // Escape wildcard characters in user deck names per Anki search
                // syntax.
                deck.replace('*', "\\*");
                deck.replace('_', "\\_");
                queries << "\"deck:" + deck + "\"";
              }
              const QString query = "(" + queries.join(" or ") +
                                    ") -is:new -is:suspended -is:buried";
              client.request(
                  "findCards", {{"query", query}},
                  [this](QJsonValue value, QString error) {
                    if (!error.isEmpty() || !value.isArray())
                      return finish(false, error.isEmpty()
                                               ? "Invalid Anki card list."
                                               : error);
                    ids = value.toArray();
                    if (ids.size() > 10000)
                      return finish(
                          false,
                          "Choose fewer decks (10,000-card import limit).");
                    nextBatch();
                  });
            });
      });
}
void AnkiSource::nextBatch() {
  if (cursor >= ids.size()) {
    // Do not commit a snapshot spanning two profiles.
    client.request("getActiveProfile", {}, [this](QJsonValue v, QString e) {
      if (!e.isEmpty() || v.toString() != profile)
        finish(false,
               e.isEmpty()
                   ? "Anki profile changed during import; cache unchanged."
                   : e);
      else
        commit();
    });
    return;
  }
  QJsonArray batch;
  for (int i = cursor; i < qMin(cursor + 32, int(ids.size())); ++i)
    batch.append(ids[i]);
  emit progress(
      QString("Reading Anki · %1 / %2 cards").arg(cursor).arg(ids.size()));
  client.request(
      "cardsInfo", {{"cards", batch}},
      [this, batch](QJsonValue value, QString error) {
        if (!error.isEmpty() || !value.isArray() ||
            value.toArray().size() != batch.size())
          return finish(false, error.isEmpty()
                                   ? "Incomplete Anki cards; cache unchanged."
                                   : error);
        for (const auto &v : value.toArray()) {
          auto c = v.toObject();
          c.remove("question");
          c.remove("answer");
          c.remove("css");
          cards.append(c);
        }
        client.request(
            "getReviewsOfCards", {{"cards", batch}},
            [this, batch](QJsonValue value, QString error) {
              if (!error.isEmpty() || !value.isObject())
                return finish(false,
                              error.isEmpty()
                                  ? "Invalid Anki reviews; cache unchanged."
                                  : error);
              const auto object = value.toObject();
              for (const auto &id : batch) {
                const auto key = QString::number(id.toInteger());
                if (!object[key].isArray())
                  return finish(false,
                                "Incomplete Anki reviews; cache unchanged.");
                reviews[key] = object[key];
              }
              cursor += batch.size();
              QTimer::singleShot(0, this, &AnkiSource::nextBatch);
            });
      });
}
void AnkiSource::commit() {
  if (!store.db.transaction())
    return finish(false, store.db.lastError().text());
  auto fail = [this](QString error) {
    store.db.rollback();
    finish(false, "Could not cache Anki: " + error);
  };
  QSqlQuery q(store.db);
  if (!q.exec("UPDATE anki_items SET active=0"))
    return fail(q.lastError().text());
  int imported = 0;
  for (const auto &value : cards) {
    auto card = value.toObject();
    const auto id = QString::number(card["cardId"].toInteger());
    auto history = reviews[id].toArray();
    if (!kaishiSubject(card, history, -1, profile, mediaDirectory))
      continue;
    q.prepare("INSERT INTO "
              "anki_items(profile,card_id,deck,data_json,reviews_json,active) "
              "VALUES(?,?,?,?,?,1) ON CONFLICT(profile,card_id) DO UPDATE SET "
              "deck=excluded.deck,data_json=excluded.data_json,reviews_json="
              "excluded.reviews_json,active=1");
    q.addBindValue(profile);
    q.addBindValue(id);
    q.addBindValue(card["deckName"].toString());
    q.addBindValue(
        QString::fromUtf8(QJsonDocument(card).toJson(QJsonDocument::Compact)));
    q.addBindValue(QString::fromUtf8(
        QJsonDocument(history).toJson(QJsonDocument::Compact)));
    if (!q.exec())
      return fail(q.lastError().text());
    ++imported;
  }
  QJsonObject meta{{"profile", profile},
                   {"decks", QJsonArray::fromStringList(decks)},
                   {"media_directory", mediaDirectory},
                   {"synced_at", QDateTime::currentDateTimeUtc().toString(
                                     Qt::ISODateWithMs)},
                   {"imported", imported},
                   {"skipped", cards.size() - imported}};
  q.prepare(
      "INSERT OR REPLACE INTO anki_meta(key,data_json) VALUES('snapshot',?)");
  q.addBindValue(
      QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact)));
  if (!q.exec())
    return fail(q.lastError().text());
  if (!store.db.commit())
    return fail(store.db.lastError().text());
  finish(true, QString("%1 learned cards ready · %2 unsupported cards skipped")
                   .arg(imported)
                   .arg(cards.size() - imported));
}
QJsonObject AnkiSource::metadata() const {
  QSqlQuery q(store.db);
  q.exec("SELECT data_json FROM anki_meta WHERE key='snapshot'");
  return q.next() ? QJsonDocument::fromJson(q.value(0).toByteArray()).object()
                  : QJsonObject{};
}
QVector<Subject> AnkiSource::subjects(const QString &selectedProfile,
                                      const QStringList &selectedDecks) const {
  QVector<Subject> out;
  const auto meta = metadata();
  if (meta["profile"].toString() != selectedProfile)
    return out;
  QSqlQuery q(store.db);
  q.prepare("SELECT id,deck,data_json,reviews_json FROM anki_items WHERE "
            "active=1 AND profile=?");
  q.addBindValue(selectedProfile);
  q.exec();
  while (q.next()) {
    const QString deck = q.value(1).toString();
    bool selected = false;
    for (const auto &parent : selectedDecks)
      selected |= deck == parent || deck.startsWith(parent + "::");
    if (!selected)
      continue;
    auto s = kaishiSubject(
        QJsonDocument::fromJson(q.value(2).toByteArray()).object(),
        QJsonDocument::fromJson(q.value(3).toByteArray()).array(),
        -q.value(0).toInt(), selectedProfile,
        meta["media_directory"].toString());
    if (s) {
      s->sourceData["synced_at"] = meta["synced_at"];
      out.append(*s);
    }
  }
  return out;
}
Evidence AnkiSource::evidence(const QVector<Subject> &pool) const {
  Evidence out;
  for (const auto &s : pool) {
    if (s.source != "anki")
      continue;
    for (bool reading : {true, false})
      out[skillKey(s.id, reading)] = {
          s.sourceData["recent_trouble"].toDouble(),
          s.sourceData["persistent_trouble"].toDouble()};
  }
  return out;
}
