#include "wanikani_reviews.h"
#include "sync.h"
#include <QImage>
#include <QImageReader>

namespace {
bool hasJapanese(const QString &text) {
  for (const QChar c : text) {
    const ushort u = c.unicode();
    if ((u >= 0x3040 && u <= 0x30ff) || (u >= 0x3400 && u <= 0x4dbf) ||
        (u >= 0x4e00 && u <= 0x9fff) || (u >= 0xff66 && u <= 0xff9f))
      return true;
  }
  return false;
}
bool onlyKana(const QString &text) {
  for (const QChar c : text) {
    const ushort u = c.unicode();
    if (!((u >= 0x3041 && u <= 0x309f) || (u >= 0x30a1 && u <= 0x30fa) ||
          (u >= 0x30fc && u <= 0x30ff)))
      return false;
  }
  return true;
}
int distance(const QString &a, const QString &b) {
  QVector<int> row(b.size() + 1);
  for (int j = 0; j <= b.size(); ++j)
    row[j] = j;
  for (int i = 1; i <= a.size(); ++i) {
    int diagonal = row[0];
    row[0] = i;
    for (int j = 1; j <= b.size(); ++j) {
      const int above = row[j];
      row[j] = qMin(qMin(row[j] + 1, row[j - 1] + 1),
                    diagonal + (a[i - 1] == b[j - 1] ? 0 : 1));
      diagonal = above;
    }
  }
  return row[b.size()];
}
// Like practice's normalizeMeaning, but WaniKani grades "the dog" against
// "Dog" as typed, so no leading article is dropped.
QString reviewMeaning(QString s) {
  static const QRegularExpression punctuation("[^\\p{L}\\p{N}]+");
  s = s.normalized(QString::NormalizationForm_KC).toCaseFolded();
  s.replace(punctuation, " ");
  return s.simplified();
}
bool typoMatch(const QString &answer, const QString &candidate) {
  return distance(answer, candidate) <=
         meaningTypoTolerance(int(candidate.size()));
}
QString readingTypeName(const QString &kind) {
  return kind == "onyomi"    ? QString("on’yomi")
         : kind == "kunyomi" ? QString("kun’yomi")
         : kind == "nanori"  ? QString("nanori")
                             : QString();
}
} // namespace

int meaningTypoTolerance(int length) {
  return length <= 3 ? 0 : length <= 5 ? 1 : length <= 7 ? 2 : 2 + length / 7;
}

QString reviewStageLabel(int stage) {
  if (stage >= 1 && stage <= 4)
    return QString("Apprentice %1").arg(stage);
  if (stage == 5 || stage == 6)
    return QString("Guru %1").arg(stage - 4);
  return stage <= 0 ? QString("Lesson") : stageName(stage);
}

ReviewCheck checkReviewAnswer(const Subject &subject, bool reading,
                              const QString &text) {
  if (text.trimmed().isEmpty())
    return {ReviewVerdict::Retry, "Type an answer first."};
  if (!reading) {
    if (hasJapanese(text))
      return {ReviewVerdict::Retry,
              "WaniKani wants the meaning, not the reading."};
    const auto answer = reviewMeaning(text);
    for (const auto &m : subject.meanings)
      if (answer == reviewMeaning(m))
        return {ReviewVerdict::Correct, {}};
    for (const auto &m : subject.blacklist)
      if (answer == reviewMeaning(m))
        return {ReviewVerdict::Incorrect, {}};
    static const QRegularExpression digit("\\d");
    if (answer.contains(digit))
      return {ReviewVerdict::Incorrect, {}};
    for (const auto &m : subject.meanings)
      if (typoMatch(answer, reviewMeaning(m)))
        return {ReviewVerdict::Correct, {}};
    for (const auto &m : subject.meanings) {
      const auto candidate = reviewMeaning(m);
      if (candidate.startsWith("to ") && typoMatch(answer, candidate.mid(3)))
        return {ReviewVerdict::Retry,
                "This meaning is a verb. Start it with “to”."};
    }
    auto spoken = subject.readings;
    if (subject.kind == "kana_vocabulary")
      spoken.append(subject.characters);
    for (const auto &r : spoken)
      if (toKana(text) == toKana(r))
        return {ReviewVerdict::Retry,
                "WaniKani wants the meaning, not the reading."};
    return {ReviewVerdict::Incorrect, {}};
  }
  const auto kana = toKana(text);
  if (!onlyKana(kana))
    return {ReviewVerdict::Retry, "Type the reading in kana."};
  for (const auto &r : subject.readings)
    if (kana == toKana(r))
      return {ReviewVerdict::Correct, {}};
  if (subject.kind == "kanji") {
    const auto wanted = readingTypeName(subject.readingKind);
    for (const auto &r : subject.otherReadings)
      if (kana == toKana(r) && !wanted.isEmpty())
        return {ReviewVerdict::Retry,
                QString("WaniKani wants the %1 reading.").arg(wanted)};
  }
  return {ReviewVerdict::Incorrect, {}};
}

