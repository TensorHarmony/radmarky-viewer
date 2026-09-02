#include "ui/DicomSeriesDialog.h"

#include "io/DicomGeometry.h"

#include <QAbstractTableModel>
#include <QAbstractItemView>
#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace radmarky::ui
{
namespace
{

QString pathToQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    const auto utf8 = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(utf8.data()),
        static_cast<qsizetype>(utf8.size()));
#endif
}

QString joinedIssues(const io::DicomSeriesCandidate& candidate)
{
    QStringList issues;
    issues.reserve(static_cast<qsizetype>(candidate.consistencyIssues.size()));
    for(const auto& issue : candidate.consistencyIssues)
    {
        issues.push_back(QString::fromStdString(issue));
    }
    return issues.join(QStringLiteral("; "));
}

QString joinedIssueCodes(const io::DicomSeriesCandidate& candidate)
{
    QStringList codes;
    codes.reserve(static_cast<qsizetype>(candidate.consistencyIssueCodes.size()));
    for(const auto& code : candidate.consistencyIssueCodes)
    {
        codes.push_back(QString::fromStdString(code));
    }
    return codes.empty() ? QStringLiteral("—")
                         : codes.join(QStringLiteral("; "));
}

QString candidateDescription(const io::DicomSeriesCandidate& candidate)
{
    QString description = candidate.seriesDescription.empty()
        ? QObject::tr("<no description>")
        : QString::fromStdString(candidate.seriesDescription);
    if(candidate.partCount > 1)
    {
        description += QObject::tr(" (part %1 of %2)")
                           .arg(static_cast<qulonglong>(candidate.partNumber))
                           .arg(static_cast<qulonglong>(candidate.partCount));
    }
    return description;
}

} // namespace

