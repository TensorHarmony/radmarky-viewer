#include "ui/DicomSeriesDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
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
    passed &= expectTrue(model->columnCount() == 8, "series summary columns");
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
        model->data(model->index(0, 7), Qt::DisplayRole).toString()
            == QStringLiteral("Consistent"),
        "consistency shown");
    passed &= expectTrue(
        model->headerData(6, Qt::Horizontal, Qt::DisplayRole).toString()
                == QStringLiteral("Error code")
            && model->data(model->index(0, 6), Qt::DisplayRole).toString()
                == QStringLiteral("—")
            && model->data(model->index(2, 6), Qt::DisplayRole).toString()
                == QStringLiteral("DICOM_GEOMETRY_MISSING_SPATIAL_METADATA"),
        "stable geometry error codes shown in their own column");
    dialog.show();
    application.processEvents();
    passed &= expectTrue(
        table->horizontalScrollMode() == QAbstractItemView::ScrollPerPixel
            && table->horizontalScrollBar()->maximum() > 0
            && table->horizontalHeader()->sectionResizeMode(3)
                == QHeaderView::Interactive
            && table->horizontalHeader()->sectionResizeMode(7)
                == QHeaderView::Interactive,
        "wide series table scrolls horizontally to hidden content");
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
        "different-series selection is exclusive");
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

    std::vector<radmarky::io::DicomFileRecord> overlappingParts;
    for(std::size_t index = 0; index < 4; ++index)
    {
        auto record = recordAt(index, "4.4.4", "Overlapping acquisition", index);
        record.acquisitionNumber = 1;
        overlappingParts.push_back(std::move(record));
    }
    for(std::size_t index = 0; index < 3; ++index)
    {
        auto record = recordAt(
            4 + index, "4.4.4", "Overlapping acquisition", 1.01 + index);
        record.acquisitionNumber = 2;
        overlappingParts.push_back(std::move(record));
    }
    radmarky::ui::DicomSeriesDialog partsDialog(overlappingParts);
    auto* const partsTable = partsDialog.findChild<QTableView*>(
        QStringLiteral("dicomSeriesTable"));
    auto* const partsButtons = partsDialog.findChild<QDialogButtonBox*>();
    passed &= expectTrue(
        partsTable != nullptr && partsButtons != nullptr,
        "multi-part selection controls exist");
    if(partsTable != nullptr && partsButtons != nullptr)
    {
        auto* const partsModel = partsTable->model();
        passed &= expectTrue(partsModel->rowCount() == 2, "shared UID split into parts");
        passed &= expectTrue(
            partsModel->setData(
                partsModel->index(1, 0), Qt::Checked, Qt::CheckStateRole)
                && partsModel->data(
                       partsModel->index(0, 0), Qt::CheckStateRole).toInt()
                    == Qt::Checked
                && partsModel->data(
                       partsModel->index(1, 0), Qt::CheckStateRole).toInt()
                    == Qt::Checked,
            "parts of one Series Instance UID can be selected together");
        passed &= expectTrue(
            partsDialog.selectedRecords().size() == overlappingParts.size()
                && partsDialog.selectedFilePaths().size() == overlappingParts.size(),
            "multi-part selection returns every source file");
        passed &= expectTrue(
            partsDialog.selectedSeriesRequiresMissingSlicesOverride()
                && partsDialog.selectedSeriesRequiresSliceSpacingOverride()
                && partsButtons->button(QDialogButtonBox::Ok)->isEnabled(),
            "combined parts use the explicit spacing override path");
        passed &= expectTrue(
            partsModel->setData(
                partsModel->index(1, 0), Qt::Unchecked, Qt::CheckStateRole)
                && partsDialog.selectedRecords().size() == 4
                && !partsDialog.selectedSeriesRequiresSliceSpacingOverride(),
            "an individual part can be deselected");
    }

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
                overrideModel->index(0, 7), Qt::DisplayRole).toString()
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

    std::vector<radmarky::io::DicomFileRecord> metadataMismatch{
        recordAt(30, "9.9.9", "Conflicting spacing metadata", 0.0),
        recordAt(31, "9.9.9", "Conflicting spacing metadata", 1.0),
        recordAt(32, "9.9.9", "Conflicting spacing metadata", 2.0),
    };
    for(auto& record : metadataMismatch)
    {
        record.spacingBetweenSlices = 2.0;
    }
    radmarky::ui::DicomSeriesDialog metadataOverrideDialog(metadataMismatch);
    auto* const metadataOverrideTable =
        metadataOverrideDialog.findChild<QTableView*>(
            QStringLiteral("dicomSeriesTable"));
    auto* const metadataOverrideButtons =
        metadataOverrideDialog.findChild<QDialogButtonBox*>();
    passed &= expectTrue(
        metadataOverrideTable != nullptr && metadataOverrideButtons != nullptr,
        "spacing metadata override controls exist");
    if(metadataOverrideTable != nullptr && metadataOverrideButtons != nullptr)
    {
        auto* const metadataOverrideModel = metadataOverrideTable->model();
        passed &= expectTrue(
            metadataOverrideModel
                    ->data(
                        metadataOverrideModel->index(0, 7), Qt::DisplayRole)
                    .toString()
                == QStringLiteral(
                    "Warning: declared spacing disagrees with slice positions "
                    "(override available)"),
            "spacing metadata override warning shown");
        passed &= expectTrue(
            metadataOverrideModel
                    ->data(
                        metadataOverrideModel->index(0, 0), Qt::CheckStateRole)
                    .toInt()
                    == Qt::Unchecked
                && !metadataOverrideButtons->button(QDialogButtonBox::Ok)
                        ->isEnabled(),
            "spacing metadata override candidate is not selected by default");
        passed &= expectTrue(
            metadataOverrideModel->setData(
                metadataOverrideModel->index(0, 0),
                Qt::Checked,
                Qt::CheckStateRole)
                && metadataOverrideButtons->button(QDialogButtonBox::Ok)
                       ->isEnabled()
                && metadataOverrideDialog
                       .selectedSeriesRequiresSpacingMetadataMismatchOverride()
                && metadataOverrideDialog
                       .selectedSeriesRequiresSliceSpacingOverride(),
            "spacing metadata disagreement can be selected manually");
    }

    std::vector<radmarky::io::DicomFileRecord> missingSlices{
        recordAt(40, "10.10.10", "Missing slice", 0.0),
        recordAt(41, "10.10.10", "Missing slice", 1.0),
        recordAt(42, "10.10.10", "Missing slice", 3.0),
        recordAt(43, "10.10.10", "Missing slice", 4.0),
    };
    radmarky::ui::DicomSeriesDialog missingSlicesDialog(missingSlices);
    auto* const missingSlicesTable = missingSlicesDialog.findChild<QTableView*>(
        QStringLiteral("dicomSeriesTable"));
    auto* const missingSlicesButtons =
        missingSlicesDialog.findChild<QDialogButtonBox*>();
    passed &= expectTrue(
        missingSlicesTable != nullptr && missingSlicesButtons != nullptr,
        "missing slice review controls exist");
    if(missingSlicesTable != nullptr && missingSlicesButtons != nullptr)
    {
        auto* const missingSlicesModel = missingSlicesTable->model();
        passed &= expectTrue(
            missingSlicesModel
                    ->data(missingSlicesModel->index(0, 6), Qt::DisplayRole)
                    .toString()
                    == QStringLiteral("DICOM_GEOMETRY_MISSING_SLICES")
                && missingSlicesModel
                       ->data(missingSlicesModel->index(0, 7), Qt::DisplayRole)
                       .toString()
                    == QStringLiteral(
                        "Warning: one or more slices appear to be missing "
                        "(override available)"),
            "missing slice error code and override warning shown");
        passed &= expectTrue(
            missingSlicesModel
                        ->data(
                            missingSlicesModel->index(0, 0), Qt::CheckStateRole)
                        .toInt()
                    == Qt::Unchecked
                && !missingSlicesButtons->button(QDialogButtonBox::Ok)
                        ->isEnabled(),
            "missing slice candidate opens review without default selection");
        passed &= expectTrue(
            missingSlicesModel->setData(
                missingSlicesModel->index(0, 0),
                Qt::Checked,
                Qt::CheckStateRole)
                && missingSlicesButtons->button(QDialogButtonBox::Ok)->isEnabled()
                && missingSlicesDialog.selectedSeriesRequiresMissingSlicesOverride()
                && missingSlicesDialog.selectedSeriesRequiresSliceSpacingOverride(),
            "missing slice candidate can be selected for explicit override");
    }

    return passed ? 0 : 1;
}
