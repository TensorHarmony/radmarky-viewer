#pragma once

#include <QDialog>

namespace radmarky::ui
{

class ShortcutsDialog final : public QDialog
{
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};

} // namespace radmarky::ui
