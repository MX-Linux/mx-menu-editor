/**********************************************************************
 *  addappdialog.cpp
 **********************************************************************
 * Copyright (C) 2015-2026 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of MX Menu Editor.
 *
 * MX Menu Editor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Menu Editor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Menu Editor.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include "addappdialog.h"
#include "ui_addappdialog.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QScopedPointer>
#include <QToolButton>

#include "desktoputils.h"
#include "mainwindow.h"

namespace
{
constexpr auto SystemBinPath = "/usr/bin";
constexpr auto SystemIconsPath = "/usr/share/icons";
}

AddAppDialog::AddAppDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AddAppDialog)
{
    ui->setupUi(this);
    this->setWindowTitle(tr("Add Custom Application"));

    if (ui->pushSave->icon().isNull()) {
        ui->pushSave->setIcon(QIcon(":/icons/dialog-ok.svg"));
    }

    // Remove focus from Cancel button
    ui->pushCancel->setAutoDefault(false);
    ui->pushCancel->setDefault(false);

    setConnections();
}

AddAppDialog::~AddAppDialog() { delete ui; }

bool AddAppDialog::validateApplicationName(const QString &name, QString &errorMessage)
{
    errorMessage.clear();
    if (name.isEmpty()) {
        errorMessage = tr("Application name cannot be empty");
        return false;
    }

    if (name.length() > 255) {
        errorMessage = tr("Application name is too long (maximum 255 characters)");
        return false;
    }

    if (DesktopUtils::containsInvalidDesktopChars(name)) {
        errorMessage = tr("Application name cannot contain newlines or control characters");
        return false;
    }

    return true;
}

// Checks whether the command's executable exists, prompting the user to continue
// anyway if not. Shared by the Add dialog's Save validation and the main editor's
// Save action, so both flows ask the same question with the same wording.
bool AddAppDialog::confirmExecutableExists(QWidget *parent, const QString &command)
{
    const QString executable = DesktopUtils::parseCommandExecutable(command);
    if (executable.isEmpty() || DesktopUtils::checkExecutableExists(executable)) {
        return true;
    }

    // Remove surrounding quotes for display (e.g., "/path/to app" -> /path/to app)
    QString cleanExecutable = executable;
    if ((cleanExecutable.startsWith(QLatin1Char('"')) && cleanExecutable.endsWith(QLatin1Char('"'))) ||
        (cleanExecutable.startsWith(QLatin1Char('\'')) && cleanExecutable.endsWith(QLatin1Char('\'')))) {
        cleanExecutable = cleanExecutable.mid(1, cleanExecutable.length() - 2);
    }
    const auto answer = QMessageBox::question(
        parent, tr("Warning"),
        tr("The executable '%1' does not exist or is not in PATH.\nDo you want to continue anyway?")
            .arg(cleanExecutable),
        QMessageBox::Yes | QMessageBox::No);
    return answer == QMessageBox::Yes;
}

bool AddAppDialog::validateCommand(const QString &command, QString &errorMessage)
{
    errorMessage.clear();
    if (command.isEmpty()) {
        errorMessage = tr("Command cannot be empty");
        return false;
    }

    if (command.length() > 1024) {
        errorMessage = tr("Command is too long (maximum 1024 characters)");
        return false;
    }

    // Check for newlines that would corrupt .desktop file format
    if (command.contains(QLatin1Char('\n')) || command.contains(QLatin1Char('\r'))) {
        errorMessage = tr("Command cannot contain newlines");
        return false;
    }

    // A non-empty command can still parse to an empty executable token (e.g. "''" or
    // "\"\"" - just quote characters), which would silently pass confirmExecutableExists()
    // below since it treats an empty executable as "nothing to check."
    if (DesktopUtils::parseCommandExecutable(command).isEmpty()) {
        errorMessage = tr("Command cannot be empty");
        return false;
    }

    // Note: .desktop specification allows all shell metacharacters in Exec field
    // Users are responsible for creating valid desktop entries

    return confirmExecutableExists(this, command);
}

bool AddAppDialog::validateComment(const QString &comment, QString &errorMessage)
{
    errorMessage.clear();
    if (comment.length() > 512) {
        errorMessage = tr("Comment is too long (maximum 512 characters)");
        return false;
    }

    if (DesktopUtils::containsInvalidDesktopChars(comment)) {
        errorMessage = tr("Comment cannot contain newlines or control characters");
        return false;
    }

    return true;
}

bool AddAppDialog::validateIconPath(const QString &iconPath, QString &errorMessage)
{
    errorMessage.clear();
    if (iconPath.isEmpty()) {
        // Empty icon path is allowed
        return true;
    }

    if (iconPath.length() > 512) {
        errorMessage = tr("Icon path is too long (maximum 512 characters)");
        return false;
    }

    if (DesktopUtils::containsInvalidDesktopChars(iconPath)) {
        errorMessage = tr("Icon path cannot contain newlines or control characters");
        return false;
    }

    return true;
}

bool AddAppDialog::pushSave_clicked()
{
    // Validate application name
    QString appName = ui->lineEditName->text().trimmed();
    QString errorMessage;
    if (!validateApplicationName(appName, errorMessage)) {
        QMessageBox::warning(this, tr("Error"), errorMessage);
        return false;
    }

    // Validate command
    QString execCommand = ui->lineEditCommand->text().trimmed();
    if (!validateCommand(execCommand, errorMessage)) {
        QMessageBox::warning(this, tr("Error"), errorMessage);
        return false;
    }

    // Validate comment
    QString comment = ui->lineEditComment->text().trimmed();
    if (!validateComment(comment, errorMessage)) {
        QMessageBox::warning(this, tr("Error"), errorMessage);
        return false;
    }

    // Validate icon path
    QString iconPath = this->icon_path;
    if (!validateIconPath(iconPath, errorMessage)) {
        QMessageBox::warning(this, tr("Error"), errorMessage);
        return false;
    }

    QString output;
    const QString fileName = DesktopUtils::sanitizeFileName(appName) + ".desktop";
    const QString appDir = MainWindow::localApplicationsPath();
    if (!QDir().exists(appDir) && !QDir().mkpath(appDir)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not create application directory"));
        return false;
    }
    output = QStringLiteral("[Desktop Entry]\n");
    output.append("Name=" + ui->lineEditName->text() + "\n");
    output.append("Exec=" + ui->lineEditCommand->text() + "\n");
    if (!ui->lineEditComment->text().isEmpty()) {
        output.append("Comment=" + ui->lineEditComment->text() + "\n");
    }
    if (!icon_path.isEmpty()) {
        output.append("Icon=" + icon_path + "\n");
    }
    if (ui->checkStartup->checkState() == Qt::Checked) {
        output.append("StartupNotify=true\n");
    }
    if (ui->checkTerminal->checkState() == Qt::Checked) {
        output.append("Terminal=true\n");
    } else {
        output.append("Terminal=false\n");
    }
    output.append("Type=Application\n");
    QString categories = QStringLiteral("Categories=");
    for (int i = 0; i < ui->listWidgetCategories->count(); ++i) {
        auto *item = ui->listWidgetCategories->item(i);
        categories += item->text() + ";";
    }
    output.append(categories + "\n");
    const auto savedPath = MainWindow::createLocalLauncher(fileName, output.toUtf8());
    if (savedPath.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Could not save the file"));
        return false;
    }
    const QStringList savedCategories = selectedCategories();
    MainWindow::restartPanel();
    resetInterface();
    emit appSaved(savedPath, savedCategories);
    accept();
    return true;
}

void AddAppDialog::pushCancel_clicked()
{
    if (ui->pushSave->isEnabled() && !saveOrNot()) {
        return;
    }
    resetInterface();
    this->close();
}

void AddAppDialog::setIconPath(const QString &path)
{
    icon_path = path;
    ui->pushChangeIcon->setIcon(QIcon(path));
    ui->pushChangeIcon->setText(tr("Change icon"));
}

void AddAppDialog::pushChangeIcon_clicked()
{
    QFileDialog dialog;
    dialog.setFilter(QDir::Hidden);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(tr("Image Files (*.png *.jpg *.bmp *.xpm *.svg)"));
    dialog.setDirectory(SystemIconsPath);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QStringList selectedList = dialog.selectedFiles();
    if (selectedList.isEmpty()) {
        return;
    }
    setIconPath(selectedList.at(0));
}

void AddAppDialog::selectCommand_clicked()
{
    const auto selected = QFileDialog::getOpenFileName(this, tr("Select executable file"), SystemBinPath);
    if (selected.isEmpty()) {
        return;
    }
    ui->lineEditCommand->setText(DesktopUtils::encodeExecPath(selected));
    updateSaveButtonState();
}

void AddAppDialog::updateSaveButtonState()
{
    const bool hasMinimalEntry = !ui->lineEditName->text().isEmpty() && !ui->lineEditCommand->text().isEmpty()
                                  && ui->listWidgetCategories->count() != 0;
    ui->pushSave->setEnabled(hasMinimalEntry);
}

void AddAppDialog::addCategoryToList(const QString &category)
{
    if (!ui->listWidgetCategories->findItems(category, Qt::MatchFixedString).isEmpty()) {
        return;
    }
    ui->listWidgetCategories->addItem(category);
    ui->pushDelete->setEnabled(true);
    updateSaveButtonState();
}

void AddAppDialog::removeCurrentCategory()
{
    const int row = ui->listWidgetCategories->currentRow();
    QScopedPointer<QListWidgetItem> item(ui->listWidgetCategories->takeItem(row));
    if (item.isNull()) {
        return;
    }
    // item automatically deleted when going out of scope
    if (ui->listWidgetCategories->count() == 0) {
        ui->pushDelete->setDisabled(true);
        ui->pushSave->setDisabled(true);
    }
}

// ask whether to save edits or not
bool AddAppDialog::saveOrNot()
{
    if (!ui->pushSave->isEnabled()) {
        return true;
    }

    const auto ans = QMessageBox::question(this, tr("Save changes?"), tr("Do you want to save your edits?"),
                                           QMessageBox::Save | QMessageBox::Discard);
    if (ans == QMessageBox::Save) {
        return pushSave_clicked(); // Continue only if the save actually succeeded
    }
    ui->pushSave->setDisabled(true);
    return true; // Discarded, continue
}

// clear all the GUI items
void AddAppDialog::resetInterface()
{
    icon_path.clear();
    ui->lineEditName->clear();
    ui->lineEditCommand->clear();
    ui->lineEditComment->clear();
    ui->checkStartup->setChecked(false);
    ui->checkTerminal->setChecked(false);
    ui->pushChangeIcon->setIcon(QIcon());
    ui->pushChangeIcon->setText(tr("Set icon"));
    ui->pushSave->setDisabled(true);
    ui->listWidgetCategories->clear();
}

void AddAppDialog::setConnections() const
{
    connect(ui->pushSave, &QPushButton::clicked, this, &AddAppDialog::pushSave_clicked);
    connect(ui->pushCancel, &QPushButton::clicked, this, &AddAppDialog::pushCancel_clicked);
    connect(ui->pushChangeIcon, &QPushButton::clicked, this, &AddAppDialog::pushChangeIcon_clicked);
    connect(ui->selectCommand, &QToolButton::clicked, this, &AddAppDialog::selectCommand_clicked);
    connect(ui->lineEditName, &QLineEdit::editingFinished, this, &AddAppDialog::updateSaveButtonState);
    connect(ui->lineEditCommand, &QLineEdit::editingFinished, this, &AddAppDialog::updateSaveButtonState);
    connect(ui->pushAdd, &QPushButton::clicked, this, &AddAppDialog::addCategoryRequested);
    connect(ui->pushDelete, &QPushButton::clicked, this, &AddAppDialog::removeCurrentCategory);
}

QStringList AddAppDialog::selectedCategories() const
{
    QStringList categories;
    categories.reserve(ui->listWidgetCategories->count());
    for (int i = 0; i < ui->listWidgetCategories->count(); ++i) {
        categories << ui->listWidgetCategories->item(i)->text();
    }
    return categories;
}
