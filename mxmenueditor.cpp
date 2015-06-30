/**********************************************************************
 *  mxmenueditor.cpp
 **********************************************************************
 * Copyright (C) 2015 MX Authors
 *
 * Authors: Adrian
 *          MX & MEPIS Community <http://forum.mepiscommunity.org>
 *
 * This file is part of MX Menu Editor.
 *
 * MX Menu Editor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Menu Editor.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include "mxmenueditor.h"
#include "ui_mxmenueditor.h"
#include "ui_addappdialog.h"

#include <QProcess>
#include <QFileDialog>
#include <QTextStream>
#include <QFormLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QHashIterator>
#include <QTextCodec>

//#include <QDebug>

mxmenueditor::mxmenueditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::mxmenueditor),
    add(new AddAppDialog)
{
    ui->setupUi(this);

    comboBox = new QComboBox;
    version = getVersion("mx-menu-editor");

    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    all_local_desktop_files = listDesktopFiles("\"\"", QDir::homePath() + "/.local/share/applications/*");
    all_usr_desktop_files = listDesktopFiles("\"\"", "/usr/share/applications/*");

    resetInterface();
    loadMenuFiles();

    connect(ui->treeWidget, SIGNAL(itemSelectionChanged()), SLOT(loadApps()));
    connect(ui->treeWidget, SIGNAL(itemExpanded(QTreeWidgetItem*)), SLOT(loadApps(QTreeWidgetItem*)));
    connect(ui->toolButtonCommand, SIGNAL(clicked()), SLOT(selectCommand()));
    connect(ui->lineEditName, SIGNAL(editingFinished()), SLOT(changeName()));
    connect(ui->lineEditCommand, SIGNAL(editingFinished()), SLOT(changeCommand()));
    connect(ui->lineEditComment, SIGNAL(editingFinished()), SLOT(changeComment()));
    connect(ui->listWidgetEditCategories, SIGNAL(itemSelectionChanged()), SLOT(enableDelete()));
    connect(ui->pushDelete, SIGNAL(clicked()), SLOT(delCategory()));
    connect(ui->pushAdd, SIGNAL(clicked()), SLOT(addCategoryMsgBox()));
    connect(ui->pushChangeIcon, SIGNAL(clicked()), SLOT(changeIcon()));
    connect(ui->checkNotify, SIGNAL(clicked(bool)), SLOT(changeNotify(bool)));
    connect(ui->checkHide, SIGNAL(clicked(bool)), SLOT(changeHide(bool)));
    connect(ui->checkRunInTerminal, SIGNAL(clicked(bool)), SLOT(changeTerminal(bool)));
    connect(ui->advancedEditor, SIGNAL(undoAvailable(bool)), ui->buttonSave, SLOT(setEnabled(bool)));
    connect(ui->pushAddApp  , SIGNAL(clicked()), SLOT(addAppMsgBox()));
    connect(ui->lineEditName, SIGNAL(textEdited(QString)), SLOT(setEnabled(QString)));
    connect(ui->lineEditCommand, SIGNAL(textEdited(QString)), SLOT(setEnabled(QString)));
    connect(ui->lineEditComment, SIGNAL(textEdited(QString)), SLOT(setEnabled(QString)));

}

mxmenueditor::~mxmenueditor()
{
    delete ui;
}

// util function for getting bash command output and error code
Output runCmd(QString cmd)
{
    QProcess *proc = new QProcess();
    QEventLoop loop;
    proc->setReadChannelMode(QProcess::MergedChannels);
    proc->start("/bin/bash", QStringList() << "-c" << cmd);
    proc->waitForFinished();
    Output out = {proc->exitCode(), proc->readAll().trimmed()};
    delete proc;
    return out;
}

// get version of the program
QString mxmenueditor::getVersion(QString name)
{
    QString cmd = QString("dpkg -l %1 | awk 'NR==6 {print $3}'").arg(name);
    return runCmd(cmd).str;
}

// load menu files
void mxmenueditor::loadMenuFiles()
{
    QString home_path = QDir::homePath();
    QStringList menu_items;
    QStringList menu_files = listMenuFiles();

    // process each menu_file
    foreach (QString file_name, menu_files) {
        QFile file(file_name);
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine();
                QString name;
                // find <Name> of the item
                if (line.contains("<Name>") ) {
                    // find <Directory> for the <Name>
                    if (!in.atEnd()) {
                        line = in.readLine();
                        if (line.contains("<Directory>")) {
                            line = line.remove("<Directory>").remove("</Directory>").trimmed();
                            QString file_name = home_path + "/.local/share/desktop-directories/" + line;
                            QFile *file = new QFile(file_name);
                            if (!file->exists()) { // use /usr if the file is not present in ~/.local
                                QString file_name = "/usr/share/desktop-directories/" + line;
                                file->setFileName(file_name);
                            }
                            name = getCatName(file); // get the Name= from .directory file
                            if (name != "" && name != "Other" && name != "Wine") {
                                menu_items << name;
                            }
                            delete file;
                            // Find <Category> and <Filename> and add them in hashCategory and hashInclude
                            while (!(in.atEnd() || line.contains("</Include>"))) {
                                line = in.readLine();
                                if (line.contains("<Category>")) {
                                    line = line.remove("<Category>").remove("</Category>").trimmed();
                                    if (!hashCategories.values(name).contains(line)) {
                                        hashCategories.insertMulti(name, line); //each menu category displays a number of categories
                                    }
                                }
                                if (line.contains("<Filename>")) {
                                    line = line.remove("<Filename>").remove("</Filename>").trimmed();
                                    if (!hashInclude.values(name).contains(line)) {
                                        hashInclude.insertMulti(name, line); //each menu category contains a number of files
                                    }
                                }
                            }
                            // find <Exludes> and add them in hashExclude
                            while (!(in.atEnd() || line.contains("</Exclude>") || line.contains("<Menu>"))) {
                                line = in.readLine();
                                if (line.contains("<Exclude>")) {
                                    while (!(in.atEnd() || line.contains("</Exclude>"))) {
                                        line = in.readLine();
                                        if(line.contains("<Filename>")) {
                                            line = line.remove("<Filename>").remove("</Filename>").trimmed();
                                            if (!hashExclude.values(name).contains(line)) {
                                                hashExclude.insertMulti(name, line); //each menu category contains a number of files
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            file.close();
        }
    }
    displayList(menu_items);
}

// get Name= from .directory file
QString mxmenueditor::getCatName(QFile *file)
{
    QString cmd = QString("grep Name= %1").arg(file->fileName());
    Output out = runCmd(cmd.toUtf8());
    if (out.exit_code == 0) {
        return out.str.remove("Name=");
    } else {
        return "";
    }
}

// return a list of .menu files
QStringList mxmenueditor::listMenuFiles() {
    QString home_path = QDir::homePath();
    QStringList menu_files("/etc/xdg/menus/xfce-applications.menu");
    QDir user_dir;

    // add menu files from user directory
    user_dir.setPath(home_path + "/.config/menus");
    QDirIterator it(user_dir, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString item = it.next();
        if (item.endsWith(".menu")) {
            menu_files << item;
        }
    }
    return menu_files;
}

// display sorted list of menu items in the treeWidget
void mxmenueditor::displayList(QStringList menu_items) {
    QTreeWidgetItem *topLevelItem = 0;
    ui->treeWidget->setHeaderLabel("");
    ui->treeWidget->setSortingEnabled(true);
    menu_items.removeDuplicates();
    foreach(QString item, menu_items) {
        topLevelItem = new QTreeWidgetItem(ui->treeWidget, QStringList(item));
        // topLevelItem look
        QFont font;
        font.setBold(true);
        topLevelItem->setForeground(0, QBrush(Qt::darkGreen));
        topLevelItem->setFont(0, font);
    }
    ui->treeWidget->sortItems(0, Qt::AscendingOrder);
}


void mxmenueditor::loadApps(QTreeWidgetItem* item)
{
    ui->treeWidget->setCurrentItem(item);
}

// load the applications in the selected category
void mxmenueditor::loadApps()
{
    // execute if topLevel item is selected
    if (!ui->treeWidget->currentItem()->parent()) {
        if (ui->buttonSave->isEnabled()) {
            if (save()) {
                return;
            }
        }
        QTreeWidgetItem *item = ui->treeWidget->currentItem();
        item->takeChildren();
        resetInterface();

        QStringList categories; // displayed categories in the menu
        QStringList includes; // included files
        QStringList excludes; // excluded files
        QStringList includes_usr;
        QStringList includes_local;
        QStringList listApps;

        categories << hashCategories.values(item->text(0));
        includes << hashInclude.values(item->text(0));
        excludes << hashExclude.values(item->text(0));

        foreach (QString file, includes) {
            includes_usr << "/usr/share/applications" + file;
            includes_local << QDir::homePath() + "/.local/share/applications/" + file;
        }

        // determine search string for all categories to be listead under menu category
        QString search_string;
        foreach (QString category, categories) {
            if (search_string == "") {
                search_string = "Categories=.*\"" + category + "\"";
            } else {
                search_string += "\\|Categories=.*\"" + category + "\"";
            }
        }

        // list .desktop files from /usr and .local
        QStringList usr_desktop_files = listDesktopFiles(search_string, "/usr/share/applications/*");
        QStringList local_desktop_files = listDesktopFiles(search_string, QDir::homePath() + "/.local/share/applications/*");

        // add included files
        usr_desktop_files.append(includes_usr);
        local_desktop_files.append(includes_local);

        // exclude files
        foreach (QString base_name, excludes) {
            usr_desktop_files.removeAll("/usr/share/applications/" + base_name);
        }
        foreach (QString base_name, excludes) {
            local_desktop_files.removeAll(QDir::homePath() + "/.local/share/applications/" + base_name);
        }

        // list of names without path
        QStringList local_base_names;
        foreach (QString local_name, all_local_desktop_files) {
            QFileInfo f_local(local_name);
            local_base_names << f_local.fileName();
        }
        QStringList usr_base_names;
        foreach (QString usr_name, all_usr_desktop_files) {
            QFileInfo f_usr(usr_name);
            usr_base_names << f_usr.fileName();
        }

        // parse local .desktop files
        foreach (QString local_name, local_desktop_files) {
            QFileInfo fi_local(local_name);
            addToTree(local_name);
            all_local_desktop_files << local_name;
            if (usr_base_names.contains(fi_local.fileName())) {
                // find item and set restore flag
                QTreeWidgetItemIterator it(item);
                while (*it) {
                    if ((*it)->text(1).contains(local_name)) {
                        (*it)->setText(2, "restore");
                    }
                    ++it;
                }
            }
        }

        // parse usr .desktop files
        foreach (QString file, usr_desktop_files) {
            QFileInfo fi(file);
            QString base_name = fi.fileName();
            // add items only for files that are not in the list of local .desktop files
            if (!local_base_names.contains(base_name)) {
                addToTree(file);
            } else {
                // find item and set restore flag
                QTreeWidgetItemIterator it(item);
                while (*it) {
                    if ((*it)->text(1).contains(base_name)) {
                        (*it)->setText(2, "restore");
                    }
                    ++it;
                }
            }
        }
        item->sortChildren(true, Qt::AscendingOrder);
        item->setExpanded(true);
        current_item = ui->treeWidget->currentItem(); // remember the current_item in case user selects another item before saving
    } else {
        loadItem(ui->treeWidget->currentItem(), 0);
    }
}

// add .desktop item to treeWidget
void mxmenueditor::addToTree(QString file_name)
{
    QFile file(file_name);
    if (file.exists()) {
        QString cmd = "grep -m1 ^Name= \"" + file_name.toUtf8() + "\"| cut  -d'=' -f2";
        QString app_name = runCmd(cmd).str;
        // add item as childItem to treeWidget
        QTreeWidgetItem *childItem = new QTreeWidgetItem(ui->treeWidget->currentItem());
        if (isHidden(file_name)) {
            childItem->setForeground(0, QBrush(Qt::gray));
        }
        file_name.insert(0, "\"");
        file_name.append("\"");
        childItem->setText(0, app_name);
        childItem->setText(1, file_name);
    }
}

// list .desktop files
QStringList mxmenueditor::listDesktopFiles(QString search_string, QString location)
{
    QStringList listDesktop;
    if (search_string != "") {
        QString cmd = QString("grep -Elr %1 %2").arg(search_string).arg(location);
        Output out = runCmd(cmd);
        if (out.str != "") {
            listDesktop = out.str.split("\n");
        }
    }
    return listDesktop;
}

// load selected item to be edited
void mxmenueditor::loadItem(QTreeWidgetItem *item, int)
{
    // execute if not topLevel item is selected
    if (item->parent()) {
        if (ui->buttonSave->isEnabled()) {
            if (save()) {
                return;
            }
        }
        QString file_name = ui->treeWidget->currentItem()->text(1);
        resetInterface();
        enableEdit();

        QString cmd;
        Output out;

        out = runCmd("cat " + file_name.toUtf8());
        ui->advancedEditor->setText(out.str);
        // load categories
        out = runCmd("grep ^Categories= " + file_name.toUtf8() + " | cut -f2 -d=");
        if (out.str.endsWith(";")) {
            out.str.remove(out.str.length() - 1, 1);
        }
        QStringList categories = out.str.split(";");
        ui->listWidgetEditCategories->addItems(categories);
        // load name, command, comment
        out = runCmd("grep -m1 ^Name= " + file_name.toUtf8() + " | cut -f2 -d=");
        ui->lineEditName->setText(out.str);
        out = runCmd("grep -m1 ^Comment= " + file_name.toUtf8() + " | cut -f2 -d=");
        ui->lineEditComment->setText(out.str);
        ui->lineEditComment->home(false);
        out = runCmd("grep -m1 ^Exec= " + file_name.toUtf8() + " | cut -f2 -d=");
        ui->lineEditCommand->setText(out.str);
        ui->lineEditCommand->home(false);
        // load options
        out = runCmd("grep -m1 ^StartupNotify= " + file_name.toUtf8() + " | cut -f2 -d=");
        if (out.str == "true") {
            ui->checkNotify->setChecked(true);
        }
        out = runCmd("grep -m1 ^NoDisplay= " + file_name.toUtf8() + " | cut -f2 -d=");
        if (out.str == "true") {
            ui->checkHide->setChecked(true);
        }
        out = runCmd("grep -m1 ^Terminal= " + file_name.toUtf8() + " | cut -f2 -d=");
        if (out.str == "true") {
            ui->checkRunInTerminal->setChecked(true);
        }
        out = runCmd("grep -m1 ^Icon= " + file_name.toUtf8() + " | cut -f2 -d=");
        if (out.str != "") {
            QSize size = ui->labelIcon->size();
            QString icon = out.str;
            if (QFile(icon).exists()) {
                ui->labelIcon->setPixmap(QPixmap(icon).scaled(size));
            } else {
                ui->labelIcon->setPixmap(QPixmap(findIcon(icon)).scaled(size));
            }
        }

        // enable RestoreApp button if flag is set up for item
        if (ui->treeWidget->currentItem()->text(2) == "restore") {
            ui->pushRestoreApp->setEnabled(true);
        } else {
            ui->pushRestoreApp->setEnabled(false);
        }
        current_item = ui->treeWidget->currentItem(); // remember the current_item in case user selects another item before saving
    }
}

// check if the item is hidden
bool mxmenueditor::isHidden(QString file_name)
{
    QString cmd = "grep -q NoDisplay=true \"" + file_name + "\"";
    return !system(cmd.toUtf8());
}

// select command to be used
void mxmenueditor::selectCommand()
{
    QFileDialog dialog;
    QString selected = dialog.getOpenFileName(this, tr("Select executable file"), "/usr/bin");
    if (selected != "") {
        if (ui->lineEditCommand->isEnabled()) {
            ui->lineEditCommand->setText(selected);
            ui->buttonSave->setEnabled(true);
        } else { // if running command from add-custom-app window
            add->ui->lineEditCommand->setText(selected);
        }
        changeCommand();
    }
}

// clear selection and reset GUI components
void mxmenueditor::resetInterface()
{
    ui->listWidgetEditCategories->clear();
    ui->advancedEditor->clear();
    ui->advancedEditor->setDisabled(true);
    ui->advancedEditor->setLineWrapMode(QTextEdit::NoWrap);
    ui->lineEditName->clear();
    ui->lineEditComment->clear();
    ui->lineEditCommand->clear();
    ui->checkHide->setChecked(false);
    ui->checkNotify->setChecked(false);
    ui->checkRunInTerminal->setChecked(false);
    ui->checkHide->setDisabled(true);
    ui->checkNotify->setDisabled(true);
    ui->checkRunInTerminal->setDisabled(true);
    ui->toolButtonCommand->setDisabled(true);
    ui->pushAdd->setDisabled(true);
    ui->pushDelete->setDisabled(true);
    ui->lineEditCommand->setDisabled(true);
    ui->lineEditComment->setDisabled(true);
    ui->lineEditName->setDisabled(true);
    ui->pushChangeIcon->setDisabled(true);
    ui->pushRestoreApp->setDisabled(true);
    ui->buttonSave->setDisabled(true);
    ui->labelIcon->setPixmap(QPixmap());
}

// enable buttons to edit item
void mxmenueditor::enableEdit()
{
    ui->checkNotify->setEnabled(true);
    ui->checkHide->setEnabled(true);
    ui->checkRunInTerminal->setEnabled(true);
    ui->toolButtonCommand->setEnabled(true);
    ui->pushAdd->setEnabled(true);
    ui->pushChangeIcon->setEnabled(true);
    ui->advancedEditor->setEnabled(true);
    ui->lineEditCommand->setEnabled(true);
    ui->lineEditComment->setEnabled(true);
    ui->lineEditName->setEnabled(true);
    ui->pushChangeIcon->setEnabled(true);
}

// change the icon of the application
void mxmenueditor::changeIcon()
{
    QFileDialog dialog;
    QString selected;
    dialog.setFilter(QDir::Hidden);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(tr("Image Files (*.png *.jpg *.bmp *.xpm)"));
    dialog.setDirectory("/usr/share/icons");
    if (dialog.exec()) {
        QStringList selected_list = dialog.selectedFiles();
        selected = selected_list.at(0);
    }
    if (selected != "") {
        QString text = ui->advancedEditor->toPlainText();
        if (ui->lineEditCommand->isEnabled()) { // started from editor
            ui->buttonSave->setEnabled(true);
            text.replace(QRegExp("(^|\n)Icon=[^\n]*(\n|$)"), "\nIcon=" + selected + "\n");
            ui->advancedEditor->setText(text);
            ui->labelIcon->setPixmap(QPixmap(selected));
        } else { // if running command from add-custom-app window
            add->icon_path = selected;
            add->ui->pushChangeIcon->setIcon(QIcon(selected));
            add->ui->pushChangeIcon->setText(tr("Change icon"));
        }
    }
}

// change the name of the entry
void mxmenueditor::changeName()
{
    if (ui->lineEditCommand->isEnabled()) { // started from editor
        ui->buttonSave->setEnabled(true);
        QString new_name = ui->lineEditName->text();
        if ( new_name != "") {
            QString text = ui->advancedEditor->toPlainText();
            QRegExp regex("(^|\n)Name=[^\n]*(\n|$)");
            int index = text.indexOf(regex, 0);
            if (index != -1) {
                text.replace(index, regex.matchedLength(), "\nName=" + new_name + "\n");
                ui->advancedEditor->setText(text);
            }
        }
    } else { // if running command from add-custom-app window
        if (add->ui->lineEditName->text() != "" && add->ui->lineEditCommand->text() != "" && add->ui->listWidgetCategories->count() != 0) {
            add->ui->buttonSave->setEnabled(true);
        } else {
            add->ui->buttonSave->setEnabled(false);
        }
    }
}

// change the command string
void mxmenueditor::changeCommand()
{
    if (ui->lineEditCommand->isEnabled()) { // started from editor
        ui->buttonSave->setEnabled(true);
        QString new_command = ui->lineEditCommand->text();
        if (new_command != "") {
            QString text = ui->advancedEditor->toPlainText();
            text.replace(QRegExp("(^|\n)Exec=[^\n]*(\n|$)"), "\nExec=" + new_command + "\n");
            ui->advancedEditor->setText(text);
        }
    } else { // if running command from add-custom-app window
        QString new_command = add->ui->lineEditCommand->text();
        if (new_command != "" && add->ui->lineEditName->text() != "" && add->ui->listWidgetCategories->count() != 0) {
            add->ui->buttonSave->setEnabled(true);
        } else {
            add->ui->buttonSave->setEnabled(false);
        }
    }
}

// change the comment string
void mxmenueditor::changeComment()
{
    if (ui->lineEditCommand->isEnabled()) { // started from editor
        ui->buttonSave->setEnabled(true);
        QString text = ui->advancedEditor->toPlainText();
        QString new_comment = ui->lineEditComment->text();
        if (new_comment != "") {
            if (text.contains("Comment=")) {
                text.replace(QRegExp("(^|\n)Comment=[^\n]*(\n|$)"), "\nComment=" + new_comment + "\n");
            } else {
                text.trimmed();
                text.append("\nComment=" + new_comment + "\n");
            }
        } else {
            text.remove(QRegExp("(^|\n)Comment=[^\n]*(\n|$)"));
        }
        ui->advancedEditor->setText(text);
    }
}

// enable delete button for category
void mxmenueditor::enableDelete()
{
    ui->pushDelete->setEnabled(true);
}

// delete selected category
void mxmenueditor::delCategory()
{
    int row;
    if (ui->lineEditCommand->isEnabled()) { // started from editor
        ui->buttonSave->setEnabled(true);
        row = ui->listWidgetEditCategories->currentRow();
        QListWidgetItem *item = ui->listWidgetEditCategories->takeItem(row);
        QString text = ui->advancedEditor->toPlainText();
        int indexCategory = text.indexOf(QRegExp("(^|\n)Categories=[^\n]*(\n|$)"));
        int indexToDelete = text.indexOf(item->text() + ";", indexCategory);
        text.remove(indexToDelete, item->text().length() + 1);
        ui->advancedEditor->setText(text);
        if (ui->listWidgetEditCategories->count() == 0) {
            ui->pushDelete->setDisabled(true);
            ui->buttonSave->setDisabled(true);
        }
    } else { // if running command from add-custom-app window
        row = add->ui->listWidgetCategories->currentRow();
        add->ui->listWidgetCategories->takeItem(row);
        if (add->ui->listWidgetCategories->count() == 0) {
            add->ui->pushDelete->setDisabled(true);
            add->ui->buttonSave->setDisabled(true);
        }
    }
}

// change startup notify
void mxmenueditor::changeNotify(bool checked)
{
    ui->buttonSave->setEnabled(true);
    QString text = ui->advancedEditor->toPlainText();
    QString str = QString(checked ? "true" : "false");
    if (text.contains("StartupNotify=")) {
        text.replace(QRegExp("(^|\n)StartupNotify=[^\n]*(\n|$)"), "\nStartupNotify=" + str + "\n");
    } else {
        text.trimmed();
        text.append("\nStartupNotify=" + str);
    }
    ui->advancedEditor->setText(text);
}

// hide or show the item in the menu
void mxmenueditor::changeHide(bool checked)
{
    ui->buttonSave->setEnabled(true);
    QString text = ui->advancedEditor->toPlainText();
    QString str = QString(checked ? "true" : "false");
    if (text.contains("NoDisplay=")) {
        text.replace(QRegExp("(^|\n)NoDisplay=[^\n]*(\n|$)"), "\nNoDisplay=" + str + "\n");
    } else {
        text.trimmed();
        text.append("\nNoDisplay=" + str);
    }
    ui->advancedEditor->setText(text);
}

// change "run in terminal" setting
void mxmenueditor::changeTerminal(bool checked)
{
    ui->buttonSave->setEnabled(true);
    QString text = ui->advancedEditor->toPlainText();
    QString str = QString(checked ? "true" : "false");
    if (text.contains("Terminal=")) {
        text.replace(QRegExp("(^|\n)Terminal=[^\n]*(\n|$)"), "\nTerminal=" + str + "\n");
    } else {
        text.trimmed();
        text.append("\nTerminal=" + str);
    }
    ui->advancedEditor->setText(text);
}


// list categories of the displayed items
QStringList mxmenueditor::listCategories()
{
    QStringList categories;
    QHashIterator<QString, QString> i(hashCategories);
    while (i.hasNext()) {
        i.next();
        categories << i.value();
    }
    categories.sort();
    return categories;
}

// display add category message box
void mxmenueditor::addCategoryMsgBox()
{
    QStringList categories = listCategories();

    QWidget *window = new QWidget(this, Qt::Dialog);
    window->setWindowTitle(tr("Choose category"));
    window->resize(250, 80);

    QDialogButtonBox *buttonBox = new QDialogButtonBox();

    comboBox->clear();
    //comboBox->setEditable(true);
    comboBox->addItems(categories);
    comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // because we want to display the buttons in reverse order we use counter-intuitive roles.
    buttonBox->addButton(tr("Cancel"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(tr("OK"), QDialogButtonBox::RejectRole);
    connect(buttonBox, SIGNAL(accepted()), window, SLOT(close()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(addCategory()));
    connect(buttonBox, SIGNAL(rejected()), window, SLOT(close()));

    QFormLayout *layout = new QFormLayout;
    layout->addRow(comboBox);
    layout->addRow(buttonBox);

    window->setLayout(layout);
    window->show();
}

// add selected categorory to the .desktop file
void mxmenueditor::addCategory()
{
    QString str = comboBox->currentText();
    QString text = ui->advancedEditor->toPlainText();
    int index = text.indexOf(QRegExp("(^|\n)Categories=[^\n]*(\n|$)"));
    index = text.indexOf("\n", index + 1);
    if (ui->lineEditCommand->isEnabled()) { // started from editor
        if (ui->listWidgetEditCategories->findItems(str, Qt::MatchFixedString).isEmpty()) {
            ui->buttonSave->setEnabled(true);
            text.insert(index, str + ";");
            ui->listWidgetEditCategories->addItem(str);
            ui->advancedEditor->setText(text);
            ui->pushDelete->setEnabled(true);
            if (ui->listWidgetEditCategories->count() == 0) {
                ui->buttonSave->setDisabled(true);
            }
        }
    } else { // if running command from add-custom-app window
        if (add->ui->listWidgetCategories->findItems(str, Qt::MatchFixedString).isEmpty()) {
            text.insert(index, str + ";");
            add->ui->listWidgetCategories->addItem(str);
            add->ui->pushDelete->setEnabled(true);
            if (add->ui->lineEditName->text() != "" && add->ui->lineEditCommand->text() != "" && add->ui->listWidgetCategories->count() != 0) {
                add->ui->buttonSave->setEnabled(true);
            } else {
                add->ui->buttonSave->setEnabled(false);
            }
        }
    }
}

// display add application message box
void mxmenueditor::addAppMsgBox()
{
    QStringList categories;
    QTreeWidgetItemIterator it(ui->treeWidget);
    // list top level items
    while (*it && !(*it)->parent()) {
        categories << (*it)->text(0);
        ++it;
    }
    if (ui->buttonSave->isEnabled()) {
        if (save()) {
            return;
        }
    }
    add->show();
    resetInterface();
    ui->treeWidget->collapseAll();
    disconnect(add->ui->selectCommand, 0, 0, 0);
    disconnect(add->ui->pushChangeIcon, 0, 0, 0);
    disconnect(add->ui->lineEditName, 0, 0, 0);
    disconnect(add->ui->lineEditCommand, 0, 0, 0);
    disconnect(add->ui->lineEditComment, 0, 0, 0);
    disconnect(add->ui->pushAdd, 0, 0, 0);
    disconnect(add->ui->pushDelete, 0, 0, 0);
    connect(add->ui->selectCommand, SIGNAL(clicked()), SLOT(selectCommand()));
    connect(add->ui->pushChangeIcon, SIGNAL(clicked()), SLOT(changeIcon()));
    connect(add->ui->lineEditName, SIGNAL(editingFinished()), SLOT(changeName()));
    connect(add->ui->lineEditCommand, SIGNAL(editingFinished()), SLOT(changeCommand()));
    connect(add->ui->lineEditComment, SIGNAL(editingFinished()), SLOT(changeComment()));
    connect(add->ui->pushAdd, SIGNAL(clicked()), SLOT(addCategoryMsgBox()));
    connect(add->ui->pushDelete, SIGNAL(clicked()), SLOT(delCategory()));
}


// Save button clicked
void mxmenueditor::on_buttonSave_clicked()
{
    QDir dir;
    QString file_name = current_item->text(1).remove("\"");
    QFileInfo fi(file_name);
    QString base_name = fi.fileName();
    QString out_name = dir.homePath() + "/.local/share/applications/" + base_name;
    QFile out(out_name);
    if (!out.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::critical(0, tr("Error"), tr("Could not save the file"));
    }
    all_local_desktop_files << out_name;
    out.write(ui->advancedEditor->toPlainText().toUtf8());
    out.flush();
    out.close();
    if (system("pgrep xfce4-panel") == 0) {
        system("xfce4-panel --restart");
    }
    ui->buttonSave->setDisabled(true);
    findReloadItem(base_name);
}

// About button clicked
void mxmenueditor::on_buttonAbout_clicked()
{
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Menu Editor"), "<p align=\"center\"><b><h2>" +
                       tr("MX Menu Editor") + "</h2></b></p><p align=\"center\">" + tr("Version: ") + version + "</p><p align=\"center\"><h3>" +
                       tr("Program for editing Xfce menu") +
                       "</h3></p><p align=\"center\"><a href=\"http://www.mepiscommunity.org/mx\">http://www.mepiscommunity.org/mx</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>", 0, this);
    msgBox.addButton(tr("Cancel"), QMessageBox::AcceptRole); // because we want to display the buttons in reverse order we use counter-intuitive roles.
    msgBox.addButton(tr("License"), QMessageBox::RejectRole);
    if (msgBox.exec() == QMessageBox::RejectRole) {
        system("xdg-open http://www.mepiscommunity.org/doc_mx/mx-menu-editor-license.html");
    }
}

void mxmenueditor::setEnabled(QString)
{
    ui->buttonSave->setEnabled(true);
}

// Help button clicked
void mxmenueditor::on_buttonHelp_clicked()
{
    system("xdg-open http://mepiscommunity.org/wiki/help-files/help-mx-menu-editor");
}

// Cancel button clicked
void mxmenueditor::on_buttonCancel_clicked()
{
    if (ui->buttonSave->isEnabled()) {
        save();
    }
    qApp->quit();
}

// ask whether to save edits or not
bool mxmenueditor::save()
{
    if (ui->buttonSave->isEnabled()) {
        int ans = QMessageBox::question(0, tr("Save changes?"), tr("Do you want to save your edits?"), tr("Save"), tr("Cancel"));
        if (ans == 0) {
            on_buttonSave_clicked();
            return true;
        }
    }
    ui->buttonSave->setDisabled(true);
    return false;
}

// delete .local file and reload files
void mxmenueditor::on_pushRestoreApp_clicked()
{
    QString file_name = current_item->text(1);
    file_name.remove("\"");
    QFileInfo fi(file_name);
    QString base_name = fi.fileName();
    QFile file(file_name);
    file.remove();
    all_local_desktop_files = listDesktopFiles("\"\"", QDir::homePath() + "/.local/share/applications/*");
    ui->pushRestoreApp->setDisabled(true);
    findReloadItem(base_name);
}

// find and reload item
void mxmenueditor::findReloadItem(QString base_name)
{
    base_name.remove("\"");
    ui->treeWidget->setCurrentItem(current_item); // change current item back to original selection
    ui->treeWidget->setCurrentItem(current_item->parent()); // change current item to reload category
    QTreeWidgetItemIterator it(current_item->treeWidget());
    while (*it) {
        QFileInfo fi((*it)->text(1).remove("\""));
        if ((fi.fileName() == base_name)) {
            ui->treeWidget->setCurrentItem(*it);
            return;
        }
        ++it;
    }
}

// find icon file location using the icon name form .desktop file
QString mxmenueditor::findIcon(QString icon_name)
{
    Output out;
    QStringList extList;
    extList << ".png" << ".svg" << ".xpm";

    out = runCmd("xfconf-query -c xsettings -p /Net/IconThemeName");
    if (out.str != "") {
        QString dir = "/usr/share/icons/" + out.str;
        if (QDir(dir).exists()) {
            foreach (QString ext, extList) {
                out = runCmd("find " + dir + " -iname " + icon_name + ext);
                if (out.str != "") {
                    QStringList files = out.str.split("\n");
                    return findBiggest(files);
                } else {
                    out = runCmd("find " + QDir::homePath() + "/.local/share/icons " + "/usr/share/icons /usr/share/pixmaps -iname " + icon_name + ext);
                    if (out.str != "") {
                        QStringList files = out.str.split("\n");
                        return findBiggest(files);
                    }
                }
            }
        }
    }
    return "";
}

// find largest icon
QString mxmenueditor::findBiggest(QStringList files)
{
    int max = 0;
    QString name_biggest;
    foreach (QString file, files) {
        QFile f(file);
        int size = f.size();
        if (size >= max) {
            name_biggest = file;
            max = size;
        }
    }
    return name_biggest;
}
