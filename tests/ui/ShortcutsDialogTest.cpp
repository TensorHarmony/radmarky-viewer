#include "ui/ShortcutsDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QTableWidget>

#include <iostream>
#include <string_view>

namespace
{

bool expectTrue(const bool condition, const std::string_view field)
{
    if(condition)
    {
        return true;
    }
    std::cerr << field << ": expected true\n";
    return false;
}

bool tableContains(
    const QTableWidget& table,
    const QString& input,
    const QString& actionText)
{
    for(int row = 0; row < table.rowCount(); ++row)
    {
        const auto* const inputItem = table.item(row, 1);
        const auto* const actionItem = table.item(row, 3);
        if(inputItem != nullptr && actionItem != nullptr
           && inputItem->text() == input
           && actionItem->text().contains(actionText, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    radmarky::ui::ShortcutsDialog dialog;

    const auto* const table =
        dialog.findChild<QTableWidget*>(QStringLiteral("shortcutsTable"));
    const auto* const buttons =
        dialog.findChild<QDialogButtonBox*>(QStringLiteral("shortcutsButtons"));

    bool passed = expectTrue(
        dialog.objectName() == QStringLiteral("shortcutsDialog"),
        "dialog object name");
    passed &= expectTrue(dialog.isModal(), "dialog modality");
    passed &= expectTrue(table != nullptr, "shortcuts table");
    passed &= expectTrue(buttons != nullptr, "close buttons");
    if(table == nullptr || buttons == nullptr)
    {
        return 1;
    }

    passed &= expectTrue(table->columnCount() == 4, "table columns");
    passed &= expectTrue(table->rowCount() >= 20, "table rows");
    passed &= expectTrue(
        tableContains(*table, QStringLiteral("H"), QStringLiteral("overlays")),
        "annotation visibility shortcut");
    passed &= expectTrue(
        tableContains(*table, QStringLiteral("W / A / S / D"),
                      QStringLiteral("Pan")),
        "keyboard pan shortcut");
    passed &= expectTrue(
        tableContains(*table, QStringLiteral("F"), QStringLiteral("fit")),
        "fit shortcut");
    passed &= expectTrue(
        tableContains(*table, QStringLiteral("Wheel"), QStringLiteral("slices")),
        "mouse wheel shortcut");
    passed &= expectTrue(
        tableContains(*table, QStringLiteral("Middle-drag or Alt+left-drag"),
                      QStringLiteral("Pan")),
        "mouse pan shortcut");
    passed &= expectTrue(
        buttons->standardButtons() == QDialogButtonBox::Close,
        "close button");

    return passed ? 0 : 1;
}
