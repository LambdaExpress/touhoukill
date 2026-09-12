#ifndef _SKILL_OVERVIEW_H
#define _SKILL_OVERVIEW_H

class ClientPlayer;
class General;
class QLabel;
class QVBoxLayout;

#include <QDialog>

class SkillOverview : public QDialog
{
    Q_OBJECT

public:
    explicit SkillOverview(QWidget *parent = nullptr);
    void setPlayer(const ClientPlayer *player);
    void setGeneral(const General *general, const QString &heading = QString());

private:
    void showGenerals(const QList<const General *> &generals, const QString &heading);
    void addGeneral(const General *general);

    QLabel *m_heading;
    QVBoxLayout *m_content;
};

#endif // _SKILL_OVERVIEW_H