class DicomSeriesTableModel final : public QAbstractTableModel
{
public:
    DicomSeriesTableModel(
        const io::DicomSeriesAnalysis& analysis,
        const std::vector<io::DicomFileRecord>& files,
        QObject* const parent)
        : QAbstractTableModel(parent)
        , analysis_(analysis)
        , selectedSeries_(analysis_.series.size(), false)
    {
        if(analysis_.proposedSeriesIndex
           && *analysis_.proposedSeriesIndex < selectedSeries_.size())
        {
            selectedSeries_[*analysis_.proposedSeriesIndex] = true;
        }
        fileNames_.reserve(analysis_.series.size());
        for(const auto& candidate : analysis_.series)
        {
            QStringList names;
            names.reserve(static_cast<qsizetype>(candidate.recordIndices.size()));
            for(const auto recordIndex : candidate.recordIndices)
            {
                if(recordIndex < files.size())
                {
                    names.push_back(pathToQString(files[recordIndex].filePath.filename()));
                }
            }
            fileNames_.push_back(names.join(QStringLiteral(", ")));
        }
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(analysis_.series.size());
    }

    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 8;
    }

    [[nodiscard]] QVariant data(
        const QModelIndex& index, const int role) const override
    {
        if(!index.isValid() || index.row() < 0
           || static_cast<std::size_t>(index.row()) >= analysis_.series.size())
        {
            return {};
        }
        const auto row = static_cast<std::size_t>(index.row());
        const auto& candidate = analysis_.series[row];
        if(role == Qt::CheckStateRole && index.column() == 0)
        {
            return selectedSeries_[row] ? Qt::Checked : Qt::Unchecked;
        }
        if(role == Qt::DisplayRole)
        {
            switch(index.column())
            {
            case 0:
                return QString{};
            case 1:
                return candidateDescription(candidate);
            case 2:
                return QString::fromStdString(candidate.seriesInstanceUid);
            case 3:
                return fileNames_[row];
            case 4:
                if(candidate.columns && candidate.rows)
                {
                    return tr("%1 × %2 × %3")
                        .arg(static_cast<qulonglong>(*candidate.columns))
                        .arg(static_cast<qulonglong>(*candidate.rows))
                        .arg(static_cast<qulonglong>(candidate.sliceCount));
                }
                return tr("Unknown");
            case 5:
                return candidate.sliceSpacingMillimetres
                    ? tr("%1 mm").arg(*candidate.sliceSpacingMillimetres, 0, 'f', 3)
                    : tr("Unknown");
            case 6:
                return joinedIssueCodes(candidate);
            case 7:
                if(candidate.missingSlicesOverrideAllowed)
                {
                    return tr("Warning: one or more slices appear to be missing "
                              "(override available)");
                }
                if(candidate.nonUniformSpacingOverrideAllowed)
                {
                    return tr("Warning: non-uniform spacing (override available)");
                }
                if(candidate.spacingMetadataMismatchOverrideAllowed)
                {
                    return tr(
                        "Warning: declared spacing disagrees with slice positions "
                        "(override available)");
                }
                return candidate.consistent()
                    ? (candidate.gantryTilt ? tr("Consistent (gantry tilt)")
                                            : tr("Consistent"))
                    : joinedIssues(candidate);
            default:
                return {};
            }
        }
        if(role == Qt::ToolTipRole)
        {
            if(index.column() >= 1 && index.column() <= 3)
            {
                return tr("Double-click to copy:\n%1")
                    .arg(data(index, Qt::DisplayRole).toString());
            }
            QString details = tr("Series Instance UID: %1\nFiles: %2")
                                  .arg(QString::fromStdString(
                                      candidate.seriesInstanceUid))
                                  .arg(static_cast<qulonglong>(
                                      candidate.recordIndices.size()));
            if(!candidate.detectionNote.empty())
            {
                details += QStringLiteral("\n")
                    + QString::fromStdString(candidate.detectionNote);
            }
            if(!candidate.consistent())
            {
                details += QStringLiteral("\n") + joinedIssues(candidate);
            }
            return details;
        }
        if(role == Qt::ForegroundRole
           && (index.column() == 6 || index.column() == 7))
        {
            if(candidate.missingSlicesOverrideAllowed
               || candidate.nonUniformSpacingOverrideAllowed
               || candidate.spacingMetadataMismatchOverrideAllowed)
            {
                return QColor(176, 101, 0);
            }
            return candidate.consistent() ? QColor(37, 122, 62)
                                          : QColor(184, 45, 45);
        }
        return {};
    }

    [[nodiscard]] QVariant headerData(
        const int section,
        const Qt::Orientation orientation,
        const int role) const override
    {
        if(orientation != Qt::Horizontal || role != Qt::DisplayRole)
        {
            return {};
        }
        switch(section)
        {
        case 0:
            return tr("Use");
        case 1:
            return tr("Series description");
        case 2:
            return tr("Series Instance UID");
        case 3:
            return tr("Files");
        case 4:
            return tr("Resolution");
        case 5:
            return tr("Slice spacing");
        case 6:
            return tr("Error code");
        case 7:
            return tr("Consistency");
        default:
            return {};
        }
    }

    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        auto result = QAbstractTableModel::flags(index) & ~Qt::ItemIsEditable;
        if(index.isValid() && index.column() == 0
           && analysis_.series[static_cast<std::size_t>(index.row())].importable())
        {
            result |= Qt::ItemIsUserCheckable;
        }
        return result;
    }

    bool setData(
        const QModelIndex& index, const QVariant& value, const int role) override
    {
        if(role != Qt::CheckStateRole || !index.isValid() || index.column() != 0
           || index.row() < 0
           || static_cast<std::size_t>(index.row()) >= analysis_.series.size()
           || !analysis_.series[static_cast<std::size_t>(index.row())].importable()
           || (value.toInt() != Qt::Checked && value.toInt() != Qt::Unchecked))
        {
            return false;
        }

        const auto row = static_cast<std::size_t>(index.row());
        if(value.toInt() == Qt::Unchecked)
        {
            if(!selectedSeries_[row])
            {
                return true;
            }
            selectedSeries_[row] = false;
            emit dataChanged(
                this->index(index.row(), 0),
                this->index(index.row(), 0),
                {Qt::CheckStateRole});
            return true;
        }

        const auto& selectedUid = analysis_.series[row].seriesInstanceUid;
        for(std::size_t candidate = 0; candidate < selectedSeries_.size(); ++candidate)
        {
            if(selectedSeries_[candidate]
               && analysis_.series[candidate].seriesInstanceUid != selectedUid)
            {
                selectedSeries_[candidate] = false;
            }
        }
        selectedSeries_[row] = true;
        emit dataChanged(
            this->index(0, 0),
            this->index(rowCount() - 1, 0),
            {Qt::CheckStateRole});
        return true;
    }

    [[nodiscard]] std::vector<std::size_t> selectedSeriesIndices() const
    {
        std::vector<std::size_t> indices;
        for(std::size_t index = 0; index < selectedSeries_.size(); ++index)
        {
            if(selectedSeries_[index])
            {
                indices.push_back(index);
            }
        }
        return indices;
    }

private:
    const io::DicomSeriesAnalysis& analysis_;
    std::vector<bool> selectedSeries_;
    std::vector<QString> fileNames_;
};

DicomSeriesDialog::DicomSeriesDialog(
    const std::vector<io::DicomFileRecord>& files,
    QWidget* const parent)
    : DicomSeriesDialog(files, io::analyzeDicomSeries(files), parent)
{
}

