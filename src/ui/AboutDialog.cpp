#include "ui/AboutDialog.h"

#include "app/ApplicationInfo.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QSize>
#include <QWidget>
#include <QVBoxLayout>

#include <string_view>

namespace
{
QString fromUtf8(const std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
} // namespace

namespace radmarky::ui
{

AboutDialog::AboutDialog(QWidget* const parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("aboutDialog"));
    const QString productName = fromUtf8(app::applicationName());
    setWindowTitle(tr("About %1").arg(productName));
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);

    auto* const root = new QVBoxLayout(this);
    root->setContentsMargins(22, 22, 22, 16);
    root->setSpacing(14);

    auto* const card = new QFrame(this);
    card->setObjectName(QStringLiteral("aboutIdentityCard"));
    auto* const cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 28, 28, 24);
    cardLayout->setSpacing(5);

    auto* const icon = new QLabel(card);
    icon->setObjectName(QStringLiteral("aboutProductIcon"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setAccessibleName(tr("RadMarky application logo"));
    QIcon appIcon = windowIcon();
    if(appIcon.isNull())
    {
        appIcon = QApplication::windowIcon();
    }
    if(!appIcon.isNull())
    {
        icon->setPixmap(appIcon.pixmap(QSize(104, 104)));
        cardLayout->addWidget(icon, 0, Qt::AlignHCenter);
        cardLayout->addSpacing(8);
    }

    auto* const name = new QLabel(productName, card);
    name->setObjectName(QStringLiteral("aboutProductName"));
    name->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(name);

    auto* const version = new QLabel(
        tr("Version %1").arg(fromUtf8(app::applicationVersion())), card);
    version->setObjectName(QStringLiteral("aboutVersion"));
    version->setAlignment(Qt::AlignCenter);

    auto* const releaseDate =
        new QLabel(fromUtf8(app::applicationReleaseDate()), card);
    releaseDate->setObjectName(QStringLiteral("aboutReleaseDate"));
    releaseDate->setAlignment(Qt::AlignCenter);

    auto* const releaseMeta = new QWidget(card);
    releaseMeta->setObjectName(QStringLiteral("aboutReleaseMeta"));
    auto* const releaseMetaLayout = new QVBoxLayout(releaseMeta);
    releaseMetaLayout->setContentsMargins(0, 0, 0, 0);
    releaseMetaLayout->setSpacing(0);
    releaseMetaLayout->addWidget(version);
    releaseMetaLayout->addWidget(releaseDate);
    cardLayout->addWidget(releaseMeta);

    auto* const divider = new QFrame(card);
    divider->setObjectName(QStringLiteral("aboutDivider"));
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(divider);
    cardLayout->addSpacing(6);

    auto* const description = new QLabel(
        tr("A lightweight desktop application for reviewing 3D medical images "
           "and editing NIfTI label maps."),
        card);
    description->setObjectName(QStringLiteral("aboutDescription"));
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(description);

    auto* const creator = new QLabel(
        tr("Created by "
           "<a href=\"https://www.linkedin.com/in/omidsakhi/\">Omid Sakhi</a> "
           "and Sol Terra."),
        card);
    creator->setObjectName(QStringLiteral("aboutCreator"));
    creator->setAlignment(Qt::AlignCenter);
    creator->setTextFormat(Qt::RichText);
    creator->setOpenExternalLinks(true);
    creator->setTextInteractionFlags(Qt::TextBrowserInteraction);
    cardLayout->addWidget(creator);

    auto* const disclaimer = new QLabel(
        tr("Intended for research, education, and software evaluation. Not a "
           "certified medical device and must not be used as the sole basis "
           "for diagnosis or treatment."),
        card);
    disclaimer->setObjectName(QStringLiteral("aboutDisclaimer"));
    disclaimer->setWordWrap(true);
    disclaimer->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(disclaimer);

    auto* const license = new QLabel(
        tr("Licensed under the GNU General Public License v3.0."), card);
    license->setObjectName(QStringLiteral("aboutLicense"));
    license->setWordWrap(true);
    license->setAlignment(Qt::AlignCenter);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(license);

    auto* const copyright =
        new QLabel(fromUtf8(app::copyrightNotice()), card);
    copyright->setObjectName(QStringLiteral("aboutCopyright"));
    copyright->setWordWrap(true);
    copyright->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(copyright);

    auto* const links = new QLabel(
        QStringLiteral(
            "<a href=\"https://www.tensorharmony.com\">tensorharmony.com</a>"
            "&nbsp;&nbsp;·&nbsp;&nbsp;"
            "<a href=\"https://github.com/TensorHarmony/radmarky-viewer\">Source</a>"),
        card);
    links->setObjectName(QStringLiteral("aboutLinks"));
    links->setAlignment(Qt::AlignCenter);
    links->setTextFormat(Qt::RichText);
    links->setOpenExternalLinks(true);
    links->setTextInteractionFlags(Qt::TextBrowserInteraction);
    cardLayout->addWidget(links);

    root->addWidget(card);

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("aboutButtons"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    setFixedWidth(460);
    adjustSize();
}

} // namespace radmarky::ui
