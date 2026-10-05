#include "sync.h"

QJsonArray mergeSyncRows(const QJsonArray &cached, const QJsonArray &updates) {
  QMap<int, QJsonObject> byId;
  for (const auto &rows : {cached, updates})
    for (const auto &row : rows)
      byId[row.toObject()["id"].toInt()] = row.toObject();
  QJsonArray result;
  for (const auto &row : byId)
    result.append(row);
  return result;
}
qint64 syncRetrySeconds(const QByteArray &retryAfter, const QByteArray &reset,
                        qint64 now) {
  bool numeric = false;
  qint64 after = retryAfter.toLongLong(&numeric);
  if (!numeric) {
    auto date =
        QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
    after = date.isValid() ? date.toSecsSinceEpoch() - now : 0;
  }
  return qMax(qint64(2), qMax(after, reset.toLongLong() - now + 1));
}
Sync::Sync(Store &s, QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent), store(s), network(this),
      transport(manager ? manager : &network) {}
void Sync::start(const QString &key) {
  if (running)
    return;
  token = key.trimmed();
  if (token.isEmpty()) {
    emit finished(false, "Add your WaniKani token in Settings to sync.");
    return;
  }
  previous = store.cache();
  fingerprint = QString::fromLatin1(
      QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256)
          .toHex());
  auto last =
      QDateTime::fromString(previous["synced_at"].toString(), Qt::ISODate);
  if (previous["sync_token_fingerprint"].toString() == fingerprint &&
      last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) >= 0 &&
      last.secsTo(QDateTime::currentDateTimeUtc()) < 60) {
    token.clear();
    emit finished(true,
                  "Already synced within the last minute · using cached data");
    return;
  }
  running = true;
  pending = previous;
  cursors = previous["sync_cursors"].toObject();
  if (previous["sync_version"].toInt() != 1)
    cursors = {};
  const auto full = QDateTime::fromString(
      previous["personal_full_at"].toString(), Qt::ISODate);
  reconcile = !full.isValid() ||
              full.secsTo(QDateTime::currentDateTimeUtc()) > 7 * 86400;
  requests = received = 0;
  receivedBytes = 0;
  sections = {"assignments", "study_materials", "subjects",
              "review_statistics"};
  section = "user";
  retries = 0;
  visited.clear();
  schedule(QUrl("https://api.wanikani.com/v2/user"));
}
void Sync::fail(const QString &message) {
  running = false;
  token.clear();
  emit finished(false, message);
}
void Sync::schedule(const QUrl &url) {
  const qint64 delay =
      qMax(qint64(requestDelay),
           (notBefore - QDateTime::currentSecsSinceEpoch()) * 1000);
  if (delay > 86400000) {
    fail("WaniKani requested a long pause. Try later; cached practice is "
         "available.");
    return;
  }
  QTimer::singleShot(int(delay), this, [this, url] { fetch(url); });
}
void Sync::fetch(const QUrl &url) {
  // Authentication never leaves this origin, including redirects and
  // pagination.
  if (url.scheme() != "https" || url.host() != "api.wanikani.com" ||
      url.port(443) != 443 || !url.userInfo().isEmpty() ||
      url.path() != "/v2/" + section) {
    fail("Sync stopped: unexpected API destination.");
    return;
  }
  emit progress(QString("%1 %2 · %3 requests · %4 records received…")
                    .arg(section == "user" ? "Checking"
                         : incremental     ? "Updating"
                                           : "Loading",
                         section)
                    .arg(requests)
                    .arg(received));
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
  request.setRawHeader("Wanikani-Revision", "20170710");
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    "Moji Nook/0.3 (read-only practice; incremental sync)");
  if (section == "user" &&
      previous["sync_token_fingerprint"].toString() == fingerprint &&
      previous["user"].isObject()) {
    const auto etag = previous["user_etag"].toString().toUtf8();
    const auto modified = previous["user_last_modified"].toString().toUtf8();
    if (!etag.isEmpty())
      request.setRawHeader("If-None-Match", etag);
    else if (!modified.isEmpty())
      request.setRawHeader("If-Modified-Since", modified);
  }
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(30000);
  ++requests;
  auto *reply = transport->get(request); // GET is the only HTTP operation.
  connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
    int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto bytes = reply->readAll();
    receivedBytes += bytes.size();
    auto err = reply->error();
    auto retryAfter = reply->rawHeader("Retry-After");
    auto resetAt = reply->rawHeader("RateLimit-Reset");
    const auto etag = reply->rawHeader("ETag");
    const auto modified = reply->rawHeader("Last-Modified");
    if (reply->rawHeader("RateLimit-Remaining") == "0" || status == 429)
      notBefore = QDateTime::currentSecsSinceEpoch() +
                  qMax(qint64(60),
                       syncRetrySeconds(retryAfter, resetAt,
                                        QDateTime::currentSecsSinceEpoch()));
    reply->deleteLater();
    if ((status == 429 || status >= 500) && retries++ < 3) {
      const auto seconds =
          qMax(qint64(status == 429 ? 60 : 3 * retries),
               syncRetrySeconds(retryAfter, resetAt,
                                QDateTime::currentSecsSinceEpoch()));
      notBefore = qMax(notBefore, QDateTime::currentSecsSinceEpoch() + seconds);
      emit progress(
          QString("WaniKani is busy; retrying in %1 seconds…").arg(seconds));
      schedule(url);
      return;
    }
    if (status == 401 || status == 403) {
      fail("WaniKani rejected this token. Check it in Settings; cached "
           "practice is still available.");
      return;
    }
    if (status == 304 && section == "user" &&
        previous["sync_token_fingerprint"].toString() == fingerprint &&
        previous["user"].isObject() &&
        (!previous["user_etag"].toString().isEmpty() ||
         !previous["user_last_modified"].toString().isEmpty())) {
      retries = 0;
      nextSection();
      return;
    }
    if (err != QNetworkReply::NoError || status != 200) {
      fail(QString("Sync unavailable (HTTP %1). Your previous cache and stats "
                   "are preserved.")
               .arg(status));
      return;
    }
    retries = 0;
    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
      fail("WaniKani returned unreadable data; the previous cache is "
           "preserved.");
      return;
    }
    auto root = doc.object();
    if (section == "user") {
      auto user = root["data"].toObject();
      if (!user["id"].isString() || !user["subscription"].isObject()) {
        fail("WaniKani returned an incomplete account response.");
        return;
      }
      const auto oldUser = previous["user"].toObject();
      const auto oldId = oldUser["id"].toString();
      if (!oldId.isEmpty() && oldId != user["id"].toString()) {
        fail("This token belongs to a different account. Use a separate "
             "--data-dir to keep its stats separate.");
        return;
      }
      pending["user"] = user;
      if (oldUser["subscription"] != user["subscription"])
        reconcile = true;
      pending["user_etag"] = QString::fromUtf8(etag);
      pending["user_last_modified"] = QString::fromUtf8(modified);
      nextSection();
      return;
    }
    if (!root["data"].isArray() || !root["pages"].isObject() ||
        !(root["pages"].toObject()["next_url"].isNull() ||
          root["pages"].toObject()["next_url"].isString())) {
      fail("WaniKani returned an incomplete collection; the previous cache is "
           "preserved.");
      return;
    }
    // Freeze the first page's collection watermark. Overlap one second to
    // preserve subsecond boundary updates; ID-based merging removes duplicates.
    if (firstPage) {
      const auto timestamp = QDateTime::fromString(
          root["data_updated_at"].toString(), Qt::ISODateWithMs);
      sectionCursor =
          timestamp.isValid()
              ? timestamp.addSecs(-1).toUTC().toString(Qt::ISODateWithMs)
              : cursors[section].toString();
      firstPage = false;
    }
    for (const auto &v : root["data"].toArray()) {
      if (v.toObject()["id"].toInt() <= 0 || !v.toObject()["data"].isObject()) {
        fail("WaniKani returned an invalid record; the previous cache is "
             "preserved.");
        return;
      }
      rows.append(v);
      ++received;
    }
    auto next = root["pages"].toObject()["next_url"].toString();
    if (!next.isEmpty()) {
      if (visited.contains(next)) {
        fail("Sync stopped: pagination repeated a page.");
        return;
      }
      visited.insert(next);
      schedule(QUrl(next));
    } else {
      pending[section] = mergeSyncRows(
          incremental ? previous[section].toArray() : QJsonArray{}, rows);
      cursors[section] = sectionCursor;
      nextSection();
    }
  });
}
void Sync::nextSection() {
  if (sections.isEmpty()) {
    pending["synced_at"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    pending["sync_version"] = 1;
    pending["sync_cursors"] = cursors;
    pending["sync_token_fingerprint"] = fingerprint;
    if (reconcile)
      pending["personal_full_at"] = pending.value("synced_at").toString();
    pending["last_sync_requests"] = requests;
    pending["last_sync_records"] = received;
    pending["last_sync_bytes"] = double(receivedBytes);
    if (!store.observeReviews(pending["review_statistics"].toArray(),
                              pending["synced_at"].toString()) ||
        !store.saveCache(pending)) {
      fail("Could not save the new cache. Check available disk space.");
      return;
    }
    running = false;
    token.clear();
    emit finished(true, QString("Synced · %1 requests · %2 records received · "
                                "%3 KB · practice stays local")
                            .arg(requests)
                            .arg(received)
                            .arg(receivedBytes / 1024));
    return;
  }
  section = sections.takeFirst();
  rows = {};
  visited.clear();
  firstPage = true;
  sectionCursor.clear();
  incremental =
      previous[section].isArray() &&
      QDateTime::fromString(cursors[section].toString(), Qt::ISODateWithMs)
          .isValid() &&
      !(reconcile && section != "subjects");
  QUrl url("https://api.wanikani.com/v2/" + section);
  QUrlQuery query;
  // Mutable started/hidden filters would hide resets and removals from deltas.
  if (section == "assignments")
    query.addQueryItem("subject_types", "kanji,vocabulary,kana_vocabulary");
  if (section == "subjects")
    query.addQueryItem("types", "kanji,vocabulary,kana_vocabulary");
  if (incremental)
    query.addQueryItem("updated_after", cursors[section].toString());
  url.setQuery(query);
  visited.insert(url.toString());
  schedule(url);
}
