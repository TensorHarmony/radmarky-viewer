#include "ui/ShortcutsDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <array>

namespace
{

QString nativeShortcut(const QKeySequence& sequence)
{
    return sequence.toString(QKeySequence::NativeText);
}

struct ShortcutRow
{
    QString type;
    QString input;
    QString context;
    QString action;
};

} // namespace

namespace radmarky::ui
{

ShortcutsDialog::ShortcutsDialog(QWidget* const parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("shortcutsDialog"));
    setWindowTitle(tr("Keyboard and Mouse Shortcuts"));
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(860, 620);

    auto* const root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 14);
    root->setSpacing(12);

    auto* const introduction = new QLabel(
        tr("The active view is the slice under the pointer, or the maximized "
           "slice when using a focused layout."),
        this);
    introduction->setObjectName(QStringLiteral("shortcutsIntroduction"));
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    auto* const table = new QTableWidget(0, 4, this);
    table->setObjectName(QStringLiteral("shortcutsTable"));
    table->setAccessibleName(tr("Keyboard and mouse shortcuts"));
    table->setHorizontalHeaderLabels(
        {tr("Type"), tr("Input"), tr("Where"), tr("Action")});
    table->verticalHeader()->hide();
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(true);

    const std::array rows{
        ShortcutRow{tr("Keyboard"), nativeShortcut(QKeySequence::Open),
                    tr("Application"), tr("Open images")},
        ShortcutRow{tr("Keyboard"), nativeShortcut(QKeySequence::Close),
                    tr("Application"), tr("Close the current images")},
        ShortcutRow{tr("Keyboard"), nativeShortcut(QKeySequence::Quit),
                    tr("Application"), tr("Exit RadMarky Viewer")},
        ShortcutRow{tr("Keyboard"), nativeShortcut(QKeySequence::Undo),
                    tr("Annotation editing"), tr("Undo the last edit")},
        ShortcutRow{tr("Keyboard"), nativeShortcut(QKeySequence::Redo),
                    tr("Annotation editing"), tr("Redo the last undone edit")},
        ShortcutRow{tr("Keyboard"), nativeShortcut(QKeySequence::Save),
                    tr("Annotation"), tr("Save the selected annotation")},
        ShortcutRow{
            tr("Keyboard"),
            nativeShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)),
            tr("Annotation"), tr("Save the selected annotation as a new file")},
        ShortcutRow{tr("Keyboard"), tr("1–9"), tr("Annotation editing"),
                    tr("Choose a label and activate the brush")},
        ShortcutRow{tr("Keyboard"), tr("0"), tr("Annotation editing"),
                    tr("Activate the eraser; press again to erase all labels")},
        ShortcutRow{tr("Keyboard"), tr("Shift+0–9"),
                    tr("Annotation editing"),
                    tr("Choose a paint-over label without changing tools; "
                       "labels 1–9 jump to their nearest axial slice")},
        ShortcutRow{tr("Keyboard"), tr("[ / ]"), tr("Annotation editing"),
                    tr("Decrease or increase the brush size")},
        ShortcutRow{tr("Keyboard"), tr("H"), tr("Viewer"),
                    tr("Hide or show all annotation overlays")},
        ShortcutRow{tr("Keyboard"), tr("W / A / S / D"), tr("Active view"),
                    tr("Pan up, left, down, or right")},
        ShortcutRow{tr("Keyboard"), tr("Page Up / Page Down"),
                    tr("Active view"), tr("Move through slices")},
        ShortcutRow{tr("Keyboard"), tr("F"), tr("Active view"),
                    tr("Center and fit the image")},
        ShortcutRow{tr("Keyboard"),
                    nativeShortcut(QKeySequence::HelpContents), tr("Application"),
                    tr("Open this shortcuts reference")},
        ShortcutRow{tr("Mouse"), tr("Move pointer"), tr("Slice view"),
                    tr("Inspect the voxel under the pointer")},
        ShortcutRow{tr("Mouse"), tr("Wheel"), tr("Slice view"),
                    tr("Move through slices")},
        ShortcutRow{tr("Mouse"), tr("Left-click or drag"), tr("Slice view"),
                    tr("Use the selected cursor, zoom, pan, contrast, measure, "
                       "brush, or erase tool")},
        ShortcutRow{tr("Mouse"), tr("Right-drag or Ctrl+left-drag"),
                    tr("Slice view"), tr("Zoom, regardless of the selected tool")},
        ShortcutRow{tr("Mouse"), tr("Middle-drag or Alt+left-drag"),
                    tr("Slice view"), tr("Pan, regardless of the selected tool")},
        ShortcutRow{
            tr("Mouse"), tr("Shift+left-drag"), tr("Slice view"),
            tr("Adjust window and level; with the Contrast tool selected, "
               "adjust annotation opacity")},
        ShortcutRow{tr("Mouse"), tr("Left-click or drag"),
                    tr("Zoom overview"), tr("Reposition the visible image area")},
        ShortcutRow{tr("Mouse"), tr("Ctrl+click"), tr("Annotations list"),
                    tr("Add or remove an annotation from the selection")},
        ShortcutRow{tr("Mouse"), tr("Shift+click"), tr("Annotations list"),
                    tr("Select a second annotation for comparison")},
    };

    table->setRowCount(static_cast<int>(rows.size()));
    for(std::size_t row = 0; row < rows.size(); ++row)
    {
        const auto& shortcut = rows[row];
        const std::array values{
            shortcut.type, shortcut.input, shortcut.context, shortcut.action};
        for(std::size_t column = 0; column < values.size(); ++column)
        {
            auto* const item = new QTableWidgetItem(values[column]);
            item->setToolTip(values[column]);
            table->setItem(
                static_cast<int>(row), static_cast<int>(column), item);
        }
    }
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->resizeRowsToContents();
    root->addWidget(table, 1);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("shortcutsButtons"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

} // namespace radmarky::ui