const QString reviewAccessDenied =
    "This token can't submit reviews. Create a WaniKani token with "
    "reviews:create checked, then paste it above.";

Subject parseReviewSubject(const QJsonObject &root,
                           const QStringList &synonyms) {
  const auto d = root["data"].toObject();
  Subject s;
  s.id = root["id"].toInt();
  s.kind = root["object"].toString();
  s.characters = d["characters"].toString();
  s.level = d["level"].toInt();
  s.url = d["document_url"].toString();
  for (const auto &v : d["meanings"].toArray()) {
    const auto m = v.toObject();
    if (!m["accepted_answer"].toBool())
      continue;
    if (m["primary"].toBool())
      s.meanings.prepend(m["meaning"].toString());
    else
      s.meanings.append(m["meaning"].toString());
  }
  for (const auto &v : d["auxiliary_meanings"].toArray()) {
    const auto m = v.toObject();
    (m["type"] == "whitelist" ? s.meanings : s.blacklist)
        .append(m["meaning"].toString());
  }
  s.meanings.append(synonyms);
  s.meanings.removeDuplicates();
  for (const auto &v : d["readings"].toArray()) {
    const auto r = v.toObject();
    if (!r["accepted_answer"].toBool())
      s.otherReadings.append(r["reading"].toString());
    else if (r["primary"].toBool()) {
      s.readings.prepend(r["reading"].toString());
      s.readingKind = r["type"].toString();
    } else
      s.readings.append(r["reading"].toString());
  }
  const auto contexts = d["context_sentences"].toArray();
  if (!contexts.isEmpty()) {
    s.contextJa = contexts[0].toObject()["ja"].toString();
    s.contextEn = contexts[0].toObject()["en"].toString();
  }
  return s;
}

bool reviewRefreshDue(const QDateTime &last, const QDateTime &now) {
  return !last.isValid() || last.secsTo(now) >= 300 ||
         last.toSecsSinceEpoch() / 3600 != now.toSecsSinceEpoch() / 3600;
}

namespace {
const QSet<QString> apiPaths = {"/v2/assignments", "/v2/subjects",
                                "/v2/study_materials", "/v2/reviews"};
bool apiDestination(const QUrl &url) {
  static const QRegularExpression oneAssignment("^/v2/assignments/\\d+$");
  return url.scheme() == "https" && url.host() == "api.wanikani.com" &&
         url.port(443) == 443 && url.userInfo().isEmpty() &&
         (apiPaths.contains(url.path()) ||
          oneAssignment.match(url.path()).hasMatch());
}
QJsonObject progressEntry(const ReviewItem &item) {
  return {{"assignment_id", item.assignmentId},
          {"available_at", item.availableAt},
          {"meaning_done", item.meaningDone},
          {"reading_done", item.readingDone},
          {"meaning_misses", item.meaningMisses},
          {"reading_misses", item.readingMisses}};
}
void restore(ReviewItem &item, const QJsonObject &saved) {
  item.meaningDone = saved["meaning_done"].toBool();
  item.readingDone = saved["reading_done"].toBool();
  item.meaningMisses = saved["meaning_misses"].toInt();
  item.readingMisses = saved["reading_misses"].toInt();
}
// SVG first: WaniKani now answers 403 for many PNG variants. PNG sizes nearest
// 128 px remain the fallback where Qt lacks its SVG plugin.
QList<QUrl> pictureUrls(const QJsonArray &images) {
  QList<QPair<int, QUrl>> ranked;
  for (const auto &v : images) {
    const auto image = v.toObject();
    const auto type = image["content_type"].toString();
    const int width = image["metadata"]
                          .toObject()["dimensions"]
                          .toString()
                          .section('x', 0, 0)
                          .toInt();
    if (type == "image/png")
      ranked.append({qAbs(width - 128), QUrl(image["url"].toString())});
    else if (type == "image/svg+xml")
      ranked.append({-1, QUrl(image["url"].toString())});
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto &a, const auto &b) { return a.first < b.first; });
  QList<QUrl> urls;
  for (const auto &r : ranked)
    urls.append(r.second);
  return urls;
}
} // namespace

