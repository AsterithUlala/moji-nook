#pragma once
#include "core.h"
#include <QtNetwork>

QJsonArray mergeSyncRows(const QJsonArray &cached, const QJsonArray &updates);
qint64 syncRetrySeconds(const QByteArray &retryAfter, const QByteArray &reset,
                        qint64 now);

class Sync : public QObject {
  Q_OBJECT
public:
  explicit Sync(Store &store, QObject *parent = nullptr,
                QNetworkAccessManager *transport = nullptr);
  void start(const QString &token);
  bool busy() const { return running; }
signals:
  void progress(QString message);
  void finished(bool ok, QString message);

private:
  friend class SyncTest;
  Store &store;
  QNetworkAccessManager network;
  QNetworkAccessManager *transport;
  QJsonObject pending, previous, cursors;
  QString token, section;
  QString sectionCursor, fingerprint;
  QStringList sections;
  QJsonArray rows;
  QSet<QString> visited;
  bool running = false;
  int retries = 0;
  int requestDelay = 1100, requests = 0, received = 0;
  qint64 receivedBytes = 0, notBefore = 0;
  bool incremental = false, reconcile = false, firstPage = true;
  void schedule(const QUrl &url);
  void fetch(const QUrl &url);
  void nextSection();
  void fail(const QString &message);
};
