#include "skilloverview.h"

#include "SkinBank.h"
#include "client.h"
#include "clientplayer.h"
#include "clientstruct.h"
#include "dialogsupport.h"
#include "engine.h"
#include "settings.h"
#include "skill.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            delete widget;
        if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
}

} // namespace

SkillOverview::SkillOverview(QWidget *parent)
    : QDialog(parent)
    , m_heading(nullptr)
    , m_content(nullptr)
{
    setWindowTitle(tr("Character skills"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 8);

    QHBoxLayout *header = new QHBoxLayout;
    QPushButton *back = new QPushButton(tr("Back"));
    connect(back, &QPushButton::clicked, this, &QDialog::reject);
    header->addWidget(back);

    m_heading = new QLabel(windowTitle());
    m_heading->setProperty("sgsHeading", true);
    m_heading->setWordWrap(true);
    header->addWidget(m_heading, 1);
    root->addLayout(header);

    QWidget *page = new QWidget;
    m_content = new QVBoxLayout(page);
    m_content->setContentsMargins(0, 0, 0, 0);
    root->addWidget(DialogSupport::createScrollArea(page), 1);

#ifdef Q_OS_ANDROID
    setProperty("sgsMobileLayout", true);
    setMinimumSize(0, 0);
#endif
}

void SkillOverview::setPlayer(const ClientPlayer *player)
{
    if (player == nullptr)
        return;

    const QString player_name = ClientInstance->getPlayerName(player->objectName());

    QList<const General *> generals;
    generals << player->getGeneral() << player->getGeneral2();
    showGenerals(generals, player_name.isEmpty() ? windowTitle() : player_name);
}

void SkillOverview::setGeneral(const General *general, const QString &heading)
{
    QList<const General *> generals;
    generals << general;
    showGenerals(generals, heading.isEmpty() ? windowTitle() : heading);
}

void SkillOverview::showGenerals(const QList<const General *> &generals, const QString &heading)
{
    clearLayout(m_content);
    m_heading->setText(heading);

    QList<const General *> present;
    foreach (const General *general, generals) {
        if (general != nullptr)
            present << general;
    }

    if (present.isEmpty()) {
        QLabel *label = new QLabel(tr("The character is not revealed yet"));
        label->setWordWrap(true);
        m_content->addWidget(label);
        m_content->addStretch();
        return;
    }

    bool first = true;
    foreach (const General *current, present) {
        if (!first) {
            QFrame *separator = new QFrame;
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Sunken);
            m_content->addWidget(separator);
        }
        addGeneral(current);
        first = false;
    }
    m_content->addStretch();
}

void SkillOverview::addGeneral(const General *general)
{
    QHBoxLayout *row = new QHBoxLayout;

    QLabel *portrait = new QLabel;
    const QPixmap portrait_pixmap = G_ROOM_SKIN.getCardMainPixmap(general->objectName());
    if (!portrait_pixmap.isNull()) {
        const int height = 220;
        const int width = portrait_pixmap.height() > 0 ? portrait_pixmap.width() * height / portrait_pixmap.height() : height;
        portrait->setPixmap(portrait_pixmap.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    row->addWidget(portrait, 0, Qt::AlignTop);

    QVBoxLayout *detail = new QVBoxLayout;

    QString general_name = Sanguosha->translate("!" + general->objectName());
    if (general_name.startsWith("!"))
        general_name = Sanguosha->translate(general->objectName());
    QLabel *name_label = new QLabel(general_name);
    name_label->setProperty("sgsHeading", true);
    name_label->setWordWrap(true);
    detail->addWidget(name_label);

    int max_hp = general->getMaxHp();
    if (general->objectName().endsWith("_hegemony"))
        max_hp /= 2;
    QLabel *info_label = new QLabel(Sanguosha->translate(general->getKingdom()) + " · " + tr("MaxHP %1").arg(max_hp));
    info_label->setWordWrap(true);
    detail->addWidget(info_label);

    const bool is_hegemony = isHegemonyGameMode(ServerInfo.GameMode);

    QList<const Skill *> skills = general->getVisibleSkillList();
    foreach (QString skill_name, general->getRelatedSkillNames()) {
        const Skill *skill = Sanguosha->getSkill(skill_name);
        if (skill != nullptr && skill->isVisible())
            skills << skill;
    }

    foreach (const Skill *skill, skills) {
        QLabel *skill_name_label = new QLabel(Sanguosha->translate(skill->objectName()));
        skill_name_label->setStyleSheet("color:#edce8c;font-weight:bold;");
        skill_name_label->setWordWrap(true);
        detail->addWidget(skill_name_label);

        const QString description = skill->getDescription(true, is_hegemony);
        if (!description.isEmpty()) {
            QLabel *description_label = new QLabel;
            description_label->setTextFormat(Qt::RichText);
            description_label->setWordWrap(true);
            description_label->setText(description);
            detail->addWidget(description_label);
        }
    }

    row->addLayout(detail, 1);
    m_content->addLayout(row);
}