WaniKaniReviews::WaniKaniReviews(const QString &dataDir, QObject *parent,
                                 QNetworkAccessManager *manager)
    : QObject(parent), directory(dataDir), network(this),
      transport(manager ? manager : &network) {
  retryTimer.setSingleShot(true);
  connect(&retryTimer, &QTimer::timeout, this, &WaniKaniReviews::submitNext);
  QFile f(directory + "/wanikani_review_progress.json");
  if (f.open(QIODevice::ReadOnly))
    progress = QJsonDocument::fromJson(f.readAll()).object()["items"].toObject();
}

void WaniKaniReviews::send(const QUrl &url, std::optional<QByteArray> body,
                           Handler done) {
  // The token never leaves the API origin, including redirects and pages.
  if (!apiDestination(url)) {
    done({-1, true, {}, {}, {}});
    return;
  }
  const qint64 wait =
      qMax(qint64(0), notBefore - QDateTime::currentSecsSinceEpoch()) * 1000;
  QTimer::singleShot(int(qMin(wait, qint64(INT_MAX))), this,
                     [this, url, body, done] {
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setRawHeader("Wanikani-Revision", "20170710");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Moji Nook/0.3 (WaniKani reviews)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply *reply;
    if (body) {
      request.setHeader(QNetworkRequest::ContentTypeHeader,
                        "application/json; charset=utf-8");
      reply = transport->post(request, *body);
    } else
      reply = transport->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
      reply->deleteLater();
      Reply r;
      r.status =
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      r.body = reply->readAll();
      r.retryAfter = reply->rawHeader("Retry-After");
      r.reset = reply->rawHeader("RateLimit-Reset");
      r.networkError = r.status == 0;
      const auto now = QDateTime::currentSecsSinceEpoch();
      if (r.status == 429 || reply->rawHeader("RateLimit-Remaining") == "0")
        notBefore = qMax(notBefore,
                         now + syncRetrySeconds(r.retryAfter, r.reset, now));
      done(r);
    });
  });
}

void WaniKaniReviews::checkAccess(const QString &key) {
  const int probe = ++probeEpoch;
  token = key.trimmed();
  if (token.isEmpty()) {
    emit accessChecked(false, "Paste your WaniKani token above first.");
    return;
  }
  // An empty review is rejected as 422 only when the token may create reviews.
  send(QUrl("https://api.wanikani.com/v2/reviews"),
       QByteArray(R"({"review":{}})"), [this, probe](const Reply &r) {
         if (probe != probeEpoch)
           return;
         if (r.status == 422)
           emit accessChecked(true, "Token can submit reviews");
         else if (r.status == 401 || r.status == 403)
           emit accessChecked(false, reviewAccessDenied);
         else
           emit accessChecked(false, "Couldn't reach WaniKani to check the "
                                     "token. Try again in a moment.");
       });
}

void WaniKaniReviews::refresh(const QString &key) {
  token = key.trimmed();
  reload();
}

void WaniKaniReviews::reload() {
  attemptedAt = QDateTime::currentDateTimeUtc();
  if (token.isEmpty()) {
    emit refreshed(false, "Add your WaniKani token to load reviews.");
    return;
  }
  loading = true;
  const int run = ++epoch;
  QUrl url("https://api.wanikani.com/v2/assignments");
  url.setQuery("immediately_available_for_review=true");
  collect({url}, {}, 0, [this, run](const QJsonArray &rows) {
    QJsonArray assignments;
    QStringList ids;
    for (const auto &v : rows) {
      const auto d = v.toObject()["data"].toObject();
      if (d["hidden"].toBool())
        continue;
      assignments.append(v);
      ids.append(QString::number(d["subject_id"].toInt()));
    }
    ids.removeDuplicates();
    QList<QUrl> subjectPages, materialPages;
    for (int i = 0; i < ids.size(); i += 200) {
      const auto chunk = ids.mid(i, 200).join(',');
      QUrl subjects("https://api.wanikani.com/v2/subjects");
      subjects.setQuery("ids=" + chunk);
      subjectPages.append(subjects);
      QUrl materials("https://api.wanikani.com/v2/study_materials");
      materials.setQuery("subject_ids=" + chunk);
      materialPages.append(materials);
    }
    collect(subjectPages, {}, 0,
            [this, run, assignments, materialPages](const QJsonArray &subjects) {
      collect(materialPages, {}, 0,
              [this, run, assignments, subjects](const QJsonArray &materials) {
        if (run == epoch)
          build(assignments, subjects, materials);
      });
    });
  });
}

