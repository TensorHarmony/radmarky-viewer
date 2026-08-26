#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <optional>
#include <vector>

namespace radmarky::app
{

struct WindowLevelSetting
{
    QString name;
    double window = 1.0;
    double level = 0.0;
};

struct RecentAnnotationSetting
{
    QString sourceFile;
    double opacity = 0.5;

    [[nodiscard]] bool operator==(const RecentAnnotationSetting&) const = default;
};

struct RecentImageSetting
{
    QString name;
    QString kind;
    QStringList sourceFiles;
    QString thumbnailPath;
    QByteArray thumbnailPixels;
    int thumbnailWidth = 0;
    int thumbnailHeight = 0;
    std::vector<RecentAnnotationSetting> annotations;
    int activeLabel = 1;
};

struct ValidationScriptSetting
{
    QString name;
    QString path;
    bool enabled = true;

    [[nodiscard]] bool operator==(const ValidationScriptSetting&) const = default;
};

struct WindowLayoutSetting
{
    QByteArray geometry;
    QByteArray windowState;
    QList<int> viewSplitterSizes;
    QList<int> rightViewSplitterSizes;
    QString focusedView;

    [[nodiscard]] bool operator==(const WindowLayoutSetting&) const = default;
};

class UserSettings
{
public:
    explicit UserSettings(QString settingsFilePath = {});

    [[nodiscard]] bool darkTheme() const noexcept;
    void setDarkTheme(bool dark);

    [[nodiscard]] bool keepWindowOnTop() const noexcept;
    void setKeepWindowOnTop(bool keepOnTop);

    [[nodiscard]] const std::optional<WindowLevelSetting>&
    defaultWindowLevel() const noexcept;
    void setDefaultWindowLevel(double window, double level);

    [[nodiscard]] const std::vector<WindowLevelSetting>&
    windowLevelPresets() const noexcept;
    void addWindowLevelPreset(const QString& name, double window, double level);

    [[nodiscard]] const std::vector<RecentImageSetting>& recentImages() const noexcept;
    void addRecentImage(RecentImageSetting recent);
    bool removeRecentImage(std::size_t index);
    std::size_t removeMissingRecentImages();

    [[nodiscard]] const std::vector<ValidationScriptSetting>&
    validationScripts() const noexcept;
    void setValidationScripts(std::vector<ValidationScriptSetting> scripts);

    [[nodiscard]] const std::optional<WindowLayoutSetting>&
    windowLayout() const noexcept;
    void setWindowLayout(WindowLayoutSetting layout);

    [[nodiscard]] QString settingsFilePath() const;
    [[nodiscard]] QString thumbnailDirectoryPath() const;

private:
    void load();
    void addValidationPresetsIfNeeded();
    void save() const;

    bool darkTheme_ = false;
    bool keepWindowOnTop_ = false;
    std::optional<WindowLevelSetting> defaultWindowLevel_;
    std::vector<WindowLevelSetting> windowLevelPresets_;
    std::vector<RecentImageSetting> recentImages_;
    std::vector<ValidationScriptSetting> validationScripts_;
    std::optional<WindowLayoutSetting> windowLayout_;
    bool validationPresetsAdded_ = false;
    QString settingsFilePathOverride_;
};

} // namespace radmarky::app
