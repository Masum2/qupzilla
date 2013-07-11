/* ============================================================
* QupZilla - WebKit based browser
* Copyright (C) 2010-2013  David Rosca <nowrep@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* ============================================================ */
#include "autofillmanager.h"
#include "ui_autofillmanager.h"
#include "autofill.h"
#include "passwordmanager.h"
#include "passwordbackends/passwordbackend.h"
#include "mainapplication.h"
#include "settings.h"

#include <QUrl>
#include <QMenu>
#include <QTimer>
#include <QSqlQuery>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>

AutoFillManager::AutoFillManager(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::AutoFillManager)
    , m_passwordManager(mApp->autoFill()->passwordManager())
    , m_passwordsShown(false)
{
    ui->setupUi(this);
    if (isRightToLeft()) {
        ui->treePass->headerItem()->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        ui->treePass->headerItem()->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        ui->treePass->headerItem()->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        ui->treePass->setLayoutDirection(Qt::LeftToRight);
        ui->treeExcept->setLayoutDirection(Qt::LeftToRight);
    }

    connect(ui->removePass, SIGNAL(clicked()), this, SLOT(removePass()));
    connect(ui->removeAllPass, SIGNAL(clicked()), this, SLOT(removeAllPass()));
    connect(ui->editPass, SIGNAL(clicked()), this, SLOT(editPass()));
    connect(ui->showPasswords, SIGNAL(clicked()), this, SLOT(showPasswords()));
    connect(ui->search, SIGNAL(textChanged(QString)), ui->treePass, SLOT(filterString(QString)));
    connect(ui->changeBackend, SIGNAL(clicked()), this, SLOT(changePasswordBackend()));
    connect(ui->backendOptions, SIGNAL(clicked()), this, SLOT(showBackendOptions()));
    connect(m_passwordManager, SIGNAL(passwordBackendChanged()), this, SLOT(currentPasswordBackendChanged()));

    connect(ui->removeExcept, SIGNAL(clicked()), this, SLOT(removeExcept()));
    connect(ui->removeAllExcept, SIGNAL(clicked()), this, SLOT(removeAllExcept()));

    QMenu* menu = new QMenu(this);
    menu->addAction(tr("Import Passwords from File..."), this, SLOT(importPasswords()));
    menu->addAction(tr("Export Passwords to File..."), this, SLOT(exportPasswords()));
    ui->importExport->setMenu(menu);
    ui->importExport->setPopupMode(QToolButton::InstantPopup);
    ui->search->setPlaceholderText(tr("Search"));

    // Password backends
    ui->currentBackend->setText(QString("<b>%1</b>").arg(m_passwordManager->activeBackend()->name()));
    ui->backendOptions->setVisible(m_passwordManager->activeBackend()->hasSettings());

    // Load passwords
    QTimer::singleShot(0, this, SLOT(loadPasswords()));
}

void AutoFillManager::loadPasswords()
{
    ui->showPasswords->setText(tr("Show Passwords"));
    m_passwordsShown = false;

    QVector<PasswordEntry> allEntries = mApp->autoFill()->getAllFormData();

    ui->treePass->clear();
    foreach (const PasswordEntry &entry, allEntries) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->treePass);
        item->setText(0, entry.host);
        item->setText(1, entry.username);
        item->setText(2, "*****");

        QVariant v;
        v.setValue<PasswordEntry>(entry);
        item->setData(0, Qt::UserRole + 10, v);
        ui->treePass->addTopLevelItem(item);
    }

    QSqlQuery query;
    query.exec("SELECT server, id FROM autofill_exceptions");
    ui->treeExcept->clear();
    while (query.next()) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeExcept);
        item->setText(0, query.value(0).toString());
        item->setData(0, Qt::UserRole + 10, query.value(1).toString());
        ui->treeExcept->addTopLevelItem(item);
    }

    ui->treePass->sortByColumn(-1);
    ui->treeExcept->sortByColumn(-1);
}

void AutoFillManager::changePasswordBackend()
{
    QHash<QString, PasswordBackend*> backends = m_passwordManager->availableBackends();
    QStringList items;

    int current = 0;

    QHashIterator<QString, PasswordBackend*> i(backends);
    while (i.hasNext()) {
        i.next();
        if (i.value() == m_passwordManager->activeBackend()) {
            current = items.size();
        }
        items << i.value()->name();
    }

    QString item = QInputDialog::getItem(this, tr("Change backend..."), tr("Change backend:"), items, current, false);

    // Switch backends
    if (!item.isEmpty()) {
        PasswordBackend* backend = 0;

        QHashIterator<QString, PasswordBackend*> i(backends);
        while (i.hasNext()) {
            i.next();
            if (i.value()->name() == item) {
                backend = i.value();
                break;
            }
        }

        if (backend) {
            m_passwordManager->switchBackend(backends.key(backend));
        }
    }
}

void AutoFillManager::showBackendOptions()
{
    PasswordBackend* backend = m_passwordManager->activeBackend();

    if (backend->hasSettings()) {
        backend->showSettings(this);
    }
}