void WaniKaniReviews::collect(QList<QUrl> pages, QJsonArray rows, int attempt,
                              std::function<void(QJsonArray)> done) {
  const int run = epoch;
  if (pages.isEmpty()) {
    done(rows);
    return;
  }
  const QUrl url = pages.first();
  send(url, std::nullopt, [=](const Reply &r) mutable {
    if (run != epoch)
      return;
    if ((r.status == 429 || r.status >= 500 || r.networkError) &&
        r.status != -1 && attempt < 3) {
      const auto now = QDateTime::currentSecsSinceEpoch();
      notBefore = qMax(notBefore, now + (r.status == 429 ? syncRetrySeconds(
                                             r.retryAfter, r.reset, now)
                                                         : 0));
      QTimer::singleShot(getRetryMs * (attempt + 1), this, [=] {
        if (run == epoch)
          collect(pages, rows, attempt + 1, done);
      });
      return;
    }
    if (r.status == -1) {
      failRefresh("Reviews stopped: unexpected API destination.");
      return;
    }
    if (r.status == 401 || r.status == 403) {
      failRefresh("WaniKani rejected this token. Check it in Settings.");
      return;
    }
    const auto root = QJsonDocument::fromJson(r.body).object();
    if (r.status != 200 || !root["data"].isArray()) {
      failRefresh(QString("Couldn't load WaniKani reviews (HTTP %1) · practice "
                          "as usual.")
                      .arg(r.status));
      return;
    }
    for (const auto &v : root["data"].toArray())
      rows.append(v);
    pages.removeFirst();
    const QUrl next(root["pages"].toObject()["next_url"].toString());
    if (!next.isEmpty()) {
      if (next.path() != url.path() || next == url) {
        failRefresh("Reviews stopped: unexpected API destination.");
        return;
      }
      pages.prepend(next);
    }
    collect(pages, rows, 0, done);
  });
}

void WaniKaniReviews::failRefresh(const QString &message) {
  loading = false;
  emit refreshed(false, message);
}

void WaniKaniReviews::build(const QJsonArray &assignments,
                            const QJsonArray &subjects,
                            const QJsonArray &materials) {
  QHash<int, QJsonObject> subjectById;
  for (const auto &v : subjects)
    subjectById[v.toObject()["id"].toInt()] = v.toObject();
  QHash<int, QStringList> synonyms;
  for (const auto &v : materials) {
    const auto d = v.toObject()["data"].toObject();
    for (const auto &m : d["meaning_synonyms"].toArray())
      synonyms[d["subject_id"].toInt()].append(m.toString());
  }
  QVector<ReviewItem> items;
  QJsonObject kept;
  QHash<int, QList<QUrl>> pictures;
  for (const auto &v : assignments) {
    const auto d = v.toObject()["data"].toObject();
    ReviewItem item;
    item.assignmentId = v.toObject()["id"].toInt();
    item.availableAt = d["available_at"].toString();
    const int subjectId = d["subject_id"].toInt();
    if (progress.contains(item.key()) && !settled.contains(item.key()))
      kept[item.key()] = progress[item.key()];
    if (item.assignmentId <= 0 || !subjectById.contains(subjectId) ||
        settled.contains(item.key()))
      continue;
    const auto resource = subjectById[subjectId];
    item.subject = parseReviewSubject(resource, synonyms.value(subjectId));
    item.subject.stage = d["srs_stage"].toInt();
    if (item.subject.meanings.isEmpty())
      continue;
    if (item.subject.characters.isEmpty()) {
      const QString path = QString("%1/wanikani_radicals/%2.png")
                               .arg(directory)
                               .arg(subjectId);
      if (QFileInfo::exists(path))
        item.subject.picture = path;
      else
        pictures[subjectId] =
            pictureUrls(resource["data"].toObject()["character_images"].toArray());
    }
    restore(item, progress[item.key()].toObject());
    items.append(item);
  }
  std::stable_sort(items.begin(), items.end(),
                   [](const ReviewItem &a, const ReviewItem &b) {
                     return a.availableAt != b.availableAt
                                ? a.availableAt < b.availableAt
                                : a.assignmentId < b.assignmentId;
                   });
  queue = items;
  // Progress for an assignment WaniKani no longer offers can never be sent.
  if (kept != progress) {
    progress = kept;
    saveProgress();
  }
  loading = false;
  for (auto it = pictures.begin(); it != pictures.end(); ++it)
    fetchPicture(it.key(), it.value());
  emit refreshed(true, waitingText());
  submitNext();
}

