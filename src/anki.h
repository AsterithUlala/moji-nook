#pragma once
#include "core.h"
#include <QtNetwork>
#include <functional>

// Local AnkiConnect only. No scheduling, editing or remote HTTP actions.
class AnkiClient : public QObject {
  Q_OBJECT
public:
  explicit AnkiClient(QObject *parent = nullptr,
                      QUrl endpoint = QUrl("http://127.0.0.1:8765"));
  using Callback = std::function<void(QJsonValue, QString)>;
  void request(const QString &action, const QJsonObject &params, Callback done);

private:
  QNetworkAccessManager network;
  QUrl endpoint;
};

QString ankiPlainText(QString html);
QString ankiMediaPath(const QString &directory, const QString &filename);
std::optional<Subject> kaishiSubject(const QJsonObject &card,
                                     const QJsonArray &reviews, int localId,
                                     const QString &profile,
                                     const QString &mediaDirectory);

class AnkiSource : public QObject {
  Q_OBJECT
public:
  explicit AnkiSource(Store &store, QObject *parent = nullptr,
                      QUrl endpoint = QUrl("http://127.0.0.1:8765"));
  void discover();
  void importDecks(const QString &profile, const QStringList &decks);
  bool busy() const { return running; }
  QString error;
  QJsonObject metadata() const;
  QVector<Subject> subjects(const QString &profile,
                            const QStringList &decks) const;
  Evidence evidence(const QVector<Subject> &pool) const;
signals:
  void decksFound(QString profile, QStringList decks);
  void progress(QString message);
  void finished(bool ok, QString message);

private:
  Store &store;
  AnkiClient client;
  bool running = false;
  QString profile, mediaDirectory;
  QStringList decks;
  QJsonArray ids, cards;
  QJsonObject reviews;
  int cursor = 0;
  void nextBatch();
  void commit();
  void finish(bool ok, const QString &message);
};
