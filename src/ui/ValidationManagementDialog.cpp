#include "ui/ValidationManagementDialog.h"

#include "ui/CodeEditor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace radmarky::ui
{
namespace
{

constexpr int validatorPathRole = Qt::UserRole + 1;

class ValidatorDetailsDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(
        QPainter* const painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        QStyle* const style = background.widget != nullptr
            ? background.widget->style() : QApplication::style();
        style->drawControl(
            QStyle::CE_ItemViewItem, &background, painter, background.widget);

        const QRect content = option.rect.adjusted(8, 4, -6, -4);
        QFont nameFont = option.font;
        nameFont.setBold(true);
        QFont pathFont = option.font;
        if(pathFont.pointSizeF() > 0.0)
        {
            pathFont.setPointSizeF(std::max(7.0, pathFont.pointSizeF() - 1.5));
        }
        else
        {
            pathFont.setPixelSize(std::max(8, pathFont.pixelSize() - 2));
        }
        const QFontMetrics nameMetrics(nameFont);
        const QFontMetrics pathMetrics(pathFont);
        const int nameHeight = nameMetrics.height();
        const int pathHeight = pathMetrics.height();
        const QColor textColor = option.palette.color(
            option.state.testFlag(QStyle::State_Selected)
                ? QPalette::HighlightedText : QPalette::Text);

        painter->save();
        painter->setPen(textColor);
        painter->setFont(nameFont);
        painter->drawText(
            QRect(content.left(), content.top(), content.width(), nameHeight),
            Qt::AlignLeft | Qt::AlignVCenter,
            nameMetrics.elidedText(
                index.data(Qt::DisplayRole).toString(),
                Qt::ElideRight, content.width()));
        painter->setFont(pathFont);
        painter->drawText(
            QRect(
                content.left(), content.top() + nameHeight + 1,
                content.width(), pathHeight),
            Qt::AlignLeft | Qt::AlignVCenter,
            pathMetrics.elidedText(
                index.data(validatorPathRole).toString(),
                Qt::ElideMiddle, content.width()));
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(
        const QStyleOptionViewItem& option,
        const QModelIndex&) const override
    {
        QFont nameFont = option.font;
        nameFont.setBold(true);
        QFont pathFont = option.font;
        if(pathFont.pointSizeF() > 0.0)
        {
            pathFont.setPointSizeF(std::max(7.0, pathFont.pointSizeF() - 1.5));
        }
        else
        {
            pathFont.setPixelSize(std::max(8, pathFont.pixelSize() - 2));
        }
        return {
            260,
            QFontMetrics(nameFont).height()
                + QFontMetrics(pathFont).height() + 10};
    }
};

} // namespace

ValidationManagementDialog::ValidationManagementDialog(
    std::vector<app::ValidationScriptSetting> scripts,
    const bool canValidate,
    Inspector inspector,
    ValidateCallback validate,
    QWidget* const parent)
    : QDialog(parent)
    , scripts_(std::move(scripts))
    , inspector_(std::move(inspector))
    , validate_(std::move(validate))
{
    setObjectName(QStringLiteral("validationManagementDialog"));
    setWindowTitle(tr("Validation Management"));
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);

    auto* const title = new QLabel(tr("Python annotation validators"), this);
    title->setObjectName(QStringLiteral("validationManagementTitle"));
    layout->addWidget(title);
    auto* const explanation = new QLabel(
        tr("Enabled scripts run before an annotation is saved. A script that "
           "returns an error prevents the save."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* const splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("validationContentSplitter"));
    splitter->setChildrenCollapsible(false);

    auto* const listPanel = new QWidget(splitter);
    auto* const listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 0, 0);
    table_ = new QTableWidget(listPanel);
    table_->setObjectName(QStringLiteral("validationScriptsTable"));
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels(
        {tr("Enabled"), tr("Validator"), tr("Status")});
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setItemDelegateForColumn(
        1, new ValidatorDetailsDelegate(table_));

    emptyState_ = new QLabel(
        tr("No validation scripts are registered. Add a trusted Python file to begin."),
        listPanel);
    emptyState_->setObjectName(QStringLiteral("validationScriptsEmptyState"));
    emptyState_->setAlignment(Qt::AlignCenter);
    emptyState_->setWordWrap(true);

    auto* const content = new QStackedWidget(listPanel);
    content->setObjectName(QStringLiteral("validationScriptsContent"));
    content->addWidget(table_);
    content->addWidget(emptyState_);
    listLayout->addWidget(content);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        removeButton_->setEnabled(table_->currentRow() >= 0);
        loadSelectedScript();
    });

    auto* const editorPanel = new QWidget(splitter);
    auto* const editorLayout = new QVBoxLayout(editorPanel);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    auto* const sourceLabel = new QLabel(tr("Script source"), editorPanel);
    sourceLabel->setObjectName(QStringLiteral("validationScriptSourceLabel"));
    editorLayout->addWidget(sourceLabel);
    codeEditor_ = new CodeEditor(editorPanel);
    codeEditor_->setObjectName(QStringLiteral("validationScriptCodeEditor"));
    codeEditor_->setAccessibleName(tr("Python validation script source"));
    codeEditor_->setReadOnly(true);
    codeEditor_->setPlaceholderText(
        tr("Select a validator to inspect its Python source."));
    codeEditor_->setMinimumSize(320, 220);
    editorLayout->addWidget(codeEditor_, 1);
    splitter->addWidget(listPanel);
    splitter->addWidget(editorPanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({420, 630});
    layout->addWidget(splitter, 1);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if(rebuilding_ || item == nullptr || item->column() != 0)
        {
            return;
        }
        const int row = item->row();
        if(row >= 0 && static_cast<std::size_t>(row) < scripts_.size())
        {
            scripts_[static_cast<std::size_t>(row)].enabled =
                item->checkState() == Qt::Checked;
        }
    });

    auto* const controls = new QDialogButtonBox(this);
    auto* const addButton = controls->addButton(
        tr("Add Python script…"), QDialogButtonBox::ActionRole);
    addButton->setObjectName(QStringLiteral("addValidationScriptButton"));
    removeButton_ = controls->addButton(
        tr("Remove"), QDialogButtonBox::ActionRole);
    removeButton_->setObjectName(QStringLiteral("removeValidationScriptButton"));
    removeButton_->setEnabled(false);
    validateButton_ = controls->addButton(
        tr("Validate now"), QDialogButtonBox::ActionRole);
    validateButton_->setObjectName(QStringLiteral("validateNowButton"));
    validateButton_->setEnabled(canValidate);
    controls->addButton(QDialogButtonBox::Close);
    layout->addWidget(controls);
    connect(addButton, &QPushButton::clicked, this, [this] { chooseAndAddScript(); });
    connect(removeButton_, &QPushButton::clicked, this, [this] {
        removeSelectedScript();
    });
    connect(validateButton_, &QPushButton::clicked, this, [this] {
        if(validate_)
        {
            validate_(scripts_);
        }
    });
    connect(controls, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildTable();
    const QScreen* const screen = parent != nullptr
        ? parent->screen() : QGuiApplication::primaryScreen();
    const QSize available = screen != nullptr
        ? screen->availableGeometry().size() : QSize(1200, 800);
    setMinimumSize(780, 420);
    resize(
        std::max(780, std::min(1200, available.width() - 80)),
        std::max(420, std::min(720, available.height() - 80)));
}