QString WaniKaniReviews::waitingText() const {
  return queue.isEmpty() ? QString("No reviews waiting · practice as usual")
                         : QString("%1 review%2 waiting")
                               .arg(queue.size())
                               .arg(queue.size() == 1 ? "" : "s");
}

void WaniKaniReviews::fetchPicture(int subjectId, QList<QUrl> urls) {
  if (urls.isEmpty())
    return;
  const QUrl url = urls.takeFirst();
  if (url.scheme() != "https" || url.host() != "files.wanikani.com" ||
      !url.userInfo().isEmpty() || url.port(443) != 443) {
    fetchPicture(subjectId, urls);
    return;
  }
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(30000);
  auto *reply = transport->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, subjectId, urls] {
    reply->deleteLater();
    QByteArray bytes = reply->readAll();
    // Qt's SVG renderer ignores CSS variables; keep their fallback colors.
    static const QRegularExpression cssVariable(R"(var\(--[\w-]+,\s*([^)]+)\))");
    if (bytes.trimmed().startsWith('<'))
      bytes = QString::fromUtf8(bytes).replace(cssVariable, "\\1").toUtf8();
    QBuffer buffer(&bytes);
    QImageReader reader(&buffer);
    reader.setScaledSize(QSize(128, 128));
    const QImage image =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() ==
                200
            ? reader.read()
            : QImage();
    QDir().mkpath(directory + "/wanikani_radicals");
    const QString path =
        QString("%1/wanikani_radicals/%2.png").arg(directory).arg(subjectId);
    QSaveFile file(path);
    if (image.isNull() || !file.open(QIODevice::WriteOnly) ||
        !image.save(&file, "PNG") || !file.commit()) {
      fetchPicture(subjectId, urls);
      return;
    }
    for (auto &item : queue)
      if (item.subject.id == subjectId)
        item.subject.picture = path;
  });
}

std::optional<Challenge> WaniKaniReviews::next() {
  QVector<QPair<int, bool>> halves;
  int window = 0;
  for (int i = 0; i < queue.size() && window < 10; ++i) {
    const auto &item = queue[i];
    if (item.complete() ||
        (item.subject.characters.isEmpty() && item.subject.picture.isEmpty()))
      continue;
    ++window;
    if (!item.meaningDone)
      halves.append({i, false});
    if (item.needsReading() && !item.readingDone)
      halves.append({i, true});
  }
  if (halves.isEmpty())
    return std::nullopt;
  const auto pick = halves[int(random.bounded(quint32(halves.size())))];
  const auto &item = queue[pick.first];
  Challenge c;
  c.subject = item.subject;
  c.mode = Mode::Typing;
  c.reading = pick.second;
  c.wanikaniReview = true;
  c.selectionReason = QString("WaniKani review · %1 waiting").arg(queue.size());
  c.event["wanikani_review"] = true;
  c.event["assignment_id"] = item.assignmentId;
  c.event["available_at"] = item.availableAt;
  c.event["subject_kind"] = item.subject.kind;
  return c;
}

// Saved even when the item has left the queue (mode turned off, token
// rejected), so a miss is never lost before the item is sent.
void WaniKaniReviews::answer(const Challenge &c, bool correct) {
  if (!c.wanikaniReview)
    return;
  ReviewItem item;
  item.assignmentId = c.event["assignment_id"].toInt();
  item.availableAt = c.event["available_at"].toString();
  item.subject.kind = c.event["subject_kind"].toString();
  const auto key = item.key();
  if (settled.contains(key))
    return;
  const auto queued =
      std::find_if(queue.begin(), queue.end(),
                   [&key](const ReviewItem &i) { return i.key() == key; });
  if (queued != queue.end())
    item = *queued;
  else
    restore(item, progress[key].toObject());
  if (item.complete())
    return;
  item.record(c.reading, correct);
  progress[key] = progressEntry(item);
  saveProgress();
  if (queued != queue.end()) {
    *queued = item;
    if (item.complete())
      submitNext();
  }
}

