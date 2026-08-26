#include "ui/DicomHeaderDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTableView>

#include <iostream>
#include <string_view>
#include <vector>

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

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    const std::vector<radmarky::core::Volume::DicomMetadataEntry> metadata{
        {"(0008,0060) Modality", "CT"},
        {"(0010,0010) Patient's Name", "Alice Smith"},
        {"(0019,1001)", "A private value that remains inspectable"},
        {"(0020,4000) Image Comments", ""},
    };
    radmarky::ui::DicomHeaderDialog dialog(metadata);

    auto* const search = dialog.findChild<QLineEdit*>(
        QStringLiteral("dicomMetadataSearch"));
    auto* const table = dialog.findChild<QTableView*>(
        QStringLiteral("dicomMetadataTable"));
    auto* const results = dialog.findChild<QStackedWidget*>(
        QStringLiteral("dicomMetadataResults"));
    bool passed = expectTrue(dialog.isModal(), "dialog modality")
        && expectTrue(search != nullptr, "search field")
        && expectTrue(table != nullptr, "metadata table")
        && expectTrue(results != nullptr, "results stack");
    if(!passed)
    {
        return 1;
    }
    dialog.show();
    application.processEvents();

    auto* const model = qobject_cast<QSortFilterProxyModel*>(table->model());
    passed &= expectTrue(model != nullptr, "filter model");
    passed &= expectTrue(table->alternatingRowColors(), "alternating rows");
    passed &= expectTrue(search->geometry().top() < results->geometry().top(), "search above table");
    passed &= expectTrue(model->rowCount() == 4, "all metadata rows");
    passed &= expectTrue(
        model->headerData(0, Qt::Horizontal).toString() == QStringLiteral("Key"),
        "key header");
    passed &= expectTrue(
        model->headerData(1, Qt::Horizontal).toString() == QStringLiteral("Value"),
        "value header");
    passed &= expectTrue(
        !(model->flags(model->index(0, 0)) & Qt::ItemIsEditable),
        "read-only metadata");

    table->doubleClicked(model->index(0, 0));
    passed &= expectTrue(
        QApplication::clipboard()->text() == QStringLiteral("(0008,0060) Modality"),
        "double-click copies key");
    table->doubleClicked(model->index(0, 1));
    passed &= expectTrue(
        QApplication::clipboard()->text() == QStringLiteral("CT"),
        "double-click copies value");

    search->setText(QStringLiteral("MODALITY"));
    passed &= expectTrue(model->rowCount() == 1, "case-insensitive key filter");
    search->setText(QStringLiteral("smith"));
    passed &= expectTrue(model->rowCount() == 1, "value filter");
    search->setText(QStringLiteral("private value"));
    passed &= expectTrue(model->rowCount() == 1, "private value filter");
    search->setText(QStringLiteral("not present"));
    passed &= expectTrue(model->rowCount() == 0, "no matching rows");
    passed &= expectTrue(
        results->currentWidget()->objectName()
            == QStringLiteral("dicomMetadataEmptyState"),
        "no-results state");
    search->clear();
    passed &= expectTrue(model->rowCount() == 4, "cleared filter");
    passed &= expectTrue(results->currentWidget() == table, "table restored");

    QKeyEvent escapePress(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&dialog, &escapePress);
    application.processEvents();
    passed &= expectTrue(!dialog.isVisible(), "Escape closes dialog");

    return passed ? 0 : 1;
}
