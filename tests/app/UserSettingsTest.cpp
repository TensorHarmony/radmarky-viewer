#include "app/UserSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace
{

bool expect(const bool condition, const char* const message)
{
    if(!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool near(const double left, const double right)
{
    return std::abs(left - right) < 1.0e-9;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("radmarky-settings-test-" + std::to_string(unique));
    std::filesystem::create_directories(directory);
#ifdef _WIN32
    const QString path = QString::fromStdWString((directory / "settings.json").wstring());
#else
    const QString path = QString::fromStdString((directory / "settings.json").string());
#endif
    const QString existingImagePath = QFileInfo(path).absoluteDir().filePath(
        QStringLiteral("existing-scan.nii.gz"));
    QFile existingImage(existingImagePath);
    if(!existingImage.open(QIODevice::WriteOnly)
       || existingImage.write("test") < 0)
    {
        std::cerr << "FAILED: create existing recent image\n";
        std::filesystem::remove_all(directory);
        return 1;
    }
    existingImage.close();
    const QString existingAnnotationPath = QFileInfo(path).absoluteDir().filePath(
        QStringLiteral("existing-labels.nii.gz"));
    QFile existingAnnotation(existingAnnotationPath);
    if(!existingAnnotation.open(QIODevice::WriteOnly)
       || existingAnnotation.write("labels") < 0)
    {
        std::cerr << "FAILED: create existing recent annotation\n";
        std::filesystem::remove_all(directory);
        return 1;
    }
    existingAnnotation.close();
    bool passed = true;
    {
        radmarky::app::UserSettings settings(path);
        passed &= expect(settings.settingsFilePath() == path, "settings path override");
        passed &= expect(
            !settings.keepWindowOnTop(), "keep-on-top defaults to disabled");
        passed &= expect(
            settings.validationScripts().size() == 3
                && std::all_of(
                    settings.validationScripts().begin(),
                    settings.validationScripts().end(),
                    [](const auto& script) { return !script.enabled; }),
            "built-in validation presets start disabled");
        settings.setDarkTheme(true);
        settings.setKeepWindowOnTop(true);
        passed &= expect(QFile::exists(path), "settings file created");
        settings.setDefaultWindowLevel(410.0, 45.0);
        settings.addWindowLevelPreset(QStringLiteral("Vessels"), 700.0, 210.0);
        passed &= expect(settings.brushRadius(0) == 1, "eraser brush size default");
        passed &= expect(settings.brushRadius(7) == 1, "label brush size default");
        settings.setBrushRadius(0, 9);
        settings.setBrushRadius(1, 3);
        settings.setBrushRadius(7, 12);
        settings.setBrushRadius(-1, 50);
        settings.setBrushRadius(8, 101);
        passed &= expect(
            settings.paintOverSelection(0) == -1,
            "eraser paint-over default");
        passed &= expect(
            settings.paintOverSelection(7) == -1,
            "label paint-over default");
        settings.setPaintOverSelection(0, 0);
        settings.setPaintOverSelection(1, -1);
        settings.setPaintOverSelection(7, 3);
        settings.setPaintOverSelection(-1, 2);
        settings.setPaintOverSelection(8, 65536);
        settings.addRecentImage(radmarky::app::RecentImageSetting{
            QStringLiteral("existing-scan.nii.gz"),
            QStringLiteral("NIfTI"),
            {existingImagePath},
            QStringLiteral("C:/cache/existing-scan.png"),
            QByteArray::fromHex("00112233"),
            2,
            2,
            {{existingAnnotationPath, 0.35}},
            7});
        settings.addRecentImage(radmarky::app::RecentImageSetting{
            QStringLiteral("scan.nii.gz"),
            QStringLiteral("NIfTI"),
            {QStringLiteral("C:/images/scan.nii.gz")},
            QStringLiteral("C:/cache/scan.png"),
            {},
            0,
            0,
            {},
            0});
        settings.setValidationScripts({
            {QStringLiteral("Continuity"), QStringLiteral("C:/validators/one.py"), true},
            {QStringLiteral("Contours"), QStringLiteral("C:/validators/two.py"), false},
        });
        settings.setWindowLayout({
            QByteArrayLiteral("geom-bytes"),
            QByteArrayLiteral("state-bytes"),
            {640, 320},
            {360, 360},
            QStringLiteral("axial"),
        });
    }
    {
        radmarky::app::UserSettings settings(path);
        passed &= expect(settings.darkTheme(), "dark theme round trip");
        passed &= expect(
            settings.keepWindowOnTop(), "keep-on-top round trip");
        passed &= expect(
            settings.brushRadius(0) == 9
                && settings.brushRadius(1) == 3
                && settings.brushRadius(7) == 12
                && settings.brushRadius(8) == 1,
            "per-label and eraser brush sizes round trip");
        passed &= expect(
            settings.paintOverSelection(0) == -1
                && settings.paintOverSelection(1) == -1
                && settings.paintOverSelection(7) == 3
                && settings.paintOverSelection(8) == -1,
            "per-label and eraser paint-over choices round trip");
        passed &= expect(
            settings.defaultWindowLevel().has_value(),
            "default window level round trip");
        if(settings.defaultWindowLevel())
        {
            passed &= expect(
                near(settings.defaultWindowLevel()->window, 410.0),
                "default window round trip");
            passed &= expect(
                near(settings.defaultWindowLevel()->level, 45.0),
                "default level round trip");
        }
        passed &= expect(
            settings.windowLevelPresets().size() == 1,
            "named preset count");
        passed &= expect(settings.recentImages().size() == 2, "recent count");
        if(settings.recentImages().size() == 2)
        {
            passed &= expect(
                settings.recentImages()[0].name == QStringLiteral("scan.nii.gz")
                    && settings.recentImages()[1].name
                        == QStringLiteral("existing-scan.nii.gz"),
                "most recently opened image is first");
            passed &= expect(
                settings.recentImages()[0].activeLabel == 0,
                "clear eraser selection round trip");
            passed &= expect(
                settings.recentImages()[1].thumbnailPath
                    == QStringLiteral("C:/cache/existing-scan.png"),
                "recent thumbnail path round trip");
            passed &= expect(
                settings.recentImages()[1].thumbnailPixels
                        == QByteArray::fromHex("00112233")
                    && settings.recentImages()[1].thumbnailWidth == 2
                    && settings.recentImages()[1].thumbnailHeight == 2,
                "embedded recent thumbnail round trip");
            passed &= expect(
                settings.recentImages()[1].annotations
                        == std::vector<radmarky::app::RecentAnnotationSetting>{
                            {existingAnnotationPath, 0.35}}
                    && settings.recentImages()[1].activeLabel == 7,
                "recent annotation state round trip");
        }
        passed &= expect(
            settings.validationScripts().size() == 2,
            "validation script count round trip");
        if(settings.validationScripts().size() == 2)
        {
            passed &= expect(
                settings.validationScripts()[0].name == QStringLiteral("Continuity")
                    && settings.validationScripts()[0].enabled,
                "enabled validation script round trip");
            passed &= expect(
                settings.validationScripts()[1].path
                        == QStringLiteral("C:/validators/two.py")
                    && !settings.validationScripts()[1].enabled,
                "disabled validation script round trip");
        }
        passed &= expect(
            settings.windowLayout().has_value(), "window layout round trip");
        if(settings.windowLayout())
        {
            passed &= expect(
                settings.windowLayout()->geometry == QByteArrayLiteral("geom-bytes"),
                "window geometry round trip");
            passed &= expect(
                settings.windowLayout()->windowState
                    == QByteArrayLiteral("state-bytes"),
                "window state round trip");
            passed &= expect(
                settings.windowLayout()->viewSplitterSizes == QList<int>({640, 320}),
                "view splitter sizes round trip");
            passed &= expect(
                settings.windowLayout()->rightViewSplitterSizes
                    == QList<int>({360, 360}),
                "right view splitter sizes round trip");
            passed &= expect(
                settings.windowLayout()->focusedView == QStringLiteral("axial"),
                "focused view round trip");
        }
        passed &= expect(
            settings.removeMissingRecentImages() == 1,
            "missing recent removed");
        passed &= expect(
            settings.recentImages().size() == 1,
            "existing recent retained");
        if(!settings.recentImages().empty())
        {
            passed &= expect(
                settings.recentImages().front().sourceFiles
                    == QStringList{existingImagePath},
                "existing recent source retained");
        }
        settings.addWindowLevelPreset(QStringLiteral("vessels"), 900.0, 300.0);
    }
    {
        radmarky::app::UserSettings settings(path);
        passed &= expect(
            settings.windowLevelPresets().size() == 1,
            "case-insensitive preset replacement");
        if(!settings.windowLevelPresets().empty())
        {
            passed &= expect(
                near(settings.windowLevelPresets().front().window, 900.0),
                "replacement preset value");
        }
        passed &= expect(
            settings.recentImages().size() == 1,
            "pruned recents persisted");
        passed &= expect(
            !settings.removeRecentImage(1),
            "out-of-range recent removal rejected");
        passed &= expect(
            settings.removeRecentImage(0) && settings.recentImages().empty(),
            "recent image removed explicitly");
    }
    {
        radmarky::app::UserSettings settings(path);
        passed &= expect(
            settings.recentImages().empty(),
            "explicit recent removal persisted");
    }
    std::filesystem::remove_all(directory);
    return passed ? 0 : 1;
}