void WaniKaniReviews::saveProgress() {
  QSaveFile f(directory + "/wanikani_review_progress.json");
  if (!f.open(QIODevice::WriteOnly))
    return;
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  const auto bytes = QJsonDocument(QJsonObject{{"version", 1},
                                               {"items", progress}})
                         .toJson(QJsonDocument::Compact);
  if (f.write(bytes) == bytes.size())
    f.commit();
}

void WaniKaniReviews::settle(const QString &key) {
  settled.insert(key);
  progress.remove(key);
  saveProgress();
  queue.erase(std::remove_if(queue.begin(), queue.end(),
                             [&key](const ReviewItem &i) {
                               return i.key() == key;
                             }),
              queue.end());
  if (queue.isEmpty())
    reload();
  else
    submitNext();
}

void WaniKaniReviews::submitNext() {
  if (!submitting.isEmpty() || token.isEmpty() || retryTimer.isActive())
    return;
  const auto found =
      std::find_if(queue.cbegin(), queue.cend(),
                   [](const ReviewItem &item) { return item.complete(); });
  if (found == queue.cend())
    return;
  const ReviewItem item = *found;
  submitting = item.key();
  // The answers belong to one review instance. If it was reviewed elsewhere,
  // came due again, or isn't due, WaniKani's current review never got them.
  send(QUrl(QString("https://api.wanikani.com/v2/assignments/%1")
                .arg(item.assignmentId)),
       std::nullopt, [this, item](const Reply &r) {
         if (r.status == 200 || r.status == 404) {
           const auto d =
               QJsonDocument::fromJson(r.body).object()["data"].toObject();
           const auto at = QDateTime::fromString(d["available_at"].toString(),
                                                 Qt::ISODateWithMs);
           if (r.status == 200 && d["available_at"] == item.availableAt &&
               at.isValid() && at <= QDateTime::currentDateTimeUtc() &&
               !d["hidden"].toBool()) {
             post(item);
             return;
           }
           submitting.clear();
           settle(item.key());
           return;
         }
         submitting.clear();
         retryLater(r);
       });
}

void WaniKaniReviews::post(const ReviewItem &item) {
  const QJsonObject body{
      {"review", QJsonObject{{"assignment_id", item.assignmentId},
                             {"incorrect_meaning_answers", item.meaningMisses},
                             {"incorrect_reading_answers",
                              item.readingMisses}}}};
  send(QUrl("https://api.wanikani.com/v2/reviews"),
       QJsonDocument(body).toJson(QJsonDocument::Compact),
       [this, item](const Reply &r) {
         submitting.clear();
         const auto key = item.key();
         if (r.status == 201 || r.status == 200) {
           submitFailures = 0;
           settle(key);
           const int stage = QJsonDocument::fromJson(r.body)
                                 .object()["resources_updated"]
                                 .toObject()["assignment"]
                                 .toObject()["data"]
                                 .toObject()["srs_stage"]
                                 .toInt(-1);
           const QString name = item.subject.characters.isEmpty()
                                    ? item.subject.meaning()
                                    : item.subject.characters;
           emit submitted(
               stage < 0 ? QString("%1 · sent to WaniKani").arg(name)
                         : QString("%1 · %2 → %3")
                               .arg(name, reviewStageLabel(item.subject.stage),
                                    reviewStageLabel(stage)));
           return;
         }
         // WaniKani confirmed the review was current, so a rejection won't
         // change on retry.
         if (r.status == 422) {
           settle(key);
           return;
         }
         retryLater(r);
       });
}

void WaniKaniReviews::retryLater(const Reply &r) {
  if (r.status == 401 || r.status == 403) {
    emit accessChecked(false, reviewAccessDenied);
    return;
  }
  ++submitFailures;
  const auto now = QDateTime::currentSecsSinceEpoch();
  const qint64 wait =
      r.status == 429 ? syncRetrySeconds(r.retryAfter, r.reset, now) * 1000
                      : qint64(submitRetryMs) << qMin(submitFailures - 1, 4);
  retryTimer.start(int(qMin(wait, qint64(600000))));
}

void WaniKaniReviews::stop() {
  ++epoch;
  ++probeEpoch;
  loading = false;
  token.clear();
  queue.clear();
  retryTimer.stop();
}