const std::vector<app::ValidationScriptSetting>&
ValidationManagementDialog::scripts() const noexcept
{
    return scripts_;
}

bool ValidationManagementDialog::addScriptPath(
    const QString& path,
    QString* const errorMessage)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    QString error;
    if(!info.isFile())
    {
        error = tr("The selected Python script does not exist.");
    }
    else if(info.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) != 0)
    {
        error = tr("Validation scripts must use the .py extension.");
    }
    else if(std::any_of(
                scripts_.begin(), scripts_.end(), [&canonical](const auto& script) {
                    const QString existing = QFileInfo(script.path).canonicalFilePath();
                    return !canonical.isEmpty()
                        && existing.compare(canonical, Qt::CaseInsensitive) == 0;
                }))
    {
        error = tr("This validation script is already registered.");
    }
    else if(inspector_)
    {
        const auto inspection = inspector_(canonical);
        if(!inspection.passed())
        {
            error = inspection.message;
        }
    }
    if(!error.isEmpty())
    {
        if(errorMessage != nullptr)
        {
            *errorMessage = error;
        }
        return false;
    }
    scripts_.push_back({info.completeBaseName(), canonical, true});
    rebuildTable();
    table_->selectRow(table_->rowCount() - 1);
    return true;
}

void ValidationManagementDialog::setCanValidate(const bool canValidate)
{
    validateButton_->setEnabled(canValidate);
}

