#include "cardoverview.h"
#include "SkinBank.h"
#include "client.h"
#include "clientstruct.h"
#include "dialogsupport.h"
#include "engine.h"
#include "settings.h"
#include "ui_cardoverview.h"

#include <QFile>
#include <QMessageBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTimer>

static CardOverview *Overview;

CardOverview *CardOverview::getInstance(QWidget *main_window)
{
    if (Overview == nullptr)
        Overview = new CardOverview(main_window);

    return Overview;
}

CardOverview::CardOverview(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CardOverview)
{
    ui->setupUi(this);

    ui->tableWidget->setColumnWidth(0, 80);
    ui->tableWidget->setColumnWidth(1, 60);
    ui->tableWidget->setColumnWidth(2, 30);
    ui->tableWidget->setColumnWidth(3, 60);
    ui->tableWidget->setColumnWidth(4, 70);

    ui->tableWidget->setSortingEnabled(false);

    if (ServerInfo.EnableCheat)
        connect(ui->getCardButton, SIGNAL(clicked()), this, SLOT(askCard()));
    else
        ui->getCardButton->hide();

    ui->cardDescriptionBox->setProperty("description", true);
    ui->malePlayButton->hide();
    ui->femalePlayButton->hide();
    ui->playAudioEffectButton->hide();
#ifdef Q_OS_ANDROID
    setupMobileLayout();
#endif
}

#ifdef Q_OS_ANDROID
void CardOverview::setupMobileLayout()
{
    setProperty("sgsMobileLayout", true);
    QWidget *desktop = new QWidget(this);
    desktop->setLayout(layout());
    desktop->hide();
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 8);
    QHBoxLayout *header = new QHBoxLayout;
    QPushButton *back = new QPushButton(tr("Back"));
    connect(back, &QPushButton::clicked, this, &QDialog::reject);
    header->addWidget(back);
    QLabel *title = new QLabel(windowTitle());
    title->setProperty("sgsHeading", true);
    header->addWidget(title);
    mobile_search = new QLineEdit;
    mobile_search->setPlaceholderText(tr("Search cards"));
    mobile_search->setClearButtonEnabled(true);
    header->addWidget(mobile_search, 1);
    root->addLayout(header);
    QHBoxLayout *body = new QHBoxLayout;
    ui->tableWidget->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->tableWidget->setMinimumSize(0, 0);
    ui->tableWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    ui->tableWidget->setShowGrid(false);
    ui->tableWidget->setWordWrap(false);
    ui->tableWidget->setProperty("sgsRowHeight", 64);
    ui->tableWidget->horizontalHeader()->hide();
    ui->tableWidget->verticalHeader()->hide();
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (int i = 1; i < ui->tableWidget->columnCount(); ++i)
        ui->tableWidget->hideColumn(i);
    body->addWidget(ui->tableWidget, 3);
    ui->cardLabel->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->cardLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    ui->cardLabel->setAlignment(Qt::AlignCenter);
    body->addWidget(ui->cardLabel, 2);
    QVBoxLayout *detail = new QVBoxLayout;
    ui->cardDescriptionBox->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->cardDescriptionBox->setMinimumSize(0, 0);
    detail->addWidget(ui->cardDescriptionBox, 1);
    QHBoxLayout *audio = new QHBoxLayout;
    const QList<QPushButton *> buttons = {ui->malePlayButton, ui->femalePlayButton, ui->playAudioEffectButton, ui->getCardButton};
    foreach (QPushButton *button, buttons) {
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    audio->addWidget(ui->malePlayButton);
    audio->addWidget(ui->femalePlayButton);
    audio->addWidget(ui->playAudioEffectButton);
    detail->addLayout(audio);
    detail->addWidget(ui->getCardButton);
    body->addLayout(detail, 5);
    root->addLayout(body, 1);
    delete desktop;
    connect(mobile_search, &QLineEdit::textChanged, this, &CardOverview::filterMobileCards);
}

