#include "ui/DicomSeriesDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QListWidget>
#include <QPushButton>
#include <QTableView>

#include <filesystem>
#include <iostream>
#include <string>
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

radmarky::io::DicomFileRecord recordAt(
    const std::size_t index,
    const std::string& uid,
    const std::string& description,
    const double z)
{
    radmarky::io::DicomFileRecord record;
    record.filePath = std::filesystem::path("study") / std::to_string(index);
    record.seriesInstanceUid = uid;
    record.seriesDescription = description;
    record.readable = true;
    record.imagePositionPatient = {{0.0, 0.0, z}};
    record.imageOrientationPatient = {{1.0, 0.0, 0.0, 0.0, 1.0, 0.0}};
    record.pixelSpacing = {{0.8, 0.7}};
    record.rows = 256;
    record.columns = 512;
    record.frameOfReferenceUid = uid + ".frame";
    record.sopInstanceUid = uid + "." + std::to_string(index);
    return record;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    std::vector<radmarky::io::DicomFileRecord> files;
    files.reserve(10003);
    for(std::size_t index = 0; index < 9000; ++index)
    {
        files.push_back(recordAt(index, "1.2.3.4", "Chest thin", index * 1.0));
    }
    for(std::size_t index = 0; index < 1000; ++index)
    {
        files.push_back(recordAt(
            9000 + index, "1.2.3.5", "Chest thick", index * 2.0));
    }

    auto brokenOne = recordAt(10000, "1.2.3.6", "Broken localizer", 0.0);
    auto brokenTwo = recordAt(10001, "1.2.3.6", "Broken localizer", 1.0);
    brokenOne.filePath = "study/nested/broken-one.dcm";
    brokenTwo.filePath = "study/nested/broken-two";
    brokenTwo.imagePositionPatient.reset();
    files.push_back(std::move(brokenOne));
    files.push_back(std::move(brokenTwo));
    radmarky::io::DicomFileRecord ignored;
    ignored.filePath = "study/readme";
    ignored.issue = "Not a readable DICOM file";
    files.push_back(std::move(ignored));

    radmarky::ui::DicomSeriesDialog dialog(files);
    auto* const table = dialog.findChild<QTableView*>(
        QStringLiteral("dicomSeriesTable"));
    auto* const ignoredFiles = dialog.findChild<QListWidget*>(
        QStringLiteral("dicomIgnoredFiles"));
    auto* const buttons = dialog.findChild<QDialogButtonBox*>();
    bool passed = expectTrue(table != nullptr, "series table exists")
        && expectTrue(ignoredFiles != nullptr, "ignored file list exists")
        && expectTrue(buttons != nullptr, "dialog buttons exist");
    if(table == nullptr || ignoredFiles == nullptr || buttons == nullptr)
    {
        return 1;
    }

    auto* const model = table->model();
    passed &= expectTrue(model->rowCount() == 3, "one row per candidate series");
    passed &= expectTrue(model->columnCount() == 7, "series summary columns");
    passed &= expectTrue(
        model->data(model->index(0, 1), Qt::DisplayRole).toString()
            == QStringLiteral("Chest thin"),
        "series description shown");
    QGuiApplication::clipboard()->clear();
    table->doubleClicked(model->index(0, 1));
    passed &= expectTrue(
        QGuiApplication::clipboard()->text() == QStringLiteral("Chest thin"),
        "double-clicking the description copies it");
    QGuiApplication::clipboard()->clear();
    table->doubleClicked(model->index(0, 2));
    passed &= expectTrue(
        QGuiApplication::clipboard()->text() == QStringLiteral("1.2.3.4"),
        "double-clicking the Series Instance UID copies it");
    passed &= expectTrue(
        model->data(model->index(0, 4), Qt::DisplayRole).toString()
            == QStringLiteral("512 × 256 × 9000"),
        "series resolution shown");
    passed &= expectTrue(
        model->data(model->index(1, 5), Qt::DisplayRole).toString()
            == QStringLiteral("2.000 mm"),
        "slice spacing shown");
    passed &= expectTrue(
        model->data(model->index(0, 6), Qt::DisplayRole).toString()
            == QStringLiteral("Consistent"),
        "consistency shown");
    const auto fileNamesIndex = model->index(2, 3);
    passed &= expectTrue(
        model->data(fileNamesIndex, Qt::DisplayRole).toString()
            == QStringLiteral("broken-one.dcm, broken-two"),
        "file names shown without paths");
    QGuiApplication::clipboard()->clear();
    table->doubleClicked(fileNamesIndex);
    passed &= expectTrue(
        QGuiApplication::clipboard()->text()
            == QStringLiteral("broken-one.dcm, broken-two"),
        "double-clicking file names copies the list");
    passed &= expectTrue(
        model->data(model->index(0, 0), Qt::CheckStateRole).toInt()
            == Qt::Checked,
        "largest consistent series proposed");
    passed &= expectTrue(
        buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "valid proposed series accepted");
    passed &= expectTrue(ignoredFiles->count() == 1, "ignored file item shown");

    model->setData(model->index(1, 0), Qt::Checked, Qt::CheckStateRole);
    passed &= expectTrue(
        model->data(model->index(0, 0), Qt::CheckStateRole).toInt()
                == Qt::Unchecked
            && model->data(model->index(1, 0), Qt::CheckStateRole).toInt()
                == Qt::Checked,
        "candidate selection is exclusive");
    passed &= expectTrue(
        dialog.selectedRecords().size() == 1000,
        "selected candidate returns only its files");
    passed &= expectTrue(
        !model->setData(
            model->index(2, 0), Qt::Checked, Qt::CheckStateRole),
        "inconsistent candidate cannot be selected");
    passed &= expectTrue(
        buttons->button(QDialogButtonBox::Ok)->isEnabled(),
        "valid selection remains importable");

    auto thirdSlice = recordAt(0, "2.3.4", "Shuffled input", 2.0);
    auto firstSlice = recordAt(1, "2.3.4", "Shuffled input", 0.0);
    auto secondSlice = recordAt(2, "2.3.4", "Shuffled input", 1.0);
    thirdSlice.filePath = "study/image-000003.dcm";
    firstSlice.filePath = "study/image-000001.dcm";
    secondSlice.filePath = "study/image-000002.dcm";
    const std::vector shuffledFiles{thirdSlice, firstSlice, secondSlice};
    radmarky::ui::DicomSeriesDialog shuffledDialog(shuffledFiles);
    auto* const shuffledTable = shuffledDialog.findChild<QTableView*>(
        QStringLiteral("dicomSeriesTable"));
    passed &= expectTrue(shuffledTable != nullptr, "shuffled series table exists");
    if(shuffledTable != nullptr)
    {
        passed &= expectTrue(
            shuffledTable->model()
                    ->data(shuffledTable->model()->index(0, 3), Qt::DisplayRole)
                    .toString()
                == QStringLiteral(
                    "image-000001.dcm, image-000002.dcm, image-000003.dcm"),
            "file names shown in geometric slice order");
    }

    std::vector<radmarky::io::DicomFileRecord> irregularSpacing{
        recordAt(20, "8.8.8", "Irregular spacing", 0.0),
        recordAt(21, "8.8.8", "Irregular spacing", 1.0),
        recordAt(22, "8.8.8", "Irregular spacing", 2.25),
        recordAt(23, "8.8.8", "Irregular spacing", 3.25),
    };
    radmarky::ui::DicomSeriesDialog overrideDialog(irregularSpacing);
    auto* const overrideTable = overrideDialog.findChild<QTableView*>(
        QStringLiteral("dicomSeriesTable"));
    auto* const overrideButtons = overrideDialog.findChild<QDialogButtonBox*>();
    passed &= expectTrue(
        overrideTable != nullptr && overrideButtons != nullptr,
        "spacing override controls exist");
    if(overrideTable != nullptr && overrideButtons != nullptr)
    {
        auto* const overrideModel = overrideTable->model();
        passed &= expectTrue(
            overrideModel->data(
                overrideModel->index(0, 6), Qt::DisplayRole).toString()
                == QStringLiteral(
                    "Warning: non-uniform spacing (override available)"),
            "spacing override warning shown");
        passed &= expectTrue(
            overrideModel->data(
                overrideModel->index(0, 0), Qt::CheckStateRole).toInt()
                    == Qt::Unchecked
                && !overrideButtons->button(QDialogButtonBox::Ok)->isEnabled(),
            "spacing override candidate is not selected by default");
        passed &= expectTrue(
            overrideModel->setData(
                overrideModel->index(0, 0), Qt::Checked, Qt::CheckStateRole)
                && overrideButtons->button(QDialogButtonBox::Ok)->isEnabled()
                && overrideDialog
                       .selectedSeriesRequiresNonUniformSpacingOverride(),
            "spacing override candidate can be selected manually");
    }

    return passed ? 0 : 1;
}
