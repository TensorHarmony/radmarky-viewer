#pragma once

#include "core/Volume.h"

#include <QDialog>

#include <vector>

class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QStandardItemModel;
class QStackedWidget;
class QTableView;

namespace radmarky::ui
{

class DicomHeaderDialog final : public QDialog
{
public:
    explicit DicomHeaderDialog(
        const std::vector<core::Volume::DicomMetadataEntry>& metadata,
        QWidget* parent = nullptr);

private:
    void updateFilteredState();

    QLineEdit* search_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* emptyState_ = nullptr;
    QStackedWidget* results_ = nullptr;
    QStandardItemModel* sourceModel_ = nullptr;
    QSortFilterProxyModel* filterModel_ = nullptr;
};

} // namespace radmarky::ui
