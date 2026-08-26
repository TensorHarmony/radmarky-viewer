#include "app/ApplicationInfo.h"
#include "ui/AboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>

#include <iostream>
#include <string_view>

namespace
{

bool expectTrue(const bool condition, const std::string_view field)
{
    if(condition)
    {
        return true;
    }
    std::cerr << field << ": expected true\n";
    return false;
}

QString fromUtf8(const std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    radmarky::ui::AboutDialog dialog;

    auto* const name = dialog.findChild<QLabel*>(QStringLiteral("aboutProductName"));
    auto* const version = dialog.findChild<QLabel*>(QStringLiteral("aboutVersion"));
    auto* const releaseDate =
        dialog.findChild<QLabel*>(QStringLiteral("aboutReleaseDate"));
    auto* const copyright =
        dialog.findChild<QLabel*>(QStringLiteral("aboutCopyright"));
    auto* const creator =
        dialog.findChild<QLabel*>(QStringLiteral("aboutCreator"));
    auto* const license = dialog.findChild<QLabel*>(QStringLiteral("aboutLicense"));
    auto* const links = dialog.findChild<QLabel*>(QStringLiteral("aboutLinks"));
    auto* const card = dialog.findChild<QFrame*>(QStringLiteral("aboutIdentityCard"));
    auto* const buttons =
        dialog.findChild<QDialogButtonBox*>(QStringLiteral("aboutButtons"));

    bool passed = expectTrue(dialog.objectName() == QStringLiteral("aboutDialog"),
                             "dialog object name")
        && expectTrue(dialog.isModal(), "dialog modality")
        && expectTrue(name != nullptr, "product name")
        && expectTrue(version != nullptr, "version")
        && expectTrue(releaseDate != nullptr, "release date")
        && expectTrue(copyright != nullptr, "copyright")
        && expectTrue(creator != nullptr, "creator credit")
        && expectTrue(license != nullptr, "license")
        && expectTrue(links != nullptr, "links")
        && expectTrue(card != nullptr, "identity card")
        && expectTrue(buttons != nullptr, "close buttons");
    if(!passed)
    {
        return 1;
    }

    passed &= expectTrue(
        dialog.windowTitle()
            == QStringLiteral("About %1").arg(
                fromUtf8(radmarky::app::applicationName())),
        "window title");
    passed &= expectTrue(
        name->text() == fromUtf8(radmarky::app::applicationName()),
        "product name text");
    passed &= expectTrue(
        version->text().contains(
            fromUtf8(radmarky::app::applicationVersion())),
        "version text");
    passed &= expectTrue(
        releaseDate->text() == fromUtf8(radmarky::app::applicationReleaseDate()),
        "release date text");
    passed &= expectTrue(
        copyright->text() == fromUtf8(radmarky::app::copyrightNotice()),
        "copyright text");
    passed &= expectTrue(
        license->text().contains(QStringLiteral("GNU General Public License v3.0")),
        "license text");
    passed &= expectTrue(
        creator->text().contains(QStringLiteral("Created by"))
            && creator->text().contains(QStringLiteral("Omid Sakhi"))
            && creator->text().contains(QStringLiteral("Sol Terra"))
            && creator->text().contains(
                QStringLiteral("linkedin.com/in/omidsakhi/")),
        "creator LinkedIn profile");
    passed &= expectTrue(
        links->text().contains(QStringLiteral("tensorharmony.com")),
        "company website");
    passed &= expectTrue(
        links->text().contains(
            QStringLiteral("github.com/TensorHarmony/radmarky-viewer")),
        "source repository");
    passed &= expectTrue(
        buttons->standardButtons() == QDialogButtonBox::Close, "close button");

    return passed ? 0 : 1;
}
