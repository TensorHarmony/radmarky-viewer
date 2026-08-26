#pragma once

#include <QColor>
#include <QCursor>
#include <QPixmap>
#include <QSize>
#include <QString>

class QIcon;

namespace radmarky::ui
{

enum class UiTheme
{
    Light,
    Dark,
};

QString applicationStyleSheet(UiTheme theme);
QPixmap svgPixmap(const QString& resourcePath, const QSize& size);
QIcon svgIcon(const QString& resourcePath);
QCursor svgCursor(
    const QString& resourcePath,
    const QSize& logicalSize = QSize(32, 32),
    const QColor& tint = QColor());
QCursor crosshairCursor(
    const QColor& tint = QColor(),
    const QSize& logicalSize = QSize(32, 32));

} // namespace radmarky::ui
