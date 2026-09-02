#pragma once

#include "io/DicomSeries.h"

#include <QDialog>

#include <filesystem>
#include <vector>

class QLabel;
class QPushButton;
class QTableView;

namespace radmarky::ui
{

class DicomSeriesTableModel;

class DicomSeriesDialog final : public QDialog
{
public:
    explicit DicomSeriesDialog(
        const std::vector<io::DicomFileRecord>& files,
        QWidget* parent = nullptr);
    DicomSeriesDialog(
        const std::vector<io::DicomFileRecord>& files,
        io::DicomSeriesAnalysis analysis,
        QWidget* parent);
    DicomSeriesDialog(
        std::vector<io::DicomFileRecord>&& files,
        QWidget* parent = nullptr) = delete;

    [[nodiscard]] std::vector<std::filesystem::path> selectedFilePaths() const;
    [[nodiscard]] std::vector<io::DicomFileRecord> selectedRecords() const;
    [[nodiscard]] bool selectedSeriesRequiresSliceSpacingOverride() const;
    [[nodiscard]] bool selectedSeriesRequiresMissingSlicesOverride() const;
    [[nodiscard]] bool selectedSeriesRequiresNonUniformSpacingOverride() const;
    [[nodiscard]] bool selectedSeriesRequiresSpacingMetadataMismatchOverride() const;

private:
    [[nodiscard]] std::optional<std::size_t> selectedSeriesIndex() const;
    void updateSelectionState();

    const std::vector<io::DicomFileRecord>& files_;
    io::DicomSeriesAnalysis analysis_;
    DicomSeriesTableModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* importButton_ = nullptr;
};

} // namespace radmarky::ui
