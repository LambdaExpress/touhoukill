#ifndef _CARD_OVERVIEW_H
#define _CARD_OVERVIEW_H

#include "card.h"

#include <QDialog>
#include <QPixmap>
#include <QTableWidgetItem>

class MainWindow;
class QLineEdit;
namespace Ui {
class CardOverview;
}

class CardOverview : public QDialog
{
    Q_OBJECT

public:
    static CardOverview *getInstance(QWidget *main_window);

    explicit CardOverview(QWidget *parent = nullptr);
    void loadFromAll();
    void loadFromList(const QList<const Card *> &list);

    ~CardOverview() override;

private:
    Ui::CardOverview *ui;

    void addCard(int i, const Card *card);

#ifdef Q_OS_ANDROID
    QLineEdit *mobile_search = nullptr;
    QPixmap mobile_portrait;
    void setupMobileLayout();
    void filterMobileCards(const QString &text);
    void updateMobilePortrait();

protected:
    void resizeEvent(QResizeEvent *event) override;
#endif

private slots:
    void on_femalePlayButton_clicked();
    void on_malePlayButton_clicked();
    void on_playAudioEffectButton_clicked();
    void on_tableWidget_itemDoubleClicked(QTableWidgetItem *item);
    void on_tableWidget_itemSelectionChanged();
    void askCard();
};

#endif
