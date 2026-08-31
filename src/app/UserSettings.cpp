#include "app/UserSettings.h"

#include "core/WindowLevel.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace radmarky::app
{
namespace
{

constexpr int schemaVersion = 1;
constexpr std::size_t maximumRecentImages = 8;
constexpr int minimumBrushRadius = 1;
constexpr int maximumBrushRadius = 100;
constexpr int maximumLabel = 65535;

QList<int> positiveIntList(const QJsonValue& value)
{
    QList<int> sizes;
    for(const auto item : value.toArray())
    {
        const int size = item.toInt();
        if(item.isDouble() && size > 0)
        {
            sizes.push_back(size);
        }
    }
    return sizes;
}

QJsonArray intListJson(const QList<int>& sizes)
{
    QJsonArray array;
    for(const int size : sizes)
    {
        array.push_back(size);
    }
    return array;
}

QByteArray bytesFromBase64(const QJsonValue& value)
{
    return QByteArray::fromBase64(value.toString().toLatin1());
}

QString focusedViewName(const QString& value)
{
    QString focused = value.trimmed().toLower();
    if(focused == QStringLiteral("axial")
       || focused == QStringLiteral("sagittal")
       || focused == QStringLiteral("coronal"))
    {
        return focused;
    }
    return {};
}

bool validWindowLevel(const double window, const double level)
{
    return std::isfinite(window) && std::isfinite(level)
        && window >= core::WindowLevel::minimumWindow;
}

QString recentKey(const QStringList& files)
{
    QStringList normalized;
    normalized.reserve(files.size());
    for(const auto& file : files)
    {
        normalized.push_back(QDir::cleanPath(file).toCaseFolded());
    }
    return normalized.join(QChar(0x001f));
}

} // namespace

UserSettings::UserSettings(QString settingsFilePath)
    : settingsFilePathOverride_(std::move(settingsFilePath))
{
    load();
    addValidationPresetsIfNeeded();
}

bool UserSettings::darkTheme() const noexcept
{
    return darkTheme_;
}

void UserSettings::setDarkTheme(const bool dark)
{
    if(darkTheme_ == dark)
    {
        return;
    }
    darkTheme_ = dark;
    save();
}

bool UserSettings::keepWindowOnTop() const noexcept
{
    return keepWindowOnTop_;
}

void UserSettings::setKeepWindowOnTop(const bool keepOnTop)
{
    if(keepWindowOnTop_ == keepOnTop)
    {
        return;
    }
    keepWindowOnTop_ = keepOnTop;
    save();
}

const std::optional<WindowLevelSetting>&
UserSettings::defaultWindowLevel() const noexcept
{
    return defaultWindowLevel_;
}

void UserSettings::setDefaultWindowLevel(const double window, const double level)
{
    if(!validWindowLevel(window, level))
    {
        return;
    }
    defaultWindowLevel_ = WindowLevelSetting{QString{}, window, level};
    save();
}

const std::vector<WindowLevelSetting>&
UserSettings::windowLevelPresets() const noexcept
{
    return windowLevelPresets_;
}

void UserSettings::addWindowLevelPreset(
    const QString& name,
    const double window,
    const double level)
{
    const QString trimmedName = name.trimmed();
    if(trimmedName.isEmpty() || !validWindowLevel(window, level))
    {
        return;
    }
    const auto existing = std::find_if(
        windowLevelPresets_.begin(),
        windowLevelPresets_.end(),
        [&trimmedName](const WindowLevelSetting& preset) {
            return preset.name.compare(trimmedName, Qt::CaseInsensitive) == 0;
        });
    const WindowLevelSetting value{trimmedName, window, level};
    if(existing == windowLevelPresets_.end())
    {
        windowLevelPresets_.push_back(value);
    }
    else
    {
        *existing = value;
    }
    save();
}

int UserSettings::brushRadius(const int label) const noexcept
{
    const auto found = brushRadii_.find(label);
    return found != brushRadii_.end() ? found->second : minimumBrushRadius;
}

const std::map<int, int>& UserSettings::brushRadii() const noexcept
{
    return brushRadii_;
}

void UserSettings::setBrushRadius(const int label, const int radius)
{
    if(label < 0 || label > maximumLabel
       || radius < minimumBrushRadius || radius > maximumBrushRadius)
    {
        return;
    }
    const auto found = brushRadii_.find(label);
    if(found != brushRadii_.end() && found->second == radius)
    {
        return;
    }
    brushRadii_[label] = radius;
    save();
}

int UserSettings::paintOverSelection(const int label) const noexcept
{
    const auto found = paintOverSelections_.find(label);
    return found != paintOverSelections_.end() ? found->second : -1;
}

const std::map<int, int>& UserSettings::paintOverSelections() const noexcept
{
    return paintOverSelections_;
}

void UserSettings::setPaintOverSelection(
    const int label, const int selection)
{
    if(label < 0 || label > maximumLabel
       || selection < -1 || selection > maximumLabel)
    {
        return;
    }
    const auto found = paintOverSelections_.find(label);
    if(found != paintOverSelections_.end() && found->second == selection)
    {
        return;
    }
    paintOverSelections_[label] = selection;
    save();
}

const std::vector<RecentImageSetting>& UserSettings::recentImages() const noexcept
{
    return recentImages_;
}

void UserSettings::addRecentImage(RecentImageSetting recent)
{
    if(recent.sourceFiles.isEmpty())
    {
        return;
    }
    const QString key = recentKey(recent.sourceFiles);
    std::erase_if(recentImages_, [&key](const RecentImageSetting& candidate) {
        return recentKey(candidate.sourceFiles) == key;
    });
    recentImages_.insert(recentImages_.begin(), std::move(recent));
    if(recentImages_.size() > maximumRecentImages)
    {
        recentImages_.resize(maximumRecentImages);
    }
    save();
}

bool UserSettings::removeRecentImage(const std::size_t index)
{
    if(index >= recentImages_.size())
    {
        return false;
    }
    recentImages_.erase(
        recentImages_.begin()
        + static_cast<std::vector<RecentImageSetting>::difference_type>(index));
    save();
    return true;
}

std::size_t UserSettings::removeMissingRecentImages()
{
    const auto removed = std::erase_if(
        recentImages_,
        [](const RecentImageSetting& recent) {
            return recent.sourceFiles.isEmpty()
                || std::any_of(
                    recent.sourceFiles.begin(),
                    recent.sourceFiles.end(),
                    [](const QString& path) { return !QFileInfo(path).isFile(); });
        });
    if(removed > 0)
    {
        save();
    }
    return removed;
}

const std::vector<ValidationScriptSetting>&
UserSettings::validationScripts() const noexcept
{
    return validationScripts_;
}

void UserSettings::setValidationScripts(
    std::vector<ValidationScriptSetting> scripts)
{
    std::erase_if(scripts, [](const ValidationScriptSetting& script) {
        return script.path.trimmed().isEmpty();
    });
    validationScripts_ = std::move(scripts);
    save();
}

const std::optional<WindowLayoutSetting>&
UserSettings::windowLayout() const noexcept
{
    return windowLayout_;
}

void UserSettings::setWindowLayout(WindowLayoutSetting layout)
{
    if(windowLayout_ && *windowLayout_ == layout)
    {
        return;
    }
    windowLayout_ = std::move(layout);
    save();
}

QString UserSettings::settingsFilePath() const
{
    if(!settingsFilePathOverride_.isEmpty())
    {
        return settingsFilePathOverride_;
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("settings.json"));
}

QString UserSettings::thumbnailDirectoryPath() const
{
    if(!settingsFilePathOverride_.isEmpty())
    {
        return QDir(QFileInfo(settingsFilePathOverride_).absolutePath())
            .filePath(QStringLiteral("recent-thumbnails"));
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("recent-thumbnails"));
}

void UserSettings::load()
{
    QFile file(settingsFilePath());
    if(!file.exists())
    {
        return;
    }
    if(!file.open(QIODevice::ReadOnly))
    {
        qWarning().noquote() << "[SETTINGS] Unable to read" << file.fileName();
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if(error.error != QJsonParseError::NoError || !document.isObject())
    {
        qWarning().noquote()
            << "[SETTINGS] Ignoring invalid JSON:" << error.errorString();
        return;
    }

    const QJsonObject root = document.object();
    if(root.value(QStringLiteral("schemaVersion")).toInt() != schemaVersion)
    {
        qWarning() << "[SETTINGS] Ignoring unsupported settings schema";
        return;
    }
    darkTheme_ = root.value(QStringLiteral("darkTheme")).toBool(false);
    keepWindowOnTop_ =
        root.value(QStringLiteral("keepWindowOnTop")).toBool(false);
    validationPresetsAdded_ =
        root.value(QStringLiteral("validationPresetsAdded")).toBool(false);

    const QJsonObject brushRadii =
        root.value(QStringLiteral("brushRadii")).toObject();
    for(auto value = brushRadii.constBegin(); value != brushRadii.constEnd(); ++value)
    {
        bool validLabel = false;
        const int label = value.key().toInt(&validLabel);
        const int radius = value.value().toInt();
        if(validLabel && label >= 0 && label <= maximumLabel
           && value.value().isDouble()
           && radius >= minimumBrushRadius && radius <= maximumBrushRadius)
        {
            brushRadii_[label] = radius;
        }
    }

    const QJsonObject paintOverSelections =
        root.value(QStringLiteral("paintOverSelections")).toObject();
    for(auto value = paintOverSelections.constBegin();
        value != paintOverSelections.constEnd(); ++value)
    {
        bool validLabel = false;
        const int label = value.key().toInt(&validLabel);
        const int selection = value.value().toInt(-2);
        if(validLabel && label >= 0 && label <= maximumLabel
           && value.value().isDouble()
           && selection >= -1 && selection <= maximumLabel)
        {
            paintOverSelections_[label] = selection;
        }
    }

    const auto readWindowLevel = [](const QJsonObject& object,
                                    const bool requireName)
        -> std::optional<WindowLevelSetting> {
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        const double window = object.value(QStringLiteral("window")).toDouble(0.0);
        const double level = object.value(QStringLiteral("level")).toDouble(0.0);
        if((requireName && name.isEmpty()) || !validWindowLevel(window, level))
        {
            return std::nullopt;
        }
        return WindowLevelSetting{name, window, level};
    };

    if(root.value(QStringLiteral("defaultWindowLevel")).isObject())
    {
        defaultWindowLevel_ = readWindowLevel(
            root.value(QStringLiteral("defaultWindowLevel")).toObject(), false);
    }
    for(const auto value : root.value(QStringLiteral("windowLevelPresets")).toArray())
    {
        if(value.isObject())
        {
            if(const auto preset = readWindowLevel(value.toObject(), true))
            {
                windowLevelPresets_.push_back(*preset);
            }
        }
    }
    for(const auto value : root.value(QStringLiteral("recentImages")).toArray())
    {
        if(!value.isObject())
        {
            continue;
        }
        const QJsonObject object = value.toObject();
        RecentImageSetting recent;
        recent.name = object.value(QStringLiteral("name")).toString();
        recent.kind = object.value(QStringLiteral("kind")).toString();
        recent.thumbnailPath = object.value(QStringLiteral("thumbnail")).toString();
        recent.thumbnailWidth =
            object.value(QStringLiteral("thumbnailWidth")).toInt();
        recent.thumbnailHeight =
            object.value(QStringLiteral("thumbnailHeight")).toInt();
        recent.thumbnailPixels = QByteArray::fromBase64(
            object.value(QStringLiteral("thumbnailPixels")).toString().toLatin1());
        const qint64 expectedThumbnailBytes =
            static_cast<qint64>(recent.thumbnailWidth) * recent.thumbnailHeight;
        if(recent.thumbnailWidth <= 0 || recent.thumbnailHeight <= 0
           || expectedThumbnailBytes != recent.thumbnailPixels.size())
        {
            recent.thumbnailPixels.clear();
            recent.thumbnailWidth = 0;
            recent.thumbnailHeight = 0;
        }
        for(const auto path : object.value(QStringLiteral("sourceFiles")).toArray())
        {
            if(path.isString())
            {
                recent.sourceFiles.push_back(path.toString());
            }
        }
        for(const auto annotationValue :
            object.value(QStringLiteral("annotations")).toArray())
        {
            RecentAnnotationSetting annotation;
            if(annotationValue.isString())
            {
                annotation.sourceFile = annotationValue.toString().trimmed();
            }
            else if(annotationValue.isObject())
            {
                const auto annotationObject = annotationValue.toObject();
                annotation.sourceFile =
                    annotationObject.value(QStringLiteral("sourceFile"))
                        .toString()
                        .trimmed();
                const double opacity =
                    annotationObject.value(QStringLiteral("opacity")).toDouble(0.5);
                if(std::isfinite(opacity) && opacity >= 0.0 && opacity <= 1.0)
                {
                    annotation.opacity = opacity;
                }
            }
            if(!annotation.sourceFile.isEmpty())
            {
                recent.annotations.push_back(std::move(annotation));
            }
        }
        const int activeLabel =
            object.value(QStringLiteral("activeLabel")).toInt(1);
        if(activeLabel >= 0 && activeLabel <= maximumLabel)
        {
            recent.activeLabel = activeLabel;
        }
        if(!recent.sourceFiles.isEmpty())
        {
            recentImages_.push_back(std::move(recent));
        }
        if(recentImages_.size() == maximumRecentImages)
        {
            break;
        }
    }
    for(const auto value : root.value(QStringLiteral("validationScripts")).toArray())
    {
        if(!value.isObject())
        {
            continue;
        }
        const QJsonObject object = value.toObject();
        ValidationScriptSetting script;
        script.name = object.value(QStringLiteral("name")).toString().trimmed();
        script.path = object.value(QStringLiteral("path")).toString().trimmed();
        script.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        if(script.path.isEmpty())
        {
            continue;
        }
        if(script.name.isEmpty())
        {
            script.name = QFileInfo(script.path).completeBaseName();
        }
        validationScripts_.push_back(std::move(script));
    }
    if(root.value(QStringLiteral("windowLayout")).isObject())
    {
        const QJsonObject object =
            root.value(QStringLiteral("windowLayout")).toObject();
        WindowLayoutSetting layout;
        layout.geometry =
            bytesFromBase64(object.value(QStringLiteral("geometry")));
        layout.windowState =
            bytesFromBase64(object.value(QStringLiteral("windowState")));
        layout.viewSplitterSizes =
            positiveIntList(object.value(QStringLiteral("viewSplitterSizes")));
        layout.rightViewSplitterSizes = positiveIntList(
            object.value(QStringLiteral("rightViewSplitterSizes")));
        layout.focusedView =
            focusedViewName(object.value(QStringLiteral("focusedView")).toString());
        if(!layout.geometry.isEmpty() || !layout.windowState.isEmpty()
           || !layout.viewSplitterSizes.isEmpty()
           || !layout.rightViewSplitterSizes.isEmpty()
           || !layout.focusedView.isEmpty())
        {
            windowLayout_ = std::move(layout);
        }
    }
}

void UserSettings::addValidationPresetsIfNeeded()
{
    if(validationPresetsAdded_)
    {
        return;
    }
    const std::array presets{
        ValidationScriptSetting{
            QStringLiteral("Example: Slice continuity"),
            QStringLiteral(":/validation-presets/continuity.py"),
            false},
        ValidationScriptSetting{
            QStringLiteral("Example: Non-empty annotation"),
            QStringLiteral(":/validation-presets/non_empty.py"),
            false},
        ValidationScriptSetting{
            QStringLiteral("Example: Allowed labels"),
            QStringLiteral(":/validation-presets/allowed_labels.py"),
            false},
    };
    for(const auto& preset : presets)
    {
        const auto existing = std::find_if(
            validationScripts_.begin(), validationScripts_.end(),
            [&preset](const ValidationScriptSetting& script) {
                return script.path == preset.path;
            });
        if(existing == validationScripts_.end())
        {
            validationScripts_.push_back(preset);
        }
    }
    validationPresetsAdded_ = true;
    save();
}

void UserSettings::save() const
{
    const QString path = settingsFilePath();
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        qWarning().noquote() << "[SETTINGS] Unable to create settings directory";
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), schemaVersion);
    root.insert(QStringLiteral("darkTheme"), darkTheme_);
    root.insert(QStringLiteral("keepWindowOnTop"), keepWindowOnTop_);
    root.insert(QStringLiteral("validationPresetsAdded"), validationPresetsAdded_);
    QJsonObject brushRadii;
    for(const auto& [label, radius] : brushRadii_)
    {
        brushRadii.insert(QString::number(label), radius);
    }
    root.insert(QStringLiteral("brushRadii"), brushRadii);
    QJsonObject paintOverSelections;
    for(const auto& [label, selection] : paintOverSelections_)
    {
        paintOverSelections.insert(QString::number(label), selection);
    }
    root.insert(QStringLiteral("paintOverSelections"), paintOverSelections);
    if(defaultWindowLevel_)
    {
        QJsonObject value;
        value.insert(QStringLiteral("window"), defaultWindowLevel_->window);
        value.insert(QStringLiteral("level"), defaultWindowLevel_->level);
        root.insert(QStringLiteral("defaultWindowLevel"), value);
    }
    QJsonArray presets;
    for(const auto& preset : windowLevelPresets_)
    {
        QJsonObject value;
        value.insert(QStringLiteral("name"), preset.name);
        value.insert(QStringLiteral("window"), preset.window);
        value.insert(QStringLiteral("level"), preset.level);
        presets.push_back(value);
    }
    root.insert(QStringLiteral("windowLevelPresets"), presets);

    QJsonArray recents;
    for(const auto& recent : recentImages_)
    {
        QJsonObject value;
        value.insert(QStringLiteral("name"), recent.name);
        value.insert(QStringLiteral("kind"), recent.kind);
        value.insert(QStringLiteral("thumbnail"), recent.thumbnailPath);
        if(!recent.thumbnailPixels.isEmpty() && recent.thumbnailWidth > 0
           && recent.thumbnailHeight > 0)
        {
            value.insert(
                QStringLiteral("thumbnailPixels"),
                QString::fromLatin1(recent.thumbnailPixels.toBase64()));
            value.insert(
                QStringLiteral("thumbnailWidth"), recent.thumbnailWidth);
            value.insert(
                QStringLiteral("thumbnailHeight"), recent.thumbnailHeight);
        }
        QJsonArray files;
        for(const auto& file : recent.sourceFiles)
        {
            files.push_back(file);
        }
        value.insert(QStringLiteral("sourceFiles"), files);
        QJsonArray annotations;
        for(const auto& annotation : recent.annotations)
        {
            if(annotation.sourceFile.trimmed().isEmpty())
            {
                continue;
            }
            QJsonObject annotationValue;
            annotationValue.insert(
                QStringLiteral("sourceFile"), annotation.sourceFile);
            annotationValue.insert(QStringLiteral("opacity"), annotation.opacity);
            annotations.push_back(annotationValue);
        }
        value.insert(QStringLiteral("annotations"), annotations);
        value.insert(QStringLiteral("activeLabel"), recent.activeLabel);
        recents.push_back(value);
    }
    root.insert(QStringLiteral("recentImages"), recents);

    QJsonArray validators;
    for(const auto& script : validationScripts_)
    {
        QJsonObject value;
        value.insert(QStringLiteral("name"), script.name);
        value.insert(QStringLiteral("path"), script.path);
        value.insert(QStringLiteral("enabled"), script.enabled);
        validators.push_back(value);
    }
    root.insert(QStringLiteral("validationScripts"), validators);
    if(windowLayout_)
    {
        QJsonObject value;
        value.insert(
            QStringLiteral("geometry"),
            QString::fromLatin1(windowLayout_->geometry.toBase64()));
        value.insert(
            QStringLiteral("windowState"),
            QString::fromLatin1(windowLayout_->windowState.toBase64()));
        value.insert(
            QStringLiteral("viewSplitterSizes"),
            intListJson(windowLayout_->viewSplitterSizes));
        value.insert(
            QStringLiteral("rightViewSplitterSizes"),
            intListJson(windowLayout_->rightViewSplitterSizes));
        value.insert(QStringLiteral("focusedView"), windowLayout_->focusedView);
        root.insert(QStringLiteral("windowLayout"), value);
    }

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
       || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
       || !file.flush())
    {
        qWarning().noquote() << "[SETTINGS] Unable to write" << path;
    }
}

} // namespace radmarky::app
