#include "ui.h"
QVector<Subject> demoSubjects() {
  QVector<Subject> pool;
  QStringList words = {"日本|Japan|にほん",
                       "本日|Today|ほんじつ",
                       "休日|Holiday|きゅうじつ",
                       "毎日|Every day|まいにち",
                       "日本語|Japanese language|にほんご",
                       "中国語|Chinese language|ちゅうごくご",
                       "大学|University|だいがく",
                       "大学生|University student|だいがくせい"};
  int id = 1;
  for (const auto &line : words) {
    auto parts = line.split('|');
    Subject s;
    s.id = id++;
    s.stage = s.id % 9 + 1;
    s.level = 5;
    s.kind = "vocabulary";
    s.characters = parts[0];
    s.meanings = {parts[1]};
    s.readings = {parts[2]};
    pool.append(s);
  }
  return pool;
}