void CardOverview::filterMobileCards(const QString &text)
{
    QSignalBlocker blocker(ui->tableWidget);
    int first = -1;
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QTableWidgetItem *item = ui->tableWidget->item(row, 0);
        const bool visible = item != nullptr && item->text().contains(text.trimmed(), Qt::CaseInsensitive);
        ui->tableWidget->setRowHidden(row, !visible);
        if (visible && first < 0)
            first = row;
    }
    const int current = ui->tableWidget->currentRow();
    if (first < 0)
        ui->tableWidget->setCurrentItem(nullptr);
    else if (current < 0 || ui->tableWidget->isRowHidden(current))
        ui->tableWidget->setCurrentCell(first, 0);
    blocker.unblock();
    on_tableWidget_itemSelectionChanged();
}

void CardOverview::updateMobilePortrait()
{
    if (mobile_portrait.isNull() || ui->cardLabel->size().isEmpty())
        ui->cardLabel->clear();
    else
        ui->cardLabel->setPixmap(mobile_portrait.scaled(ui->cardLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CardOverview::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    QTimer::singleShot(0, this, &CardOverview::updateMobilePortrait);
}
#endif

void CardOverview::loadFromAll()
{
    QSignalBlocker blocker(ui->tableWidget);
#ifdef Q_OS_ANDROID
    const QSignalBlocker searchBlocker(mobile_search);
    mobile_search->clear();
#endif
    int n = Sanguosha->getCardCount();
    ui->tableWidget->setRowCount(n);
    for (int i = 0; i < n; i++) {
        const Card *card = Sanguosha->getEngineCard(i);
        addCard(i, card);
    }
    blocker.unblock();
#ifdef Q_OS_ANDROID
    filterMobileCards(QString());
#endif

    if (n > 0) {
        ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));

        const Card *card = Sanguosha->getEngineCard(0);
        if (card->getTypeId() == Card::TypeEquip) {
            ui->playAudioEffectButton->show();
            ui->malePlayButton->hide();
            ui->femalePlayButton->hide();
        } else {
            ui->playAudioEffectButton->hide();
            ui->malePlayButton->show();
            ui->femalePlayButton->show();
        }
    }
}

void CardOverview::loadFromList(const QList<const Card *> &list)
{
    QSignalBlocker blocker(ui->tableWidget);
#ifdef Q_OS_ANDROID
    const QSignalBlocker searchBlocker(mobile_search);
    mobile_search->clear();
#endif
    int n = list.length();
    ui->tableWidget->setRowCount(n);
    for (int i = 0; i < n; i++)
        addCard(i, list.at(i));
    blocker.unblock();
#ifdef Q_OS_ANDROID
    filterMobileCards(QString());
#endif

    if (n > 0) {
        ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));

        const Card *card = list.first();
        if (card->getTypeId() == Card::TypeEquip) {
            ui->playAudioEffectButton->show();
            ui->malePlayButton->hide();
            ui->femalePlayButton->hide();
        } else {
            ui->playAudioEffectButton->hide();
            ui->malePlayButton->show();
            ui->femalePlayButton->show();
        }
    }
}

void CardOverview::addCard(int i, const Card *card)
{
    QString name = Sanguosha->translate(card->objectName());
    QIcon suit_icon = QIcon(QString("image/system/suit/%1.png").arg(card->getSuitString()));
    QString suit_str = Sanguosha->translate(card->getSuitString());
    QString point = card->getNumberString();
    QString type = Sanguosha->translate(card->getType());
    QString subtype = Sanguosha->translate(card->getSubtype());
    QString package = Sanguosha->translate(card->getPackage());

    QTableWidgetItem *name_item = new QTableWidgetItem(name);
    name_item->setData(Qt::UserRole, card->getId());
#ifdef Q_OS_ANDROID
    name_item->setText(name + "  " + suit_str + point + "\n" + subtype + " · " + package);
#endif

    ui->tableWidget->setItem(i, 0, name_item);
    ui->tableWidget->setItem(i, 1, new QTableWidgetItem(suit_icon, suit_str));
    ui->tableWidget->setItem(i, 2, new QTableWidgetItem(point));
    ui->tableWidget->setItem(i, 3, new QTableWidgetItem(type));
    ui->tableWidget->setItem(i, 4, new QTableWidgetItem(subtype));

    QTableWidgetItem *package_item = new QTableWidgetItem(package);
    if (Config.value("LuaPackages", QString()).toString().split("+").contains(card->getPackage())) {
        package_item->setBackgroundColor(QColor(0x66, 0xCC, 0xFF));
        package_item->setToolTip(tr("<font color=#FFFF33>This is an Lua extension</font>"));
    }
    ui->tableWidget->setItem(i, 5, package_item);
}

