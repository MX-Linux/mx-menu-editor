/**********************************************************************
 *  mainwindow.h
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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QComboBox>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QTreeWidget>

#include "addappdialog.h"

namespace Ui
{
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    QMultiHash<QString, QString> hashCategories;
    QMultiHash<QString, QString> hashExclude;
    QMultiHash<QString, QString> hashInclude;
    QStringList all_local_desktop_files;
    QStringList all_usr_desktop_files;

    QTreeWidgetItem *addToTree(QTreeWidgetItem *parent, const QString &fileName);
    [[nodiscard]] QPixmap findIcon(const QString &iconName, const QSize &size);
    [[nodiscard]] QString getCatName(const QString &fileName);
    [[nodiscard]] QStringList listCategories() const;
    [[nodiscard]] QStringList listDesktopFiles(const QString &searchString, const QString &location,
                                               bool *hadError = nullptr);
    [[nodiscard]] bool save();
    [[nodiscard]] static QStringList listMenuFiles();
    [[nodiscard]] static QString localApplicationsPath();
    [[nodiscard]] static QString systemApplicationsPath();
    [[nodiscard]] static QString createLocalLauncher(const QString &baseName, const QByteArray &contents,
                                                     bool useCopyName = false);
    void displayList(QStringList menuItems);
    void filterTree(const QString &query);
    void findReloadItem(const QString &filePath);
    void loadMenuFiles();
    void updateRestoreButtonState(const QString &fileName);
    void populateAllCategories();
    void populateCategory(QTreeWidgetItem *categoryItem);
    void insertAppIntoCategories(const QString &filePath, const QStringList &categories);
    void updateLocalBasenamesCache();
    static void restartPanel();
    void setConnections();

public slots:
    void reject() override;
    void addAppMsgBox();
    void addCategory(const QString &category);
    void addCategoryMsgBox();
    void centerWindow();
    void changeCommand();
    void changeComment();
    void changeHide(bool);
    void changeIcon();
    void changeName();
    void changeNotify(bool);
    void changeTerminal(bool);
    void delCategory();
    void enableDelete();
    void enableEdit();
    void loadApps();
    void loadItem(QTreeWidgetItem *item, int);
    void resetInterface();
    void saveSettings();
    void selectCommand();
    void setEnabled(const QString &);
    void onEditorTextChanged();
    void onCustomAppSaved(const QString &path, const QStringList &categories);

private slots:
    static void pushHelp_clicked();
    void pushAbout_clicked();
    void pushRestoreApp_clicked();
    void pushDuplicateApp_clicked();
    bool pushSave_clicked();

private:
    AddAppDialog *add;
    QString advancedEditorBaseline;
    QSettings settings;
    QTreeWidgetItem *current_item {};
    Ui::MainWindow *ui;
    QSet<QString> local_basenames_cache;
};

#endif // MAINWINDOW_H
