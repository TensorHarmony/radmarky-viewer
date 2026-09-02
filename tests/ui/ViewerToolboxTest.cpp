#include "ui/ViewerToolbox.h"

#include <QApplication>
#include <QComboBox>

#include <iostream>
#include <string_view>

namespace
{

bool expect(const bool condition, const std::string_view message)
{
    if(condition)
    {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    radmarky::ui::ViewerToolbox toolbox;
    toolbox.setWindowLevel(400.0, 40.0, -1024.0, 3071.0);
    toolbox.addAnnotation(
        QStringLiteral("Editable"), QStringLiteral("Label map"), 1.0, true);
    toolbox.setAnnotationEditingState(true, false, false);
    toolbox.setAnnotationLabels({4, 2, 4});
    toolbox.setPaintOverSelections({{0, 0}, {4, 0}});

    auto* const activeLabel =
        toolbox.findChild<QComboBox*>(QStringLiteral("activeLabelCombo"));
    auto* const paintOver =
        toolbox.findChild<QComboBox*>(QStringLiteral("paintOverCombo"));
    bool passed = true;
    passed &= expect(activeLabel != nullptr, "active-label combo exists");
    passed &= expect(paintOver != nullptr, "paint-over combo exists");
    if(activeLabel == nullptr || paintOver == nullptr)
    {
        return 1;
    }

    toolbox.setActiveLabel(0);
    passed &= expect(toolbox.activeLabel() == 0, "eraser becomes active");
    passed &= expect(
        paintOver->findData(0) == -1,
        "clear label is absent from eraser paint-over options");
    passed &= expect(
        paintOver->currentData().toInt() == -1,
        "invalid persisted eraser target falls back to all labels");
    passed &= expect(
        paintOver->findData(2) >= 0 && paintOver->findData(4) >= 0,
        "eraser can target existing nonzero labels");

    paintOver->setCurrentIndex(paintOver->findData(2));
    toolbox.setActiveLabel(4);
    passed &= expect(toolbox.activeLabel() == 4, "drawing label becomes active");
    passed &= expect(
        paintOver->findData(0) >= 0,
        "clear label returns for drawing paint-over options");
    passed &= expect(
        paintOver->currentData().toInt() == 0,
        "drawing label restores its clear-label paint-over preference");

    toolbox.setActiveLabel(0);
    passed &= expect(
        paintOver->findData(0) == -1 && paintOver->currentData().toInt() == 2,
        "eraser restores only its valid nonzero paint-over preference");
    (void)toolbox.setPaintOverSelection(-1);
    passed &= expect(
        paintOver->currentData().toInt() == -1,
        "eraser shortcut can switch paint-over to all labels");
    (void)toolbox.setPaintOverSelection(4);
    passed &= expect(
        toolbox.activeLabel() == 0 && paintOver->currentData().toInt() == 4,
        "paint-over shortcut changes the target without changing tools");

    return passed ? 0 : 1;
}