CardOverview::~CardOverview()
{
    delete ui;
}

void CardOverview::on_tableWidget_itemSelectionChanged()
{
    int row = ui->tableWidget->currentRow();
    if (row < 0 || ui->tableWidget->item(row, 0) == nullptr) {
        ui->cardLabel->clear();
        ui->cardDescriptionBox->clear();
        ui->getCardButton->setEnabled(false);
        ui->malePlayButton->hide();
        ui->femalePlayButton->hide();
        ui->playAudioEffectButton->hide();
#ifdef Q_OS_ANDROID
        mobile_portrait = QPixmap();
#endif
        return;
    }
    ui->getCardButton->setEnabled(true);
    int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
    const Card *card = Sanguosha->getEngineCard(card_id);
    QString pixmap_path = QString("image/big-card/%1.png").arg(card->objectName());
#ifdef Q_OS_ANDROID
    mobile_portrait.load(pixmap_path);
    updateMobilePortrait();
#else
    ui->cardLabel->setPixmap(pixmap_path);
#endif

    ui->cardDescriptionBox->setText(card->getDescription(false));

    if (card->getTypeId() == Card::TypeEquip) {
        ui->playAudioEffectButton->show();
        ui->malePlayButton->hide();
        ui->femalePlayButton->hide();
    } else {
        ui->playAudioEffectButton->hide();
        ui->malePlayButton->show();
        ui->femalePlayButton->show();
    }
}

void CardOverview::askCard()
{
    if (!ServerInfo.EnableCheat)
        return;

    int row = ui->tableWidget->currentRow();
    if (row >= 0) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        if (!ClientInstance->getAvailableCards().contains(card_id)) {
            QMessageBox::warning(this, tr("Warning"), tr("These packages don't contain this card"));
            return;
        }
        ClientInstance->requestCheatGetOneCard(card_id);
    }
}

void CardOverview::on_tableWidget_itemDoubleClicked(QTableWidgetItem * /*unused*/)
{
    if (Self != nullptr)
        askCard();
}

void CardOverview::on_malePlayButton_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row >= 0) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        const Card *card = Sanguosha->getEngineCard(card_id);
        Sanguosha->playAudioEffect(G_ROOM_SKIN.getPlayerAudioEffectPath(card->objectName(), true));
    }
}

void CardOverview::on_femalePlayButton_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row >= 0) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        const Card *card = Sanguosha->getEngineCard(card_id);
        Sanguosha->playAudioEffect(G_ROOM_SKIN.getPlayerAudioEffectPath(card->objectName(), false));
    }
}

void CardOverview::on_playAudioEffectButton_clicked()
{
    int row = ui->tableWidget->currentRow();
    if (row >= 0) {
        int card_id = ui->tableWidget->item(row, 0)->data(Qt::UserRole).toInt();
        const Card *card = Sanguosha->getEngineCard(card_id);
        if (card->getTypeId() == Card::TypeEquip) {
            QString effectName = card->getEffectName();
            if (effectName == "vscrossbow")
                effectName = "crossbow";
            QString fileName = G_ROOM_SKIN.getPlayerAudioEffectPath(effectName, QString("equip"), -1);
            if (!QFile::exists(fileName))
                fileName = G_ROOM_SKIN.getPlayerAudioEffectPath(card->getCommonEffectName(), QString("common"), -1);
            Sanguosha->playAudioEffect(fileName);
        }
    }
}
