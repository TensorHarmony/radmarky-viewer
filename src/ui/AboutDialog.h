#pragma once

#include <QDialog>

namespace radmarky::ui
{

class AboutDialog final : public QDialog
{
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace radmarky::ui