void ValidationManagementDialog::rebuildTable()
{
    rebuilding_ = true;
    table_->setRowCount(static_cast<int>(scripts_.size()));
    for(int row = 0; row < static_cast<int>(scripts_.size()); ++row)
    {
        const auto& script = scripts_[static_cast<std::size_t>(row)];
        auto* const enabled = new QTableWidgetItem;
        enabled->setFlags(
            Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        enabled->setCheckState(script.enabled ? Qt::Checked : Qt::Unchecked);
        table_->setItem(row, 0, enabled);
        auto* const details = new QTableWidgetItem(script.name);
        const QString displayPath = QDir::toNativeSeparators(script.path);
        details->setData(validatorPathRole, displayPath);
        details->setToolTip(displayPath);
        table_->setItem(row, 1, details);
        table_->setItem(
            row, 2,
            new QTableWidgetItem(
                QFile::exists(script.path) ? tr("Ready") : tr("Missing")));
    }
    table_->resizeRowsToContents();
    rebuilding_ = false;
    updateEmptyState();
    if(!scripts_.empty() && table_->currentRow() < 0)
    {
        table_->selectRow(0);
    }
}

void ValidationManagementDialog::updateEmptyState()
{
    auto* const content = qobject_cast<QStackedWidget*>(table_->parentWidget());
    if(content != nullptr)
    {
        content->setCurrentWidget(
            scripts_.empty() ? static_cast<QWidget*>(emptyState_)
                             : static_cast<QWidget*>(table_));
    }
    removeButton_->setEnabled(!scripts_.empty() && table_->currentRow() >= 0);
}

void ValidationManagementDialog::loadSelectedScript()
{
    const int row = table_->currentRow();
    if(row < 0 || static_cast<std::size_t>(row) >= scripts_.size())
    {
        codeEditor_->clear();
        return;
    }
    QFile file(scripts_[static_cast<std::size_t>(row)].path);
    if(!file.open(QIODevice::ReadOnly))
    {
        codeEditor_->setPlainText(tr("Unable to read this Python script."));
        return;
    }
    codeEditor_->setPlainText(QString::fromUtf8(file.readAll()));
    codeEditor_->moveCursor(QTextCursor::Start);
}

void ValidationManagementDialog::chooseAndAddScript()
{
    const auto answer = QMessageBox::warning(
        this,
        tr("Only Add Trusted Python Code"),
        tr("Python validators run in a separate installed-Python process with "
           "your account permissions. They can access your files and start "
           "other programs. Add only code you trust."),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if(answer != QMessageBox::Ok)
    {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add Python Validation Script"), {}, tr("Python files (*.py)"));
    if(path.isEmpty())
    {
        return;
    }
    QString error;
    if(!addScriptPath(path, &error))
    {
        QMessageBox::critical(this, tr("Unable to Add Validator"), error);
    }
}

void ValidationManagementDialog::removeSelectedScript()
{
    const int row = table_->currentRow();
    if(row < 0 || static_cast<std::size_t>(row) >= scripts_.size())
    {
        return;
    }
    scripts_.erase(scripts_.begin() + row);
    rebuildTable();
    if(!scripts_.empty())
    {
        table_->selectRow(std::min(row, table_->rowCount() - 1));
    }
}

} // namespace radmarky::ui
