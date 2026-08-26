#include "core/Annotation.h"
#include "core/Volume.h"
#include "validation/AnnotationValidationService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUuid>

#include <iostream>
#include <memory>
#include <filesystem>

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

QString writeScript(
    const QString& directory,
    const QString& name,
    const QByteArray& source)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
       || file.write(source) != source.size())
    {
        return {};
    }
    return path;
}

std::shared_ptr<radmarky::core::Volume> makePrimary()
{
    auto image = radmarky::core::Volume::ImageType::New();
    radmarky::core::Volume::ImageType::SizeType size;
    size[0] = 3;
    size[1] = 3;
    size[2] = 2;
    image->SetRegions(size);
    image->Allocate();
    image->FillBuffer(0.0F);
    return std::make_shared<radmarky::core::Volume>(image);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const auto directoryPath = std::filesystem::temp_directory_path()
        / ("radmarky-service-"
           + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    std::filesystem::create_directories(directoryPath);
    const QString directory = QString::fromStdWString(directoryPath.wstring());
    auto annotation = radmarky::core::Annotation::createBlankLabelMap(
        "Unsaved edits", *makePrimary());
    const QString passing = writeScript(
        directory, QStringLiteral("pass.py"),
        "import os\n"
        "def validate(annotation_path, context):\n"
        "    return None if os.path.isfile(annotation_path) else 'snapshot missing'\n");
    const QString firstFailure = writeScript(
        directory, QStringLiteral("first.py"),
        "def validate(annotation_path, context):\n    return 'first error'\n");
    const QString secondFailure = writeScript(
        directory, QStringLiteral("second.py"),
        "def validate(annotation_path, context):\n    return 'second error'\n");

    radmarky::validation::AnnotationValidationService service;
    radmarky::validation::AnnotationValidationContext context;
    const QString output = QDir(directory).filePath(QStringLiteral("output.nii.gz"));
    bool passed = true;
    auto saved = service.validateAndSave(
        *annotation,
        output,
        {{QStringLiteral("Pass"), passing, true}},
        context);
    passed &= expect(saved.accepted() && saved.saved, "passing validator saves");
    passed &= expect(QFileInfo::exists(output), "saved candidate finalized");
    passed &= expect(annotation->isModified(), "service does not clear dirty state");

    QFile existing(output);
    passed &= expect(
        existing.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && existing.write("ORIGINAL") == 8,
        "prepare existing destination");
    existing.close();
    auto rejected = service.validateAndSave(
        *annotation,
        output,
        {
            {QStringLiteral("First"), firstFailure, true},
            {QStringLiteral("Disabled"), passing, false},
            {QStringLiteral("Second"), secondFailure, true},
        },
        context);
    passed &= expect(!rejected.accepted() && !rejected.saved, "failure blocks save");
    passed &= expect(rejected.scripts.size() == 2, "all enabled scripts run");
    if(rejected.scripts.size() == 2)
    {
        passed &= expect(
            rejected.scripts[0].message == QStringLiteral("first error")
                && rejected.scripts[1].message == QStringLiteral("second error"),
            "failures retain stable order");
    }
    passed &= expect(existing.open(QIODevice::ReadOnly), "reopen destination");
    passed &= expect(existing.readAll() == QByteArray("ORIGINAL"),
                     "rejected save leaves destination unchanged");
    existing.close();

    auto manual = service.validateOnly(
        *annotation,
        {{QStringLiteral("Pass"), passing, true}},
        context);
    passed &= expect(manual.accepted() && !manual.saved, "manual validation does not save");
    auto empty = service.validateOnly(*annotation, {}, context);
    passed &= expect(empty.accepted() && empty.scripts.empty(), "empty set accepts");
    std::filesystem::remove_all(directoryPath);
    return passed ? 0 : 1;
}