void AutoFillManager::showPasswords()
{
    if (m_passwordsShown) {
        for (int i = 0; i < ui->treePass->topLevelItemCount(); i++) {
            QTreeWidgetItem* item = ui->treePass->topLevelItem(i);
            if (!item) {
                continue;
            }
            item->setText(2, "*****");
        }

        ui->showPasswords->setText(tr("Show Passwords"));
        m_passwordsShown = false;

        return;
    }

    m_passwordsShown = true;

    int result = QMessageBox::question(this, tr("Show Passwords"), tr("Are you sure that you want to show all passwords?"),
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes) {
        return;
    }

    for (int i = 0; i < ui->treePass->topLevelItemCount(); i++) {
        QTreeWidgetItem* item = ui->treePass->topLevelItem(i);
        if (!item) {
            continue;
        }

        item->setText(2, item->data(0, Qt::UserRole + 10).value<PasswordEntry>().password);
    }

    ui->showPasswords->setText(tr("Hide Passwords"));
}

void AutoFillManager::removePass()
{
    QTreeWidgetItem* curItem = ui->treePass->currentItem();
    if (!curItem) {
        return;
    }

    PasswordEntry entry = curItem->data(0, Qt::UserRole + 10).value<PasswordEntry>();
    mApp->autoFill()->removeEntry(entry);

    delete curItem;
}

void AutoFillManager::removeAllPass()
{
    QMessageBox::StandardButton button = QMessageBox::warning(this, tr("Confirmation"),
                                         tr("Are you sure to delete all passwords on your computer?"), QMessageBox::Yes | QMessageBox::No);
    if (button != QMessageBox::Yes) {
        return;
    }

    mApp->autoFill()->removeAllEntries();
    ui->treePass->clear();
}

void AutoFillManager::editPass()
{
    QTreeWidgetItem* curItem = ui->treePass->currentItem();
    if (!curItem) {
        return;
    }

    PasswordEntry entry = curItem->data(0, Qt::UserRole + 10).value<PasswordEntry>();

    bool ok;
    QString text = QInputDialog::getText(this, tr("Edit password"), tr("Change password:"), QLineEdit::Normal, entry.password, &ok);

    if (ok && !text.isEmpty() && text != entry.password) {
        QByteArray oldPass = "=" + PasswordManager::urlEncodePassword(entry.password);
        entry.data.replace(oldPass, "=" + PasswordManager::urlEncodePassword(text));
        entry.password = text;

        if (mApp->autoFill()->updateEntry(entry)) {
            QVariant v;
            v.setValue<PasswordEntry>(entry);
            curItem->setData(0, Qt::UserRole + 10, v);

            if (m_passwordsShown) {
                curItem->setText(2, text);
            }
        }
    }
}

void AutoFillManager::removeExcept()
{
    QTreeWidgetItem* curItem = ui->treeExcept->currentItem();
    if (!curItem) {
        return;
    }
    QString id = curItem->data(0, Qt::UserRole + 10).toString();
    QSqlQuery query;
    query.prepare("DELETE FROM autofill_exceptions WHERE id=?");
    query.addBindValue(id);
    query.exec();

    delete curItem;
}

void AutoFillManager::removeAllExcept()
{
    QSqlQuery query;
    query.exec("DELETE FROM autofill_exceptions");

    ui->treeExcept->clear();
}

void AutoFillManager::showExceptions()
{
    ui->tabWidget->setCurrentIndex(1);
}

void AutoFillManager::importPasswords()
{
    m_fileName = QFileDialog::getOpenFileName(this, tr("Choose file..."), QDir::homePath() + "/passwords.xml", "*.xml");

    if (m_fileName.isEmpty()) {
        return;
    }

    QTimer::singleShot(0, this, SLOT(slotImportPasswords()));
}

void AutoFillManager::exportPasswords()
{
    m_fileName = QFileDialog::getSaveFileName(this, tr("Choose file..."), QDir::homePath() + "/passwords.xml", "*.xml");

    if (m_fileName.isEmpty()) {
        return;
    }

    QTimer::singleShot(0, this, SLOT(slotExportPasswords()));
}

void AutoFillManager::slotImportPasswords()
{
    QFile file(m_fileName);

    if (!file.open(QFile::ReadOnly)) {
        ui->importExportLabel->setText(tr("Cannot read file!"));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    bool status = mApp->autoFill()->importPasswords(file.readAll());
    file.close();

    ui->importExportLabel->setText(status ? tr("Successfully imported") : tr("Error while importing!"));
    loadPasswords();

    QApplication::restoreOverrideCursor();
}

void AutoFillManager::slotExportPasswords()
{
    QFile file(m_fileName);

    if (!file.open(QFile::WriteOnly)) {
        ui->importExportLabel->setText(tr("Cannot write to file!"));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    file.write(mApp->autoFill()->exportPasswords());
    file.close();

    ui->importExportLabel->setText(tr("Successfully exported"));

    QApplication::restoreOverrideCursor();
}

void AutoFillManager::currentPasswordBackendChanged()
{
    ui->currentBackend->setText(QString("<b>%1</b>").arg(m_passwordManager->activeBackend()->name()));
    ui->backendOptions->setVisible(m_passwordManager->activeBackend()->hasSettings());

    QTimer::singleShot(0, this, SLOT(loadPasswords()));
}

AutoFillManager::~AutoFillManager()
{
    delete ui;
}
