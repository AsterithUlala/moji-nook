#pragma once
#include <QtWidgets>

QList<QScreen *> orderedDisplays();
QString displayLabel(const QScreen *screen, int number);
void showDisplayIdentifiers(int theme);
