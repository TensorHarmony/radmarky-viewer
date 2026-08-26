#include "ui/DicomHeaderDialog.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace radmarky::ui
{

DicomHeaderDialog::DicomHeaderDialog(
    const std::vector<core::Volume::DicomMetadataEntry>& metadata,
    QWidget* const parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("dicomHeaderDialog"));
    setWindowTitle(tr("Image Metadata"));
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);

    auto* const title = new QLabel(tr("Image Metadata"), this);
    title->setObjectName(QStringLiteral("dicomHeaderTitle"));
    layout->addWidget(title);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("dicomMetadataSearch"));
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(tr("Search keys and values"));
    search_->setAccessibleName(tr("Search DICOM metadata"));
    layout->addWidget(search_);

    sourceModel_ = new QStandardItemModel(this);
    sourceModel_->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    for(const auto& entry : metadata)
    {
        auto* const key = new QStandardItem(QString::fromUtf8(entry.key.c_str()));
        auto* const value = new QStandardItem(QString::fromUtf8(entry.value.c_str()));
        key->setEditable(false);
        value->setEditable(false);
        sourceModel_->appendRow({key, value});
    }

    filterModel_ = new QSortFilterProxyModel(this);
    filterModel_->setObjectName(QStringLiteral("dicomMetadataFilterModel"));
    filterModel_->setSourceModel(sourceModel_);
    filterModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filterModel_->setFilterKeyColumn(-1);
    filterModel_->setDynamicSortFilter(true);

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("dicomMetadataTable"));
    table_->setModel(filterModel_);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideNone);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(25);
    table_->horizontalHeader()->setSectionsMovable(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setColumnWidth(0, 330);

    emptyState_ = new QLabel(tr("No metadata matches your search."), this);
    emptyState_->setObjectName(QStringLiteral("dicomMetadataEmptyState"));
    emptyState_->setAlignment(Qt::AlignCenter);

    results_ = new QStackedWidget(this);
    results_->setObjectName(QStringLiteral("dicomMetadataResults"));
    results_->addWidget(table_);
    results_->addWidget(emptyState_);
    layout->addWidget(results_, 1);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("dicomHeaderButtons"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        filterModel_->setFilterFixedString(text);
        updateFilteredState();
    });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if(index.isValid())
        {
            QGuiApplication::clipboard()->setText(
                filterModel_->data(index, Qt::DisplayRole).toString());
        }
    });
    updateFilteredState();

    const QScreen* screen = parent != nullptr ? parent->screen() : QGuiApplication::primaryScreen();
    const QSize available = screen != nullptr
        ? screen->availableGeometry().size()
        : QSize(1200, 800);
    resize(
        std::max(520, std::min(1000, available.width() - 80)),
        std::max(360, std::min(720, available.height() - 80)));
    search_->setFocus();
}

void DicomHeaderDialog::updateFilteredState()
{
    results_->setCurrentWidget(
        filterModel_->rowCount() == 0 ? static_cast<QWidget*>(emptyState_)
                                      : static_cast<QWidget*>(table_));
}

} // namespace radmarky::ui
