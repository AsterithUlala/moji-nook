#include "core.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

QString normalizeMeaning(QString s) {
  static const QRegularExpression punctuation("[^\\p{L}\\p{N}]+");
  static const QRegularExpression article("^(to|a|an|the) ");
  s = s.normalized(QString::NormalizationForm_KC).toCaseFolded().trimmed();
  s.replace(punctuation, " ");
  s = s.simplified();
  s.remove(article);
  return s;
}

QString toKana(QString s) {
  s = s.normalized(QString::NormalizationForm_KC).toLower().trimmed();
  for (int i = 0; i < s.size(); ++i) {
    ushort u = s[i].unicode();
    if (u >= 0x30a1 && u <= 0x30f6)
      s[i] = QChar(u - 0x60);
  }
  static const QHash<QString, QString> map = [] {
    QHash<QString, QString> m;
    const QStringList rows = {
        "a:あ i:い u:う e:え o:お",
        "ka:か ki:き ku:く ke:け ko:こ ga:が gi:ぎ gu:ぐ ge:げ go:ご",
        "sa:さ shi:し si:し su:す se:せ so:そ za:ざ ji:じ zi:じ zu:ず ze:ぜ "
        "zo:ぞ",
        "ta:た chi:ち ti:ち tsu:つ tu:つ te:て to:と da:だ di:ぢ du:づ de:で "
        "do:ど",
        "na:な ni:に nu:ぬ ne:ね no:の ha:は hi:ひ fu:ふ hu:ふ he:へ ho:ほ",
        "ba:ば bi:び bu:ぶ be:べ bo:ぼ pa:ぱ pi:ぴ pu:ぷ pe:ぺ po:ぽ",
        "ma:ま mi:み mu:む me:め mo:も ya:や yu:ゆ yo:よ ra:ら ri:り ru:る "
        "re:れ ro:ろ wa:わ wo:を",
        "kya:きゃ kyu:きゅ kyo:きょ gya:ぎゃ gyu:ぎゅ gyo:ぎょ sha:しゃ "
        "shu:しゅ sho:しょ sya:しゃ syu:しゅ syo:しょ",
        "ja:じゃ ju:じゅ jo:じょ jya:じゃ jyu:じゅ jyo:じょ zya:じゃ zyu:じゅ "
        "zyo:じょ",
        "cha:ちゃ chu:ちゅ cho:ちょ tya:ちゃ tyu:ちゅ tyo:ちょ nya:にゃ "
        "nyu:にゅ nyo:にょ",
        "hya:ひゃ hyu:ひゅ hyo:ひょ bya:びゃ byu:びゅ byo:びょ pya:ぴゃ "
        "pyu:ぴゅ pyo:ぴょ",
        "mya:みゃ myu:みゅ myo:みょ rya:りゃ ryu:りゅ ryo:りょ",
        "fa:ふぁ fi:ふぃ fe:ふぇ fo:ふぉ va:ゔぁ vi:ゔぃ vu:ゔ ve:ゔぇ vo:ゔぉ",
        "she:しぇ che:ちぇ je:じぇ tsa:つぁ tsi:つぃ tse:つぇ tso:つぉ",
        "tha:てゃ thi:てぃ thu:てゅ the:てぇ tho:てょ dhi:でぃ dhu:でゅ",
        "wi:うぃ we:うぇ wha:うぁ whi:うぃ whe:うぇ who:うぉ",
        "xa:ぁ xi:ぃ xu:ぅ xe:ぇ xo:ぉ la:ぁ li:ぃ lu:ぅ le:ぇ lo:ぉ",
        "xya:ゃ xyu:ゅ xyo:ょ lya:ゃ lyu:ゅ lyo:ょ xtsu:っ ltsu:っ xtu:っ "
        "ltu:っ -:ー"};
    for (const auto &row : rows)
      for (const auto &entry : row.split(' ')) {
        const auto parts = entry.split(':');
        m.insert(parts[0], parts[1]);
      }
    return m;
  }();
  QString out;
  for (int i = 0; i < s.size();) {
    const QChar c = s[i];
    if (c == 'n') {
      if (i + 1 == s.size()) {
        out += "ん";
        ++i;
        continue;
      }
      if (s[i + 1] == '\'') {
        out += "ん";
        i += 2;
        continue;
      }
      if (s[i + 1] == 'n') {
        out += "ん";
        i += (i + 2 < s.size() && QString("aiueoy").contains(s[i + 2])) ? 1 : 2;
        continue;
      }
      if (!QString("aiueoy").contains(s[i + 1])) {
        out += "ん";
        ++i;
        continue;
      }
    }
    if (i + 1 < s.size() && c == s[i + 1] &&
        QString("bcdfghjklmpqrstvwxyz").contains(c)) {
      out += "っ";
      ++i;
      continue;
    }
    if (s.mid(i, 3) == "tch") {
      out += "っ";
      ++i;
      continue;
    }
    bool matched = false;
    for (int n = qMin(4, int(s.size()) - i); n > 0; --n) {
      auto it = map.constFind(s.mid(i, n));
      if (it != map.cend()) {
        out += it.value();
        i += n;
        matched = true;
        break;
      }
    }
    if (!matched) {
      out += c;
      ++i;
    }
  }
  return out;
}

double stageWeight(int s) {
  return s < 1 || s > 9 ? 0
         : s <= 4       ? 4
         : s <= 6       ? 3
         : s == 7       ? 2
         : s == 8       ? 1
                        : .5;
}
QString stageName(int s) {
  return s <= 4   ? "Apprentice"
         : s <= 6 ? "Guru"
         : s == 7 ? "Master"
         : s == 8 ? "Enlightened"
                  : "Burned";
}
QString modeName(Mode m) {
  return m == Mode::Recall   ? "Recall"
         : m == Mode::Typing ? "Typed reading"
                             : "Four choices";
}

