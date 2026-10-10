#include "ui.h"
#include "ui_helpers.h"
#include "voicevox_files.h"

void App::buildSpeechSettings(QVBoxLayout *layout) {
  auto *group = new QWidget;
  group->setObjectName("speechSettings");
  auto *form = new QVBoxLayout(group);
  form->setContentsMargins(0, 16, 0, 0);
  form->setSpacing(12);
  form->addWidget(label("Japanese voice", "controlHeading"));
  auto *enabled = new SettingsCheckBox("Local Japanese pronunciation");
  enabled->setObjectName("speechEnabled");
  enabled->setChecked(settings.value("speech/enabled", true).toBool());
  form->addWidget(enabled);
  auto *controls = new QWidget;
  auto *body = new QVBoxLayout(controls);
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(8);
  form->addWidget(controls);
  auto *engine = new ScrollSafeComboBox;
  engine->setObjectName("speechEngine");
  engine->setAccessibleName("Japanese speech performance and quality");
  engine->addItem("Fast · lightweight voices", "fast");
  engine->addItem("Quality · neural voices", "quality");
  if (!speech.available("fast") && speech.available("quality"))
    engine->removeItem(engine->findData("fast"));
  const auto defaultEngine = speech.available("quality") ? "quality" : "fast";
  engine->setCurrentIndex(qMax(0, engine->findData(
      settings.value("speech/engine", defaultEngine).toString())));
  auto *engineLabel = label("Speech mode", "muted");
  engineLabel->setBuddy(engine);
  body->addWidget(engineLabel);
  body->addWidget(engine);
  auto *modeNote = label({}, "muted");
  modeNote->setObjectName("speechModeNote");
  body->addWidget(modeNote);
  // Preserve a legacy pinned Fast speaker while adding a separate Quality bank.
  if (!settings.contains("speech/voice_fast") &&
      JapaneseSpeech::voices("fast").contains(settings.value("speech/voice").toString()))
    settings.setValue("speech/voice_fast", settings.value("speech/voice"));
  auto *row = new QHBoxLayout;
  auto *voice = new ScrollSafeComboBox;
  voice->setObjectName("speechVoice");
  voice->setAccessibleName("Japanese pronunciation voice");
  auto populateVoices = [this, voice, engine] {
    const QSignalBlocker blocker(voice);
    const auto bank = engine->currentData().toString();
    voice->clear();
    voice->addItem(QString("Random · vary between %1 voices")
                       .arg(JapaneseSpeech::voices(bank).size()), "random");
    for (const auto &id : JapaneseSpeech::voices(bank))
      voice->addItem(JapaneseSpeech::voiceName(id), id);
    voice->setCurrentIndex(qMax(0, voice->findData(settings.value(
        "speech/voice_" + bank, "random").toString())));
  };
  populateVoices();
  auto *voiceLabel = label("Voice", "muted");
  voiceLabel->setBuddy(voice);
  body->addWidget(voiceLabel);
  row->addWidget(voice, 1);
  auto *tryVoice = button("Try voice", "control");
  tryVoice->setObjectName("speechPreview");
  row->addWidget(tryVoice);
  body->addLayout(row);
  body->addSpacing(12);
  body->addWidget(label("Playback", "controlHeading"));
  auto *autoplay = new SettingsCheckBox("Play after revealing an answer");
  autoplay->setObjectName("speechAutoplay");
  autoplay->setChecked(settings.value("speech/autoplay", true).toBool());
  body->addWidget(autoplay);
  auto slider = [&](const QString &name, const QString &caption, int low, int high,
                    int value, const QString &suffix, double scale) {
    auto *control = new ScrollSafeSlider;
    control->setObjectName(name);
    control->setAccessibleName(caption);
    control->setRange(low, high);
    control->setSingleStep(5);
    control->setValue(qBound(low, value, high));
    auto *text = label({}, "muted");
    text->setBuddy(control);
    auto update = [text, control, caption, suffix, scale] {
      text->setText(caption + " · " +
          QString::number(control->value() * scale, 'g', 3) + suffix);
    };
    connect(control, &QSlider::valueChanged, group, update);
    update();
    body->addWidget(text);
    body->addWidget(control);
    return control;
  };
  auto *volume = slider("speechVolume", "Voice volume", 0, 100,
      settings.value("speech/volume", 35).toInt(), "%", 1.0);
  auto *rate = slider("speechRate", "Speaking speed", 75, 125,
      settings.value("speech/rate", 100).toInt(), "×", .01);
  body->addSpacing(12);
  body->addWidget(label("Anki recordings", "controlHeading"));
  auto *recorded = new SettingsCheckBox("Use deck recordings when available");
  recorded->setObjectName("speechPreferRecording");
  recorded->setChecked(settings.value("speech/prefer_recorded", false).toBool());
  recorded->setToolTip("Original Anki recordings use their recorded voice. "
                      "Local voices handle cards without a recording.");
  body->addWidget(recorded);
  body->addWidget(label("Uses the local voice when a recording is unavailable.", "muted"));
  QStringList voiceCredits;
  for (const auto &quality : qualityVoices)
    voiceCredits << QString::fromUtf8(quality.credit);
  auto *credits = label(voiceCredits.join(" · "), "muted");
  credits->setObjectName("speechVoiceCredits");
  credits->setToolTip("Quality voice credits. Full terms and notices are included in the local speech bundle.");
  body->addWidget(credits);
  auto *state = label({}, "muted");
  state->setObjectName("speechAvailability");
  form->addWidget(state);
  auto updateAvailability = [this, state, engine] {
    const auto bank = engine->currentData().toString();
    state->setText(speech.available(bank)
        ? QString("Available offline · %1 Japanese voices").arg(JapaneseSpeech::voices(bank).size())
                                    : "Japanese voice files are unavailable");
  };
  updateAvailability();
  connect(&speech, &JapaneseSpeech::readyChanged, group, updateAvailability);
  connect(&speech, &JapaneseSpeech::error, state,
          [state](const QString &message) { state->setText(message); });
  auto updateDescription = [engine, modeNote, credits] {
    const bool quality = engine->currentData() == "quality";
    modeNote->setText(quality
        ? "Smoother speech · Slower first playback · Offline"
        : "Faster speech · Lower memory use · Offline");
    credits->setVisible(quality);
  };
  auto update = [this, enabled, engine, voice, autoplay, volume, rate, recorded, controls,
                 tryVoice, updateDescription, updateAvailability] {
    settings.setValue("speech/enabled", enabled->isChecked());
    settings.setValue("speech/engine", engine->currentData());
    settings.setValue("speech/voice", voice->currentData());
    settings.setValue("speech/voice_" + engine->currentData().toString(), voice->currentData());
    settings.setValue("speech/autoplay", autoplay->isChecked());
    settings.setValue("speech/volume", volume->value());
    settings.setValue("speech/rate", rate->value());
    settings.setValue("speech/prefer_recorded", recorded->isChecked());
    controls->setVisible(enabled->isChecked());
    tryVoice->setEnabled(enabled->isChecked() && volume->value() > 0);
    applySpeechSettings();
    updateDescription();
    updateAvailability();
  };
  connect(engine, &QComboBox::currentIndexChanged, this,
          [populateVoices, update] { populateVoices(); update(); });
  connect(enabled, &QCheckBox::toggled, this, update);
  connect(voice, &QComboBox::currentIndexChanged, this, update);
  connect(autoplay, &QCheckBox::toggled, this, update);
  connect(volume, &QSlider::valueChanged, this, update);
  connect(rate, &QSlider::valueChanged, this, update);
  connect(recorded, &QCheckBox::toggled, this, update);
  connect(tryVoice, &QPushButton::clicked, this, [this] {
    speech.say("にほんごをべんきょうしています。",
               QUuid::createUuid().toString(QUuid::WithoutBraces));
  });
  // Switching a voice is itself an audition; slider changes stay quiet.
  connect(voice, &QComboBox::activated, tryVoice, &QPushButton::click);
  connect(engine, &QComboBox::activated, tryVoice, &QPushButton::click);
  updateDescription();
  controls->setVisible(enabled->isChecked());
  tryVoice->setEnabled(enabled->isChecked() && volume->value() > 0);
  layout->addWidget(group);
}

void App::applySpeechSettings() {
  const bool enabled = settings.value("speech/enabled", true).toBool();
  const int volume = qBound(0, settings.value("speech/volume", 35).toInt(), 100);
  const bool autoplay = settings.value("speech/autoplay", true).toBool();
  const bool recorded = settings.value("speech/prefer_recorded", false).toBool();
  const auto defaultEngine = speech.available("quality") ? "quality" : "fast";
  auto engine = settings.value("speech/engine", defaultEngine).toString();
  // The settings page shows the other engine when the saved one is missing.
  const auto other = engine == "fast" ? "quality" : "fast";
  if (!speech.available(engine) && speech.available(other))
    engine = other;
  const auto voice = settings.value("speech/voice_" + engine,
      engine == "fast" ? settings.value("speech/voice", "random") : QVariant("random")).toString();
  speech.configure(enabled, voice, volume,
      settings.value("speech/rate", 100).toInt() / 100.0, engine);
  card.setSpeech(&speech, enabled && volume > 0, autoplay, recorded);
  previewCard.setSpeech(&speech, enabled && volume > 0, autoplay, recorded);
  card.setAnkiAudio(enabled && recorded, volume, autoplay);
  previewCard.setAnkiAudio(enabled && recorded, volume, autoplay);
}
