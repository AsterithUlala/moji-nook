#pragma once
#include "anki.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QtWidgets>

class AnkiPanel : public QGroupBox {
  Q_OBJECT
public:
  AnkiPanel(AnkiSource &source, QSettings &settings, QWidget *parent = nullptr);
  void syncIfDue();
signals:
  void changed();
  void previewRequested();
  void practiceRequested();

private:
  AnkiSource &source;
  QSettings &settings;
  QLabel *status;
  QListWidget *decks;
  QPushButton *refresh, *sync;
  QString profile;
  QDateTime lastAttempt;
  bool importing = false;
  QStringList pendingDecks;
  void import();
  void updateSummary();
};

// Optional, lazy details: no Anki templates, web views, scripts or remote
// media.
class AnkiDetails : public QWidget {
  Q_OBJECT
public:
  explicit AnkiDetails(QWidget *parent = nullptr);
  void setSubject(const Subject &subject);
  void reveal();
  void stop();
  void configure(bool enabled, int volume, bool autoplay = true);
signals:
  void layoutChanged();

protected:
  void hideEvent(QHideEvent *event) override;

private:
  Subject subject;
  bool enabled = true;
  bool autoplay = true;
  int volume = 50;
  int audioEpoch = 0;
  QMediaPlayer *player = nullptr;
  QAudioOutput *output = nullptr;
  QPushButton *word, *sentence, *toggle;
  QWidget *details;
  QLabel *text, *picture, *audioStatus, *japanese, *translation;
  QWidget *pitch;
  void play(const QString &path);
  void prepareAudio();
};