QVector<Subject> parseSubjects(const QJsonArray &subjects,
                               const QJsonArray &assignments,
                               const QJsonArray &materials, int maxLevel) {
  QHash<int, int> stages;
  QHash<int, QDateTime> started;
  for (const auto &v : assignments) {
    auto d = v.toObject()["data"].toObject();
    int stage = d["srs_stage"].toInt();
    started[d["subject_id"].toInt()] =
        QDateTime::fromString(d["started_at"].toString(), Qt::ISODateWithMs);
    if (!d["started_at"].toString().isEmpty() && !d["hidden"].toBool() &&
        stageWeight(stage) > 0)
      stages[d["subject_id"].toInt()] = stage;
  }
  QHash<int, QStringList> synonyms;
  for (const auto &v : materials) {
    auto d = v.toObject()["data"].toObject();
    for (const auto &s : d["meaning_synonyms"].toArray())
      synonyms[d["subject_id"].toInt()].append(s.toString());
  }
  QVector<Subject> result;
  QHash<QString, QHash<QString, QString>> kanjiReadings;
  for (const auto &v : subjects) {
    const auto root = v.toObject();
    if (root["object"] != "kanji")
      continue;
    const auto data = root["data"].toObject();
    for (const auto &r : data["readings"].toArray()) {
      const auto reading = r.toObject();
      kanjiReadings[data["characters"].toString()]
                   [toKana(reading["reading"].toString())] =
                       reading["type"].toString();
    }
  }
  for (const auto &v : subjects) {
    auto root = v.toObject();
    auto d = root["data"].toObject();
    Subject s;
    s.id = root["id"].toInt();
    s.kind = root["object"].toString();
    if (!stages.contains(s.id) ||
        !QStringList{"kanji", "vocabulary", "kana_vocabulary"}.contains(
            s.kind) ||
        !d["hidden_at"].toString().isEmpty() || d["level"].toInt() > maxLevel)
      continue;
    s.stage = stages[s.id];
    s.startedAt = started.value(s.id);
    s.characters = d["characters"].toString();
    s.kanjiReadingTypes = kanjiReadings.value(s.characters);
    s.level = d["level"].toInt();
    s.url = d["document_url"].toString();
    for (const auto &m : d["meanings"].toArray()) {
      auto a = m.toObject();
      if (a["accepted_answer"].toBool()) {
        if (a["primary"].toBool())
          s.meanings.prepend(a["meaning"].toString());
        else
          s.meanings.append(a["meaning"].toString());
      }
    }
    for (const auto &m : d["auxiliary_meanings"].toArray()) {
      auto a = m.toObject();
      if (a["type"] == "whitelist")
        s.meanings.append(a["meaning"].toString());
    }
    s.meanings.append(synonyms[s.id]);
    s.meanings.removeDuplicates();
    for (const auto &r : d["readings"].toArray()) {
      auto a = r.toObject();
      if (a["accepted_answer"].toBool()) {
        if (a["primary"].toBool()) {
          s.readings.prepend(a["reading"].toString());
          s.readingKind = a["type"].toString();
        } else
          s.readings.append(a["reading"].toString());
      }
    }
    for (const auto &c : d["component_subject_ids"].toArray())
      s.components.append(c.toInt());
    for (const auto &c : d["visually_similar_subject_ids"].toArray())
      s.similar.append(c.toInt());
    auto contexts = d["context_sentences"].toArray();
    if (!contexts.isEmpty()) {
      auto c = contexts[0].toObject();
      s.contextJa = c["ja"].toString();
      s.contextEn = c["en"].toString();
    }
    if (!s.characters.isEmpty() && !s.meanings.isEmpty())
      result.append(s);
  }
  return result;
}

bool Challenge::accepts(const QString &text) const {
  if (text.trimmed().isEmpty())
    return false;
  for (const auto &a : reading ? subject.readings : subject.meanings)
    if (reading ? toKana(text) == toKana(a)
                : normalizeMeaning(text) == normalizeMeaning(a))
      return true;
  return false;
}

QString Challenge::readingRetryHint(const QString &text) const {
  if (!reading || mode != Mode::Typing || accepts(text) ||
      (subject.kind != "kanji" && subject.kind != "vocabulary"))
    return {};
  const auto supplied = subject.kanjiReadingTypes.value(toKana(text));
  if (supplied.isEmpty())
    return {};
  QString requested = subject.readingKind;
  if (subject.kind == "vocabulary") {
    QSet<QString> types;
    for (const auto &r : subject.readings)
      types.insert(subject.kanjiReadingTypes.value(toKana(r)));
    requested = types.size() == 1 ? *types.begin() : QString();
  }
  auto name = [](const QString &kind) {
    return kind == "onyomi"    ? QString("on’yomi")
           : kind == "kunyomi" ? QString("kun’yomi")
           : kind == "nanori"  ? QString("nanori")
                               : QString();
  };
  const QString target = name(requested);
  const QString source = name(supplied);
  if (source.isEmpty())
    return {};
  if (subject.kind == "kanji") {
    if (target.isEmpty() || requested == supplied)
      return {};
    return QString("Use the %1 reading. No miss recorded.").arg(target);
  }
  return QString("Use %1. No miss recorded.")
      .arg(target.isEmpty() ? "the vocabulary reading"
                                    : "the word’s " + target + " reading");
}
