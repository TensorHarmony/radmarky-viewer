#include "ui/DicomSeriesDialog.h"

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
        , selectedSeries_(analysis_.proposedSeriesIndex)
    {
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
        return parent.isValid() ? 0 : 7;
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
            return selectedSeries_ && *selectedSeries_ == row
                ? Qt::Checked
                : Qt::Unchecked;
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
        if(role == Qt::ForegroundRole && index.column() == 6)
        {
            if(candidate.nonUniformSpacingOverrideAllowed
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
           || value.toInt() != Qt::Checked)
        {
            return false;
        }
        const auto previous = selectedSeries_;
        selectedSeries_ = static_cast<std::size_t>(index.row());
        if(previous)
        {
            emit dataChanged(
                this->index(static_cast<int>(*previous), 0),
                this->index(static_cast<int>(*previous), 0),
                {Qt::CheckStateRole});
        }
        emit dataChanged(
            this->index(index.row(), 0),
            this->index(index.row(), 0),
            {Qt::CheckStateRole});
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> selectedSeriesIndex() const
    {
        return selectedSeries_;
    }

private:
    const io::DicomSeriesAnalysis& analysis_;
    std::optional<std::size_t> selectedSeries_;
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
           "then select one series to import. A series with only non-uniform slice "
           "spacing, or only a disagreement between declared spacing and uniform "
           "slice positions, can be imported with confirmation; other inconsistent "
           "rows cannot be imported."),
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
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    table_->setColumnWidth(2, 250);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if(index.isValid())
        {
            model_->setData(
                model_->index(index.row(), 0),
                Qt::Checked,
                Qt::CheckStateRole);
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
        if(selectedSeriesRequiresNonUniformSpacingOverride())
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
    const auto selected = selectedSeriesIndex();
    if(!selected)
    {
        return paths;
    }
    for(const auto index : analysis_.series[*selected].recordIndices)
    {
        paths.push_back(files_[index].filePath);
    }
    return paths;
}

std::vector<io::DicomFileRecord> DicomSeriesDialog::selectedRecords() const
{
    const auto selected = selectedSeriesIndex();
    std::vector<io::DicomFileRecord> records;
    if(!selected)
    {
        return records;
    }
    const auto& indices = analysis_.series[*selected].recordIndices;
    records.reserve(indices.size());
    for(const auto index : indices)
    {
        records.push_back(files_[index]);
    }
    return records;
}

bool DicomSeriesDialog::selectedSeriesRequiresNonUniformSpacingOverride() const
{
    const auto selected = selectedSeriesIndex();
    return selected && *selected < analysis_.series.size()
        && analysis_.series[*selected].nonUniformSpacingOverrideAllowed;
}

bool DicomSeriesDialog::selectedSeriesRequiresSpacingMetadataMismatchOverride() const
{
    const auto selected = selectedSeriesIndex();
    return selected && *selected < analysis_.series.size()
        && analysis_.series[*selected].spacingMetadataMismatchOverrideAllowed;
}

bool DicomSeriesDialog::selectedSeriesRequiresSliceSpacingOverride() const
{
    return selectedSeriesRequiresNonUniformSpacingOverride()
        || selectedSeriesRequiresSpacingMetadataMismatchOverride();
}

std::optional<std::size_t> DicomSeriesDialog::selectedSeriesIndex() const
{
    return model_->selectedSeriesIndex();
}

void DicomSeriesDialog::updateSelectionState()
{
    const auto selected = selectedSeriesIndex();
    const bool valid = selected
        && *selected < analysis_.series.size()
        && analysis_.series[*selected].importable();
    importButton_->setEnabled(valid);

    const std::size_t selectedFiles = valid
        ? analysis_.series[*selected].recordIndices.size()
        : 0;
    const std::size_t leftBehind = files_.size() - selectedFiles;
    summary_->setText(valid
            ? tr("Detected series: %1  |  Selected files: %2  |  Left behind: %3")
                  .arg(static_cast<qulonglong>(analysis_.series.size()))
                  .arg(static_cast<qulonglong>(selectedFiles))
                  .arg(static_cast<qulonglong>(leftBehind))
            : tr("Detected series: %1  |  Select one importable series to continue  |  "
                 "Ignored or left behind: %2")
                  .arg(static_cast<qulonglong>(analysis_.series.size()))
                  .arg(static_cast<qulonglong>(files_.size())));
}

} // namespace radmarky::ui