DicomSeriesDialog::DicomSeriesDialog(
    const std::vector<io::DicomFileRecord>& files,
    io::DicomSeriesAnalysis analysis,
    QWidget* const parent)
    : QDialog(parent)
    , files_(files)
    , analysis_(std::move(analysis))
{
    setObjectName(QStringLiteral("dicomSeriesDialog"));
    setWindowTitle(tr("Review DICOM Series"));
    setModal(true);
    resize(1250, 560);
    setMinimumSize(800, 420);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto* const explanation = new QLabel(
        tr("The DICOM input has been separated into candidate image stacks. "
           "Review their resolution, measured slice spacing, and consistency, "
           "then select one series or multiple parts of the same series to import. "
           "A selection with only missing slices, "
           "non-uniform slice spacing, or a disagreement between declared spacing "
           "and uniform slice positions can be imported with confirmation; other "
           "inconsistent rows cannot be imported."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("dicomSelectionSummary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    model_ = new DicomSeriesTableModel(analysis_, files_, this);
    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("dicomSeriesTable"));
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Interactive);
    table_->setColumnWidth(1, 200);
    table_->setColumnWidth(2, 280);
    table_->setColumnWidth(3, 420);
    table_->setColumnWidth(6, 290);
    table_->setColumnWidth(7, 520);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if(index.isValid() && index.column() != 0)
        {
            const auto checkIndex = model_->index(index.row(), 0);
            model_->setData(checkIndex, Qt::Checked, Qt::CheckStateRole);
        }
    });
    connect(
        table_,
        &QTableView::doubleClicked,
        this,
        [this](const QModelIndex& index) {
            if(index.isValid() && index.column() >= 1 && index.column() <= 3)
            {
                QGuiApplication::clipboard()->setText(
                    model_->data(index, Qt::DisplayRole).toString());
            }
        });
    layout->addWidget(table_, 1);

    if(!analysis_.ignoredIndices.empty())
    {
        auto* const ignoredLabel = new QLabel(
            tr("Ignored or unassigned files (%1)")
                .arg(static_cast<qulonglong>(analysis_.ignoredIndices.size())),
            this);
        layout->addWidget(ignoredLabel);

        auto* const ignoredFiles = new QListWidget(this);
        ignoredFiles->setObjectName(QStringLiteral("dicomIgnoredFiles"));
        ignoredFiles->setMaximumHeight(100);
        for(const auto index : analysis_.ignoredIndices)
        {
            const auto& file = files_[index];
            const QString reason = file.issue.empty()
                ? tr("Cannot be assigned to a Series Instance UID")
                : QString::fromStdString(file.issue);
            auto* const item = new QListWidgetItem(
                tr("%1 — %2").arg(pathToQString(file.filePath.filename()), reason),
                ignoredFiles);
            item->setToolTip(pathToQString(file.filePath));
        }
        layout->addWidget(ignoredFiles);
    }

    auto* const buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    importButton_ = buttons->button(QDialogButtonBox::Ok);
    importButton_->setText(tr("Import"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if(selectedSeriesIndices().size() > 1
           && selectedSeriesRequiresSliceSpacingOverride())
        {
            const auto answer = QMessageBox::warning(
                this,
                tr("Import Multiple DICOM Parts?"),
                tr("The selected parts share one Series Instance UID, but together "
                   "they do not form one uniformly spaced image stack. RadMarky will "
                   "spatially order every selected file on a uniform 3-D grid so an "
                   "annotation covering all selected slices can be loaded. Overlapping "
                   "or irregular source positions cannot all be preserved exactly, so "
                   "measurements and spatial alignment along the slice axis may be "
                   "inaccurate.\n\nImport all selected parts anyway?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if(answer != QMessageBox::Yes)
            {
                return;
            }
        }
        else if(selectedSeriesRequiresMissingSlicesOverride())
        {
            const auto answer = QMessageBox::warning(
                this,
                tr("Import Series with Missing Slices?"),
                tr("One or more expected DICOM image planes appear to be missing. "
                   "RadMarky will load the available slices on a uniformly spaced "
                   "3-D grid, but it cannot reconstruct the missing anatomy or "
                   "preserve every original slice position exactly. Measurements, "
                   "annotations, and spatial alignment along the slice axis may be "
                   "inaccurate near each gap.\n\nImport this series anyway?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if(answer != QMessageBox::Yes)
            {
                return;
            }
        }
        else if(selectedSeriesRequiresNonUniformSpacingOverride())
        {
            const auto answer = QMessageBox::warning(
                this,
                tr("Import Non-Uniform Slice Spacing?"),
                tr("The distances between these DICOM slices are not uniform. "
                   "RadMarky must represent the imported data on a uniformly spaced "
                   "3-D grid, so the original slice positions cannot all be preserved "
                   "exactly. Measurements and spatial alignment along the slice axis "
                   "may be inaccurate.\n\nImport this series anyway?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if(answer != QMessageBox::Yes)
            {
                return;
            }
        }
        else if(selectedSeriesRequiresSpacingMetadataMismatchOverride())
        {
            const auto answer = QMessageBox::warning(
                this,
                tr("Import Conflicting Slice Spacing?"),
                tr("The DICOM Spacing Between Slices metadata disagrees with the "
                   "spacing calculated from Image Position (Patient). The image "
                   "positions themselves are uniformly spaced, so RadMarky can use "
                   "their measured geometry despite the conflicting declared "
                   "spacing.\n\nImport this series anyway?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if(answer != QMessageBox::Yes)
            {
                return;
            }
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(
        model_,
        &QAbstractItemModel::dataChanged,
        this,
        [this] { updateSelectionState(); });
    layout->addWidget(buttons);

    updateSelectionState();
}

std::vector<std::filesystem::path> DicomSeriesDialog::selectedFilePaths() const
{
    std::vector<std::filesystem::path> paths;
    const auto selected = selectedSeriesIndices();
    for(const auto seriesIndex : selected)
    {
        for(const auto recordIndex : analysis_.series[seriesIndex].recordIndices)
        {
            paths.push_back(files_[recordIndex].filePath);
        }
    }
    return paths;
}

std::vector<io::DicomFileRecord> DicomSeriesDialog::selectedRecords() const
{
    const auto selected = selectedSeriesIndices();
    std::vector<io::DicomFileRecord> records;
    std::size_t recordCount = 0;
    for(const auto seriesIndex : selected)
    {
        recordCount += analysis_.series[seriesIndex].recordIndices.size();
    }
    records.reserve(recordCount);
    for(const auto seriesIndex : selected)
    {
        for(const auto recordIndex : analysis_.series[seriesIndex].recordIndices)
        {
            records.push_back(files_[recordIndex]);
        }
    }
    return records;
}

bool DicomSeriesDialog::selectedSeriesRequiresNonUniformSpacingOverride() const
{
    const auto records = selectedRecords();
    return !records.empty()
        && io::analyzeDicomGeometry(records).canOverrideNonUniformSpacing();
}

bool DicomSeriesDialog::selectedSeriesRequiresMissingSlicesOverride() const
{
    const auto records = selectedRecords();
    return !records.empty()
        && io::analyzeDicomGeometry(records).canOverrideMissingSlices();
}

bool DicomSeriesDialog::selectedSeriesRequiresSpacingMetadataMismatchOverride() const
{
    const auto records = selectedRecords();
    return !records.empty()
        && io::analyzeDicomGeometry(records).canOverrideSpacingMetadataMismatch();
}

bool DicomSeriesDialog::selectedSeriesRequiresSliceSpacingOverride() const
{
    const auto records = selectedRecords();
    return !records.empty()
        && io::analyzeDicomGeometry(records).canOverrideSliceSpacing();
}

std::vector<std::size_t> DicomSeriesDialog::selectedSeriesIndices() const
{
    return model_->selectedSeriesIndices();
}

void DicomSeriesDialog::updateSelectionState()
{
    const auto selected = selectedSeriesIndices();
    const auto records = selectedRecords();
    const auto geometry = records.empty()
        ? io::DicomGeometryAnalysis{}
        : io::analyzeDicomGeometry(records);
    const bool valid = !selected.empty()
        && (geometry.valid() || geometry.canOverrideSliceSpacing());
    importButton_->setEnabled(valid);

    const std::size_t selectedFiles = records.size();
    const std::size_t leftBehind = files_.size() - selectedFiles;
    summary_->setText(valid
            ? tr("Detected stacks: %1  |  Selected stacks: %2  |  Selected files: %3  |  "
                 "Left behind: %4")
                  .arg(static_cast<qulonglong>(analysis_.series.size()))
                  .arg(static_cast<qulonglong>(selected.size()))
                  .arg(static_cast<qulonglong>(selectedFiles))
                  .arg(static_cast<qulonglong>(leftBehind))
            : tr("Detected stacks: %1  |  Select one series or compatible parts to "
                 "continue  |  Ignored or left behind: %2")
                  .arg(static_cast<qulonglong>(analysis_.series.size()))
                  .arg(static_cast<qulonglong>(files_.size())));
}

} // namespace radmarky::ui
