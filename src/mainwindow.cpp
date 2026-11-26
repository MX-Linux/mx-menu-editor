/**********************************************************************
 *  mainwindow.cpp
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
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "desktoputils.h"
#include "docviewer.h"

#include <QDebug>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Walloc-size-larger-than="
#endif
#include <QHash>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTextStream>
#include <QtAlgorithms>
#include <algorithm>
#include <utility>

#include "ui_addappdialog.h"

namespace
{
constexpr int ExecRole = Qt::UserRole + 1;

// System paths
constexpr auto SystemMenuFile = "/etc/xdg/menus/xfce-applications.menu";
constexpr auto SystemDesktopDirectories = "/usr/share/desktop-directories/";
constexpr auto SystemBinPath = "/usr/bin";
constexpr auto SystemIconsPath = "/usr/share/icons";
constexpr auto SystemDocPath = "/usr/share/doc/";
constexpr auto SystemPixmapsPath = "/usr/share/pixmaps";

// User paths (relative to home directory)
constexpr auto UserMenuPath = "/.config/menus";
constexpr auto UserDesktopDirectories = "/.local/share/desktop-directories/";

// Cached regex patterns to avoid repeated construction
const QRegularExpression regexNotFilter(QStringLiteral("^(?!<Not>).*$"));
}

MainWindow::MainWindow(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow),
      add(new AddAppDialog)
{
    qDebug().noquote() << QCoreApplication::applicationName() << "version:" << VERSION;
    connect(QApplication::instance(), &QApplication::aboutToQuit, this, &MainWindow::saveSettings);
    ui->setupUi(this);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    setConnections();

    if (ui->pushSave->icon().isNull()) {
        ui->pushSave->setIcon(QIcon(":/icons/dialog-ok.svg"));
    }

    // Remove focus from Cancel button
    ui->pushCancel->setAutoDefault(false);
    ui->pushCancel->setDefault(false);

    all_local_desktop_files = listDesktopFiles(QLatin1String(""), localApplicationsPath());
    all_usr_desktop_files = listDesktopFiles(QLatin1String(""), systemApplicationsPath());
    updateLocalBasenamesCache();

    loadMenuFiles();

    connect(ui->treeWidget, &QTreeWidget::itemSelectionChanged, this, &MainWindow::loadApps);
    connect(ui->lineEditSearch, &QLineEdit::textChanged, this, &MainWindow::filterTree);
    connect(ui->toolButtonCommand, &QToolButton::clicked, this, &MainWindow::selectCommand);
    connect(ui->lineEditName, &QLineEdit::editingFinished, this, &MainWindow::changeName);
    connect(ui->lineEditCommand, &QLineEdit::editingFinished, this, &MainWindow::changeCommand);
    connect(ui->lineEditComment, &QLineEdit::editingFinished, this, &MainWindow::changeComment);
    connect(ui->listWidgetEditCategories, &QListWidget::itemSelectionChanged, this, &MainWindow::enableDelete);
    connect(ui->pushDelete, &QPushButton::clicked, this, &MainWindow::delCategory);
    connect(ui->pushAdd, &QPushButton::clicked, this, &MainWindow::addCategoryMsgBox);
    connect(ui->pushChangeIcon, &QPushButton::clicked, this, &MainWindow::changeIcon);
    connect(ui->checkNotify, &QCheckBox::clicked, this, &MainWindow::changeNotify);
    connect(ui->checkHide, &QCheckBox::clicked, this, &MainWindow::changeHide);
    connect(ui->checkRunInTerminal, &QCheckBox::clicked, this, &MainWindow::changeTerminal);
    connect(ui->advancedEditor, &QTextEdit::textChanged, this, &MainWindow::onEditorTextChanged);
    connect(ui->pushAddApp, &QPushButton::clicked, this, &MainWindow::addAppMsgBox);
    connect(ui->lineEditName, &QLineEdit::textEdited, this, &MainWindow::setEnabled);
    connect(ui->lineEditCommand, &QLineEdit::textEdited, this, &MainWindow::setEnabled);
    connect(ui->lineEditComment, &QLineEdit::textEdited, this, &MainWindow::setEnabled);
    connect(add, &AddAppDialog::appSaved, this, &MainWindow::onCustomAppSaved);
    connect(add, &AddAppDialog::addCategoryRequested, this, &MainWindow::addCategoryMsgBox);

    QSize size = this->size();
    if (settings.contains(QStringLiteral("geometry"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
        if (this->isMaximized()) { // add option to resize if maximized
            this->resize(size);
            centerWindow();
        }
    }
}

MainWindow::~MainWindow()
{
    delete add;
    delete ui;
}

void MainWindow::loadMenuFiles()
{
    hashCategories.clear();
    hashExclude.clear();
    hashInclude.clear();
    resetInterface();
    ui->treeWidget->clear();

    const QString localDirectories = QDir::homePath() + UserDesktopDirectories;
    QStringList menuItems;
    for (const auto &fileName : listMenuFiles()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream in(&file);
            menuItems.append(readMenuRules(in, localDirectories));
        }
    }
    displayList(menuItems);
    populateAllCategories();
    filterTree(ui->lineEditSearch->text());
}

QStringList MainWindow::readMenuRules(QTextStream &in, const QString &localDirectories)
{
    enum class Rule { None, Include, Exclude };
    struct Menu {
        QString category;
        Rule rule = Rule::None;
        bool skipped = false;
    };
    QList<Menu> menus;
    QStringList menuItems;
    while (!in.atEnd()) {
        QString line = in.readLine().section(QStringLiteral("<!--"), 0, 0).trimmed();
        if (line == QLatin1String("<Menu>")) {
            Menu child;
            if (!menus.isEmpty()) {
                child.skipped = menus.last().skipped;
                // Nested folders display in their top-level category.
                if (menus.size() > 1) {
                    child.category = menus.last().category;
                }
            }
            menus.append(child);
            continue;
        }
        if (menus.isEmpty()) {
            continue;
        }
        if (line == QLatin1String("</Menu>")) {
            menus.removeLast();
            continue;
        }

        auto &menu = menus.last();
        if (menu.skipped) {
            continue;
        }
        if (line.startsWith(QLatin1String("<Directory>"))) {
            const auto directory = line.section('>', 1, 1).section('<', 0, 0).trimmed();
            QString path = QDir(localDirectories).filePath(directory);
            if (!QFileInfo::exists(path)) {
                path = QDir(SystemDesktopDirectories).filePath(directory);
            }
            const auto name = getCatName(path);
            menu.skipped = name == QLatin1String("Wine") || name == QLatin1String("Other");
            if (!menu.skipped && menu.category.isEmpty() && !name.isEmpty()) {
                menu.category = name;
                menuItems.append(name);
            }
        } else if (line == QLatin1String("<Include>")) {
            menu.rule = Rule::Include;
        } else if (line == QLatin1String("<Exclude>")) {
            menu.rule = Rule::Exclude;
        } else if (line == QLatin1String("</Include>") || line == QLatin1String("</Exclude>")) {
            menu.rule = Rule::None;
        } else if (!menu.category.isEmpty() && menu.rule != Rule::None) {
            if (menu.rule == Rule::Include && line.contains(QLatin1String("<Category>"))) {
                line = line.remove(QStringLiteral("<Category>")).remove(QStringLiteral("</Category>")).trimmed();
                if (!hashCategories.contains(menu.category, line)) {
                    hashCategories.insert(menu.category, line);
                }
            } else if (line.contains(QLatin1String("<Filename>"))) {
                line = line.remove(QStringLiteral("<Filename>")).remove(QStringLiteral("</Filename>")).trimmed();
                auto &files = menu.rule == Rule::Include ? hashInclude : hashExclude;
                if (!files.contains(menu.category, line)) {
                    files.insert(menu.category, line);
                }
            }
        }
    }
    return menuItems;
}

void MainWindow::populateAllCategories()
{
    resetInterface();
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); ++i) {
        auto *categoryItem = ui->treeWidget->topLevelItem(i);
        populateCategory(categoryItem);
    }
}

void MainWindow::populateCategory(QTreeWidgetItem *categoryItem)
{
    if (categoryItem == nullptr) {
        return;
    }

    qDeleteAll(categoryItem->takeChildren());

    const auto &categoryName = categoryItem->text(0);
    const QStringList categories = hashCategories.values(categoryName);
    const QStringList includes = hashInclude.values(categoryName);
    const QStringList excludes = hashExclude.values(categoryName);

    QStringList includesUsr;
    QStringList includesLocal;
    includesUsr.reserve(includes.size());
    includesLocal.reserve(includes.size());
    for (const auto &file : std::as_const(includes)) {
        includesUsr << systemApplicationsPath() + "/" + file;
        includesLocal << localApplicationsPath() + "/" + file;
    }

    QStringList searchPatterns;
    searchPatterns.reserve(categories.size());
    for (const auto &category : std::as_const(categories)) {
        searchPatterns << DesktopUtils::categoryMatchPattern(category);
    }
    const QString searchString = searchPatterns.join(QLatin1Char('|'));

    QStringList usrDesktopFiles = includesUsr;
    QStringList localDesktopFiles = includesLocal;
    if (!searchString.isEmpty()) {
        usrDesktopFiles.append(listDesktopFiles(searchString, systemApplicationsPath()));
        localDesktopFiles.append(listDesktopFiles(searchString, localApplicationsPath()));
    }
    usrDesktopFiles.removeDuplicates();
    localDesktopFiles.removeDuplicates();

    for (const auto &baseName : std::as_const(excludes)) {
        usrDesktopFiles.removeAll(systemApplicationsPath() + "/" + baseName);
        localDesktopFiles.removeAll(localApplicationsPath() + "/" + baseName);
    }

    for (const auto &localName : std::as_const(localDesktopFiles)) {
        QFileInfo fiLocal(localName);
        auto *app = addToTree(categoryItem, localName);
        if (app != nullptr && fiLocal.path() == localApplicationsPath()
            && all_usr_desktop_files.contains(systemApplicationsPath() + "/" + fiLocal.fileName())) {
            app->setData(0, Qt::UserRole, "restore");
        }
    }

    for (const auto &file : std::as_const(usrDesktopFiles)) {
        QFileInfo fi(file);
        const auto baseName = fi.fileName();
        if (!local_basenames_cache.contains(baseName)) {
            addToTree(categoryItem, file);
        }
    }

    categoryItem->sortChildren(1, Qt::AscendingOrder);
}

void MainWindow::insertAppIntoCategories(const QString &filePath, const QStringList &categories)
{
    if (filePath.isEmpty() || categories.isEmpty()) {
        return;
    }

    const QFileInfo newFileInfo(filePath);
    const QString baseName = newFileInfo.fileName();

    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); ++i) {
        auto *categoryItem = ui->treeWidget->topLevelItem(i);
        const auto existingCategories = hashCategories.values(categoryItem->text(0));
        const bool belongs = std::ranges::any_of(categories, [&](const QString &cat) {
            return existingCategories.contains(cat);
        });
        if (!belongs) {
            continue;
        }

        for (int j = 0; j < categoryItem->childCount(); ++j) {
            auto *child = categoryItem->child(j);
            const auto childPath = child->text(1);
            if (childPath == filePath || childPath == systemApplicationsPath() + "/" + baseName) {
                if (child == current_item) {
                    resetInterface();
                }
                delete categoryItem->takeChild(j);
                break;
            }
        }

        auto *child = addToTree(categoryItem, filePath);
        if (child != nullptr && QFileInfo::exists(systemApplicationsPath() + "/" + baseName)) {
            child->setData(0, Qt::UserRole, "restore");
        }
        categoryItem->sortChildren(1, Qt::AscendingOrder);
        categoryItem->setExpanded(true);
    }
}

void MainWindow::setConnections()
{
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushCancel, &QPushButton::clicked, this, &MainWindow::reject);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->pushRestoreApp, &QPushButton::clicked, this, &MainWindow::pushRestoreApp_clicked);
    connect(ui->pushDuplicateApp, &QPushButton::clicked, this, &MainWindow::pushDuplicateApp_clicked);
    connect(ui->pushSave, &QPushButton::clicked, this, &MainWindow::pushSave_clicked);
}

// get Name= from .directory file
QString MainWindow::getCatName(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QString defaultName;
    QHash<QString, QString> localizedNames;
    bool inDesktopEntry = false;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const auto line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inDesktopEntry || !line.startsWith(QLatin1String("Name"))) {
            continue;
        }
        if (line.startsWith(QLatin1String("Name["))) {
            const int closeIdx = line.indexOf(QLatin1Char(']'));
            if (closeIdx > 5 && line.size() > closeIdx + 1 && line.at(closeIdx + 1) == QLatin1Char('=')) {
                const auto locale = line.mid(5, closeIdx - 5);
                const auto value = line.mid(closeIdx + 2).trimmed();
                localizedNames.insert(locale, value);
            }
        } else if (line.startsWith(QLatin1String("Name="))) {
            defaultName = line.mid(5).trimmed();
        }
    }

    return DesktopUtils::pickLocalizedName(defaultName, localizedNames);
}

// return a list of .menu files
QStringList MainWindow::listMenuFiles()
{
    const auto homePath = QDir::homePath();
    QStringList menuFiles(SystemMenuFile);

    // add menu files from user directory
    const QString userMenuPath = homePath + UserMenuPath;
    QDirIterator it(userMenuPath, QStringList() << "*.menu", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        menuFiles << it.next();
    }
    return menuFiles;
}

QString MainWindow::localApplicationsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
}

QString MainWindow::systemApplicationsPath()
{
    return QStringLiteral("/usr/share/applications");
}

QString MainWindow::createLocalLauncher(const QString &baseName, const QByteArray &contents, bool useCopyName)
{
    const QDir local(localApplicationsPath());
    const QDir system(systemApplicationsPath());
    const auto occupied = [&](const QString &name) {
        const QFileInfo localFile(local.filePath(name));
        const QFileInfo systemFile(system.filePath(name));
        return localFile.exists() || localFile.isSymLink() || systemFile.exists() || systemFile.isSymLink();
    };
    QString candidate = baseName;
    QFile out;
    for (;;) {
        if (useCopyName || occupied(candidate)) {
            candidate = DesktopUtils::uniqueCopyFileName(baseName, occupied);
        }
        out.setFileName(local.filePath(candidate));
        if (out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            break;
        }
        if (!occupied(candidate)) {
            return {};
        }
    }
    if (out.write(contents) != contents.size() || !out.flush()) {
        out.close();
        QFile::remove(out.fileName());
        return {};
    }
    out.close();
    return out.fileName();
}

void MainWindow::updateLocalBasenamesCache()
{
    local_basenames_cache.clear();
    for (const auto &localName : std::as_const(all_local_desktop_files)) {
        QFileInfo fLocal(localName);
        if (fLocal.path() == localApplicationsPath()) {
            local_basenames_cache.insert(fLocal.fileName());
        }
    }
}

// Display sorted list of menu items in the treeWidget
void MainWindow::displayList(QStringList menuItems)
{
    QTreeWidgetItem *topLevelItem = nullptr;
    ui->treeWidget->setHeaderLabel(QLatin1String(""));
    ui->treeWidget->setSortingEnabled(true);
    menuItems.removeDuplicates();
    for (const auto &item : menuItems) {
        topLevelItem = new QTreeWidgetItem(ui->treeWidget, QStringList(item));
        // topLevelItem look
        QFont font;
        font.setBold(true);
        topLevelItem->setForeground(0, QBrush(Qt::darkGreen));
        topLevelItem->setFont(0, font);
    }
    ui->treeWidget->sortItems(0, Qt::AscendingOrder);
}

// load the applications in the selected category
void MainWindow::loadApps()
{
    auto *currentItem = ui->treeWidget->currentItem();
    if (currentItem == nullptr) {
        return;
    }

    if (ui->pushSave->isEnabled() && !save()) {
        // Restore previous selection if save was cancelled
        auto *itemToRestore = current_item;
        if (itemToRestore != nullptr) {
            const QSignalBlocker blocker(ui->treeWidget);
            ui->treeWidget->setCurrentItem(itemToRestore);
        }
        return;
    }

    // Re-fetch current item after potential save operation
    currentItem = ui->treeWidget->currentItem();
    if (currentItem == nullptr) {
        current_item = nullptr;
        return;
    }

    if (currentItem->parent() == nullptr) {
        current_item = nullptr;
        resetInterface();
        currentItem->setExpanded(true);
        return;
    }

    loadItem(currentItem, 0);
}

void MainWindow::filterTree(const QString &query)
{
    const QString trimmed = query.trimmed();
    const bool hasQuery = !trimmed.isEmpty();

    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); ++i) {
        auto *categoryItem = ui->treeWidget->topLevelItem(i);
        const bool categoryMatch = categoryItem->text(0).contains(trimmed, Qt::CaseInsensitive);
        bool anyChildVisible = false;

        for (int j = 0; j < categoryItem->childCount(); ++j) {
            auto *child = categoryItem->child(j);
            const auto execCommand = child->data(0, ExecRole).toString();
            const bool match = categoryMatch || child->text(0).contains(trimmed, Qt::CaseInsensitive)
                               || child->text(1).contains(trimmed, Qt::CaseInsensitive)
                               || execCommand.contains(trimmed, Qt::CaseInsensitive);
            child->setHidden(hasQuery && !match);
            anyChildVisible = anyChildVisible || !child->isHidden();
        }

        const bool hideCategory = hasQuery && !categoryMatch && !anyChildVisible;
        categoryItem->setHidden(hideCategory);
        // Only change expanded state when there's a search query
        // Otherwise preserve the current expanded state
        if (hasQuery) {
            categoryItem->setExpanded(!hideCategory && anyChildVisible);
        }
    }

    auto *current = ui->treeWidget->currentItem();
    if (current != nullptr && current->isHidden()) {
        if (ui->pushSave->isEnabled() && !save()) {
            return;
        }
        ui->treeWidget->clearSelection();
        resetInterface();
    }
}

void MainWindow::updateRestoreButtonState(const QString &fileName)
{
    const QString baseName = QFileInfo(fileName).fileName();
    const QString localPrefix = localApplicationsPath() + "/";
    const bool isLocal = fileName.startsWith(localPrefix);
    const bool hasUsrCopy = fileName == localPrefix + baseName && QFile::exists(systemApplicationsPath() + "/" + baseName);

    if (isLocal && hasUsrCopy) {
        ui->pushRestoreApp->setText(tr("Restore original item"));
        ui->pushRestoreApp->setEnabled(true);
    } else if (isLocal) {
        ui->pushRestoreApp->setText(tr("Delete"));
        ui->pushRestoreApp->setEnabled(true);
    } else {
        ui->pushRestoreApp->setText(tr("Restore original item"));
        ui->pushRestoreApp->setEnabled(false);
    }
}

// add .desktop item to treeWidget
QTreeWidgetItem *MainWindow::addToTree(QTreeWidgetItem *parent, const QString &fileName)
{
    if (!QFileInfo::exists(fileName) || parent == nullptr) {
        return nullptr;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return nullptr;
    }

    QString defaultName;
    QHash<QString, QString> localizedNames;
    QString execCommand;
    bool isHidden = false;
    QTextStream in(&file);

    // Single forward scan over the [Desktop Entry] section, looking for Name=,
    // Name[locale]=, Exec= and NoDisplay= (in any order - the spec doesn't fix one).
    bool inDesktopEntry = false;
    while (!in.atEnd()) {
        const auto line = in.readLine();
        if (line.startsWith(QLatin1Char('['))) {
            if (inDesktopEntry) {
                break; // reached the next section
            }
            inDesktopEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inDesktopEntry) {
            continue;
        }
        if (line.startsWith(QLatin1String("Name["))) {
            const int closeIdx = line.indexOf(QLatin1Char(']'));
            if (closeIdx > 5 && line.size() > closeIdx + 1 && line.at(closeIdx + 1) == QLatin1Char('=')) {
                const auto locale = line.mid(5, closeIdx - 5);
                const auto value = line.mid(closeIdx + 2).trimmed();
                localizedNames.insert(locale, value);
            }
        } else if (line.startsWith(QLatin1String("Name=")) && defaultName.isEmpty()) {
            defaultName = line.section(QStringLiteral("="), 1).trimmed();
        } else if (line.startsWith(QLatin1String("Exec=")) && execCommand.isEmpty()) {
            execCommand = line.section(QStringLiteral("="), 1).trimmed();
        } else if (line.startsWith(QLatin1String("NoDisplay="))) {
            const auto value = line.section(QStringLiteral("="), 1).trimmed();
            isHidden = (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        }
    }
    file.close();

    const QString displayName = DesktopUtils::pickLocalizedName(defaultName, localizedNames);
    if (displayName.isEmpty()) {
        return nullptr;
    }

    auto *childItem = new QTreeWidgetItem(parent);
    if (isHidden) {
        childItem->setForeground(0, QBrush(Qt::gray));
    }
    childItem->setText(0, displayName);
    childItem->setText(1, fileName);
    childItem->setData(0, ExecRole, execCommand);
    return childItem;
}

QStringList MainWindow::listDesktopFiles(const QString &searchString, const QString &location, bool *hadError)
{
    if (hadError != nullptr) {
        *hadError = false;
    }

    QStringList result;

    const QFileInfo locationInfo(location);
    if (!locationInfo.exists() || !locationInfo.isDir()) {
        if (hadError != nullptr) {
            *hadError = true;
        }
        return result;
    }

    if (searchString.isEmpty()) {
        // No search string, find all .desktop files recursively
        QDirIterator it(location, QStringList() << "*.desktop", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            result.append(it.next());
        }
    } else {
        // Search for files containing the search pattern
        const QRegularExpression regex(searchString);
        if (!regex.isValid()) {
            if (hadError != nullptr) {
                *hadError = true;
            }
            return result;
        }

        QDirIterator it(location, QStringList() << "*.desktop", QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            const QString filePath = it.next();
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                while (!in.atEnd()) {
                    const QString line = in.readLine();
                    if (regex.match(line).hasMatch()) {
                        result.append(filePath);
                        break;
                    }
                }
            }
        }
    }

    return result;
}

void MainWindow::loadItem(QTreeWidgetItem *item, int /*unused*/)
{
    if (item == nullptr) {
        return;
    }

// Qt 6: pixmap() returns by value, use isNull() to check if empty
    // execute if not topLevel item is selected
    if (item->parent() != nullptr) {
        const auto file_name = item->text(1).trimmed();
        resetInterface();

        const QSize size = ui->labelIcon->size();

        QFile file(file_name);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "Can't open file: " << file.fileName();
            return;
        }
        QString line;
        QString content;

        // reset lines for Exec and Icon
        ui->labelCommand->clear();
        ui->labelIcon->clear();
        // Only fields from the [Desktop Entry] group are relevant to the Quick Editor;
        // later groups like [Desktop Action ...] can redeclare Name=/Exec=/etc. for a
        // specific action and must not overwrite the main entry's values.
        bool inDesktopEntrySection = false;
        while (!file.atEnd()) {
            line = file.readLine();
            content.append(line);

            const QString trimmedLine = line.trimmed();
            if (trimmedLine.startsWith(QLatin1Char('['))) {
                inDesktopEntrySection = trimmedLine == QLatin1String("[Desktop Entry]");
                continue;
            }
            if (!inDesktopEntrySection) {
                continue;
            }

            if (line.startsWith(QLatin1String("Categories="))) {
                line = line.section(QStringLiteral("="), 1);
                const auto categories = line.trimmed().split(';', Qt::SkipEmptyParts);
                ui->listWidgetEditCategories->addItems(categories);
            } else if (line.startsWith(QLatin1String("Name="))) {
                line = line.section(QStringLiteral("="), 1).trimmed();
                ui->lineEditName->setText(line);
            } else if (line.startsWith(QLatin1String("Comment="))) {
                line = line.section(QStringLiteral("="), 1).trimmed();
                ui->lineEditComment->setText(line);
            } else if (line.startsWith(QLatin1String("Exec="))) {
                line = line.section(QStringLiteral("="), 1).trimmed();
                if (ui->lineEditCommand->text()
                        .isEmpty()) { // some .desktop files have multiple Exec= display first one
                    ui->lineEditCommand->setText(line);
                }
                ui->lineEditCommand->home(false);
            } else if (line.startsWith(QLatin1String("StartupNotify="))) {
                const QString value = line.section(QStringLiteral("="), 1).trimmed();
                ui->checkNotify->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            } else if (line.startsWith(QLatin1String("NoDisplay="))) {
                const QString value = line.section(QStringLiteral("="), 1).trimmed();
                ui->checkHide->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            } else if (line.startsWith(QLatin1String("Terminal="))) {
                const QString value = line.section(QStringLiteral("="), 1).trimmed();
                ui->checkRunInTerminal->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            } else if (line.startsWith(QLatin1String("Icon="))) {
                line = line.section(QStringLiteral("="), 1).trimmed();
                if (!line.isEmpty() && ui->labelIcon->pixmap(Qt::ReturnByValue).isNull()) { // some .desktop files have multiple Icon= display first
                    ui->labelIcon->setPixmap(findIcon(line, size).scaled(size));
                }
            }
        }
        if (file.error() != QFileDevice::NoError) {
            resetInterface();
            return;
        }
        file.close();
        {
            QSignalBlocker editorBlocker(ui->advancedEditor);
            QSignalBlocker docBlocker(ui->advancedEditor->document());
            ui->advancedEditor->setPlainText(content);
            ui->advancedEditor->document()->setModified(false);
            ui->advancedEditor->document()->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
        }
        advancedEditorBaseline = content;
        current_item = item;
        enableEdit();
        updateRestoreButtonState(file_name);
    }
}

// select command to be used
void MainWindow::selectCommand()
{
    const auto selected = QFileDialog::getOpenFileName(this, tr("Select executable file"), SystemBinPath);
    if (!selected.isEmpty()) {
        ui->lineEditCommand->setText(DesktopUtils::encodeExecPath(selected));
        ui->pushSave->setEnabled(true);
        changeCommand();
    }
}

void MainWindow::resetInterface()
{
    current_item = nullptr;
    ui->listWidgetEditCategories->clear();
    {
        QSignalBlocker editorBlocker(ui->advancedEditor);
        QSignalBlocker docBlocker(ui->advancedEditor->document());
        ui->advancedEditor->clear();
        ui->advancedEditor->document()->setModified(false);
        ui->advancedEditor->document()->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
    }
    advancedEditorBaseline.clear();
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
    ui->pushRestoreApp->setText(tr("Restore original item"));
    ui->pushDuplicateApp->setDisabled(true);
    ui->pushSave->setDisabled(true);
    ui->labelIcon->setPixmap(QPixmap());
}

void MainWindow::saveSettings()
{
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
}

void MainWindow::onEditorTextChanged()
{
    const auto currentText = ui->advancedEditor->toPlainText();
    if (currentText == advancedEditorBaseline) {
        ui->pushSave->setEnabled(false);
        QSignalBlocker blocker(ui->advancedEditor->document());
        ui->advancedEditor->document()->setModified(false);
        return;
    }
    ui->pushSave->setEnabled(true);
}

void MainWindow::enableEdit()
{
    ui->checkNotify->setEnabled(true);
    ui->checkHide->setEnabled(true);
    ui->checkRunInTerminal->setEnabled(true);
    ui->toolButtonCommand->setEnabled(true);
    ui->pushAdd->setEnabled(true);
    ui->pushChangeIcon->setEnabled(true);
    ui->pushDuplicateApp->setEnabled(true);
    ui->advancedEditor->setEnabled(true);
    ui->lineEditCommand->setEnabled(true);
    ui->lineEditComment->setEnabled(true);
    ui->lineEditName->setEnabled(true);
}

void MainWindow::changeIcon()
{
    QFileDialog dialog;
    QString selected;
    dialog.setFilter(QDir::Hidden);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(tr("Image Files (*.png *.jpg *.bmp *.xpm *.svg)"));
    dialog.setDirectory(SystemIconsPath);
    if (dialog.exec() == QDialog::Accepted) {
        QStringList selectedList = dialog.selectedFiles();
        if (!selectedList.isEmpty()) {
            selected = selectedList.at(0);
        }
    }
    if (!selected.isEmpty()) {
        if (DesktopUtils::containsInvalidDesktopChars(selected)) {
            QMessageBox::warning(this, tr("Error"), tr("Icon path cannot contain newlines or control characters."));
            return;
        }
        ui->pushSave->setEnabled(true);
        ui->advancedEditor->setPlainText(DesktopUtils::setEntryValue(
            ui->advancedEditor->toPlainText(), QStringLiteral("Icon"), selected));
        ui->labelIcon->setPixmap(QPixmap(selected));
    }
}

void MainWindow::changeName()
{
    const auto newName = ui->lineEditName->text();
    if (DesktopUtils::containsInvalidDesktopChars(newName)) {
        QMessageBox::warning(this, tr("Error"), tr("Application name cannot contain newlines or control characters."));
        return;
    }
    ui->pushSave->setEnabled(true);
    if (newName.isEmpty()) {
        return;
    }
    ui->advancedEditor->setPlainText(
        DesktopUtils::setEntryValue(ui->advancedEditor->toPlainText(), QStringLiteral("Name"), newName));
}

void MainWindow::changeCommand()
{
    const auto newCommand = ui->lineEditCommand->text();
    if (DesktopUtils::containsInvalidDesktopChars(newCommand)) {
        QMessageBox::warning(this, tr("Error"), tr("Command cannot contain newlines or control characters."));
        return;
    }
    ui->pushSave->setEnabled(true);
    if (newCommand.isEmpty()) {
        return;
    }
    ui->advancedEditor->setPlainText(DesktopUtils::setEntryValue(
        ui->advancedEditor->toPlainText(), QStringLiteral("Exec"), newCommand));
}

void MainWindow::changeComment()
{
    const auto newComment = ui->lineEditComment->text();
    if (DesktopUtils::containsInvalidDesktopChars(newComment)) {
        QMessageBox::warning(this, tr("Error"), tr("Comment cannot contain newlines or control characters."));
        return;
    }
    ui->pushSave->setEnabled(true);
    ui->advancedEditor->setPlainText(DesktopUtils::setEntryValue(
        ui->advancedEditor->toPlainText(), QStringLiteral("Comment"), newComment.isEmpty() ? QString() : newComment));
}

void MainWindow::enableDelete()
{
    ui->pushDelete->setEnabled(true);
}

void MainWindow::delCategory()
{
    const int row = ui->listWidgetEditCategories->currentRow();
    QScopedPointer<QListWidgetItem> item(ui->listWidgetEditCategories->takeItem(row));
    if (item.isNull()) {
        return;
    }
    const auto text = ui->advancedEditor->toPlainText();
    auto categories = DesktopUtils::entryValue(text, QStringLiteral("Categories")).split(';', Qt::SkipEmptyParts);
    categories.removeAll(item->text());
    ui->advancedEditor->setPlainText(
        DesktopUtils::setEntryValue(text, QStringLiteral("Categories"), categories.join(';') + ';'));
    if (ui->listWidgetEditCategories->count() == 0) {
        ui->pushDelete->setDisabled(true);
    }
}

void MainWindow::changeNotify(bool checked)
{
    ui->pushSave->setEnabled(true);
    const QString str = checked ? QStringLiteral("true") : QStringLiteral("false");
    ui->advancedEditor->setPlainText(DesktopUtils::setEntryValue(
        ui->advancedEditor->toPlainText(), QStringLiteral("StartupNotify"), str));
}

// hide or show the item in the menu
void MainWindow::changeHide(bool checked)
{
    ui->pushSave->setEnabled(true);
    const QString str = checked ? QStringLiteral("true") : QStringLiteral("false");
    ui->advancedEditor->setPlainText(
        DesktopUtils::setEntryValue(ui->advancedEditor->toPlainText(), QStringLiteral("NoDisplay"), str));
}

// change "run in terminal" setting
void MainWindow::changeTerminal(bool checked)
{
    ui->pushSave->setEnabled(true);
    const QString str = checked ? QStringLiteral("true") : QStringLiteral("false");
    ui->advancedEditor->setPlainText(DesktopUtils::setEntryValue(
        ui->advancedEditor->toPlainText(), QStringLiteral("Terminal"), str));
}

// list categories of the displayed items
QStringList MainWindow::listCategories() const
{
    QStringList categories;
    for (auto it = hashCategories.cbegin(); it != hashCategories.cend(); ++it) {
        categories << it.value();
    }
    categories.removeDuplicates();
    categories = categories.filter(regexNotFilter);
    categories.sort();
    return categories;
}

// display add category message box
void MainWindow::addCategoryMsgBox()
{
    const QStringList &categories = listCategories();

    auto *dialog = new QDialog(add, Qt::Dialog);
    dialog->setAttribute(Qt::WA_DeleteOnClose); // Auto-delete when closed
    dialog->setWindowTitle(tr("Choose category"));
    const int height = 80;
    const int width = 250;
    dialog->resize(width, height);

    auto *comboBox = new QComboBox(dialog);
    auto *buttonBox = new QDialogButtonBox(dialog);

    comboBox->clear();
    // comboBox->setEditable(true);
    comboBox->addItems(categories);
    comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // because we want to display the buttons in reverse order we use counter-intuitive roles.
    buttonBox->addButton(tr("Cancel"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(tr("OK"), QDialogButtonBox::RejectRole);
    connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, [this, comboBox, dialog]() {
        if (ui->lineEditCommand->isEnabled()) { // started from editor
            addCategory(comboBox->currentText());
        } else { // running from the add-custom-app window
            add->addCategoryToList(comboBox->currentText());
        }
        dialog->close();
    });

    auto *layout = new QFormLayout(dialog);
    layout->addRow(comboBox);
    layout->addRow(buttonBox);

    dialog->setLayout(layout);
    dialog->show();
}

void MainWindow::centerWindow()
{
    const auto screens = QApplication::screens();
    if (screens.isEmpty()) {
        return;
    }
    QRect screenGeometry = screens.constFirst()->geometry();
    int x = (screenGeometry.width() - this->width()) / 2;
    int y = (screenGeometry.height() - this->height()) / 2;
    this->move(x, y);
}

// add selected categorory to the .desktop file
void MainWindow::addCategory(const QString &category)
{
    if (category.isEmpty()) {
        return;
    }
    const auto text = ui->advancedEditor->toPlainText();
    auto categories = DesktopUtils::entryValue(text, QStringLiteral("Categories")).split(';', Qt::SkipEmptyParts);
    if (!categories.contains(category)) {
        categories.append(category);
        ui->advancedEditor->setPlainText(
            DesktopUtils::setEntryValue(text, QStringLiteral("Categories"), categories.join(';') + ';'));
    }
    ui->listWidgetEditCategories->clear();
    ui->listWidgetEditCategories->addItems(categories);
    ui->pushDelete->setEnabled(false);
}

// display add application message box
void MainWindow::addAppMsgBox()
{
    if (ui->pushSave->isEnabled() && !save()) {
        return;
    }
    add->show();
    resetInterface();
    ui->treeWidget->collapseAll();
}

void MainWindow::onCustomAppSaved(const QString &path, const QStringList &categories)
{
    if (!all_local_desktop_files.contains(path)) {
        all_local_desktop_files << path;
        updateLocalBasenamesCache();
    }
    insertAppIntoCategories(path, categories);
    filterTree(ui->lineEditSearch->text());
    findReloadItem(path);
}

bool MainWindow::pushSave_clicked()
{
    if (current_item == nullptr) {
        qWarning() << "pushSave_clicked: current_item is null";
        return false;
    }

    // The Save shortcut can fire while a Quick Editor field still has focus.
    // Finish that edit exactly as clicking the Save button would.
    if (ui->lineEditName->hasFocus() || ui->lineEditCommand->hasFocus() || ui->lineEditComment->hasFocus()) {
        ui->pushSave->setFocus(Qt::ShortcutFocusReason);
    }
    const QString editorText = ui->advancedEditor->toPlainText();
    const auto categories = DesktopUtils::entryValue(editorText, QStringLiteral("Categories"));
    // Existing launchers included by filename can legitimately have no categories.
    if (categories.split(';', Qt::SkipEmptyParts).isEmpty()
        && categories != DesktopUtils::entryValue(advancedEditorBaseline, QStringLiteral("Categories"))) {
        QMessageBox::warning(this, tr("Error"), tr("Choose category"));
        return false;
    }
    const QString execCommand = DesktopUtils::entryValue(editorText, QStringLiteral("Exec"));

    // Validate the Exec command before saving
    if (!AddAppDialog::confirmExecutableExists(this, execCommand)) {
        return false;
    }

    const auto file_name = current_item->text(1);
    const QFileInfo fi(file_name);
    const auto base_name = fi.fileName();
    const auto applicationsDir = localApplicationsPath();
    if (!QFileInfo::exists(applicationsDir) && !QDir().mkpath(applicationsDir)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not create the applications directory"));
        return false;
    }
    const auto out_name = file_name.startsWith(applicationsDir + "/") ? file_name : applicationsDir + "/" + base_name;
    const QByteArray contents = editorText.toUtf8();
    QSaveFile out(out_name);
    if (!out.open(QIODevice::WriteOnly) || out.write(contents) != contents.size() || !out.commit()) {
        QMessageBox::critical(this, tr("Error"), tr("Could not save the file"));
        return false;
    }
    if (!all_local_desktop_files.contains(out_name)) {
        all_local_desktop_files << out_name;
        updateLocalBasenamesCache();
    }
    {
        QSignalBlocker editorBlocker(ui->advancedEditor);
        QSignalBlocker docBlocker(ui->advancedEditor->document());
        ui->advancedEditor->document()->setModified(false);
        ui->advancedEditor->document()->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
    }
    advancedEditorBaseline = editorText;
    {
        QSignalBlocker treeBlocker(ui->treeWidget);
        populateAllCategories();
    }
    restartPanel();
    ui->pushSave->setDisabled(true);
    findReloadItem(out_name);
    return true;
}

void MainWindow::restartPanel()
{
    auto *pgrepProcess = new QProcess;
    connect(pgrepProcess, &QProcess::finished, pgrepProcess, [pgrepProcess](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0 && !QProcess::startDetached(QStringLiteral("xfce4-panel"), {QStringLiteral("--restart")})) {
            qWarning() << "Failed to restart xfce4-panel";
        }
        pgrepProcess->deleteLater();
    });
    connect(pgrepProcess, &QProcess::errorOccurred, pgrepProcess, [pgrepProcess](QProcess::ProcessError) {
        qWarning() << "Failed to run pgrep:" << pgrepProcess->errorString();
        pgrepProcess->deleteLater();
    });
    pgrepProcess->start(QStringLiteral("pgrep"), {QStringLiteral("xfce4-panel")});
}

void MainWindow::pushAbout_clicked()
{
    QMessageBox msgBox(
        QMessageBox::NoIcon, tr("About MX Menu Editor"),
        "<p align=\"center\"><b><h2>" + tr("MX Menu Editor") + "</h2></b></p><p align=\"center\">" + tr("Version: ")
            + VERSION + "</p><p align=\"center\"><h3>" + tr("Program for editing Xfce menu")
            + R"(</h3></p><p align="center"><a href="http://mxlinux.org">http://mxlinux.org</a><br /></p><p align="center">)"
            + tr("Copyright (c) MX Linux") + "<br /><br /></p>", QMessageBox::NoButton, this);
    auto *btnLicense = msgBox.addButton(tr("License"), QMessageBox::HelpRole);
    auto *btnChangelog = msgBox.addButton(tr("Changelog"), QMessageBox::HelpRole);
    auto *btnCancel = msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        displayDoc(QString(SystemDocPath) + QStringLiteral("mx-menu-editor/license.html"),
                   tr("%1 License").arg(this->windowTitle()));
    } else if (msgBox.clickedButton() == btnChangelog) {
        QDialog changelog(this);
        const int width = 500;
        const int height = 600;
        changelog.resize(width, height);

        auto *text = new QTextEdit(&changelog);
        text->setReadOnly(true);
        QProcess process;
        process.start(QStringLiteral("gzip"),
                      QStringList {QStringLiteral("-dc"),
                                   QString(SystemDocPath) + QFileInfo(QCoreApplication::applicationFilePath()).fileName()
                                       + "/changelog.gz"});
        if (!process.waitForFinished(3000)) {
            process.kill();
            process.waitForFinished(1000);
            QMessageBox::warning(this, tr("Error"), tr("Could not read the changelog (timed out)."));
            return;
        }
        if (process.exitCode() != 0) {
            QMessageBox::warning(this, tr("Error"), tr("Could not read the changelog."));
            return;
        }
        text->setText(process.readAllStandardOutput());

        auto *btnClose = new QPushButton(tr("&Close"), &changelog);
        btnClose->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
        connect(btnClose, &QPushButton::clicked, &changelog, &QDialog::close);

        auto *layout = new QVBoxLayout(&changelog);
        layout->addWidget(text);
        layout->addWidget(btnClose);
        changelog.setLayout(layout);
        changelog.exec();
    }
}

void MainWindow::setEnabled(const QString & /*unused*/)
{
    ui->pushSave->setEnabled(true);
}

void MainWindow::pushHelp_clicked()
{
    const auto path = QString(SystemDocPath) + QStringLiteral("mx-menu-editor/mx-menu-editor.html");
    displayDoc(path, tr("MX Menu Editor Help"), true);
}

void MainWindow::reject()
{
    if (ui->pushSave->isEnabled() && !save()) {
        return;
    }
    QDialog::reject();
}

// ask whether to save edits or not
bool MainWindow::save()
{
    if (!ui->pushSave->isEnabled()) {
        return true; // No unsaved changes, continue
    }

    const auto answer = QMessageBox::question(this, tr("Save changes?"), tr("Do you want to save your edits?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        return pushSave_clicked(); // Continue only if the save actually succeeded
    }
    if (answer == QMessageBox::Discard) {
        ui->pushSave->setDisabled(true);
        return true; // Discarded, continue
    }
    // Cancel was clicked
    return false; // Don't continue, stay on current item
}

// delete .local file and reload files
void MainWindow::pushRestoreApp_clicked()
{
    if (current_item == nullptr) {
        return;
    }
    const auto file_name = current_item->text(1);
    const QFileInfo fi(file_name);
    const auto base_name = fi.fileName();
    const auto applicationsDir = localApplicationsPath();
    if (!file_name.startsWith(applicationsDir + "/")) {
        return;
    }
    QFile file(file_name);
    if (file.exists() && !file.remove()) {
        QMessageBox::critical(this, tr("Error"), tr("Could not restore the application: %1").arg(file.errorString()));
        return;
    }
    all_local_desktop_files = listDesktopFiles(QLatin1String(""), localApplicationsPath());
    updateLocalBasenamesCache();
    ui->pushRestoreApp->setDisabled(true);
    {
        QSignalBlocker treeBlocker(ui->treeWidget);
        populateAllCategories();
    }
    filterTree(ui->lineEditSearch->text());
    if (fi.path() == applicationsDir) {
        findReloadItem(systemApplicationsPath() + "/" + base_name);
    }
}

// copy the current item to a new, independently-editable .desktop file
void MainWindow::pushDuplicateApp_clicked()
{
    if (current_item == nullptr) {
        return;
    }
    const auto base_name = QFileInfo(current_item->text(1)).fileName();
    const auto applicationsDir = localApplicationsPath();

    const auto newName = ui->lineEditName->text() + tr(" (copy)");
    const auto newContent = DesktopUtils::renamedCopy(ui->advancedEditor->toPlainText(), newName);

    if (!QFileInfo::exists(applicationsDir) && !QDir().mkpath(applicationsDir)) {
        QMessageBox::critical(this, tr("Error"), tr("Could not create the applications directory"));
        return;
    }
    const auto out_name = createLocalLauncher(base_name, newContent.toUtf8(), true);
    if (out_name.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Could not save the file"));
        return;
    }

    if (!all_local_desktop_files.contains(out_name)) {
        all_local_desktop_files << out_name;
        updateLocalBasenamesCache();
    }

    QStringList categories;
    for (int i = 0; i < ui->listWidgetEditCategories->count(); ++i) {
        categories << ui->listWidgetEditCategories->item(i)->text();
    }
    insertAppIntoCategories(out_name, categories);

    restartPanel();
    filterTree(ui->lineEditSearch->text());
    findReloadItem(out_name);
}

// find and reload item
void MainWindow::findReloadItem(const QString &filePath)
{
    QTreeWidgetItemIterator it(ui->treeWidget);
    while (auto *item = *it) {
        if (item->text(1) == filePath) {
            item->parent()->setExpanded(true);
            ui->treeWidget->setCurrentItem(item);
            break;
        }
        ++it;
    }
    filterTree(ui->lineEditSearch->text());
}

namespace
{
struct IconKey {
    QString name;
    QSize size;

    bool operator==(const IconKey &other) const
    {
        return name == other.name && size == other.size;
    }
};

inline size_t qHash(const IconKey &key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.name, key.size.width(), key.size.height());
}
} // namespace

QPixmap MainWindow::findIcon(const QString &iconName, const QSize &size)
{
    static QHash<IconKey, QIcon> iconCache;
    static const QRegularExpression re(QStringLiteral("\\.(png|svg|xpm)$"));
    static const QStringList extensions {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm")};

    const auto makePixmap = [size](const QIcon &icon) {
        return icon.isNull() ? QPixmap() : icon.pixmap(size.isValid() ? size : QSize());
    };

    if (iconName.isEmpty()) {
        return {};
    }

    const IconKey key {.name = iconName, .size = size};
    if (iconCache.contains(key)) {
        return makePixmap(iconCache.value(key));
    }

    // Absolute path handling
    if (QFileInfo::exists(iconName) && QFileInfo(iconName).isAbsolute()) {
        QIcon icon(iconName);
        iconCache.insert(key, icon);
        return makePixmap(icon);
    }

    QString nameNoExt = iconName;
    nameNoExt.remove(re);

    // Themed icon
    QIcon themedIcon = QIcon::fromTheme(nameNoExt);
    if (!themedIcon.isNull()) {
        iconCache.insert(key, themedIcon);
        return makePixmap(themedIcon);
    }

    // Fallback: search common icon-theme locations even when the current theme doesn't provide the icon
    // (e.g. icons exist under /usr/share/icons/<theme>/<size>/actions but the running theme is different).
    const auto findFromAnyTheme = [&](const QString &requestedName) -> QIcon {
        QString baseName = QFileInfo(requestedName).fileName();
        baseName.remove(re);
        static const QList<int> commonSizes {16, 22, 24, 32, 48, 64, 128, 256};
        QStringList sizeDirs;
        if (size.isValid() && size.width() > 0 && size.height() > 0 && size.width() == size.height()) {
            const int px = size.width();
            sizeDirs << QStringLiteral("%1x%1").arg(px);
        }
        for (int px : commonSizes) {
            const QString dir = QStringLiteral("%1x%1").arg(px);
            if (!sizeDirs.contains(dir)) {
                sizeDirs << dir;
            }
        }

        QStringList relDirs;
        relDirs << QStringLiteral("scalable/actions") << QStringLiteral("scalable/apps");
        for (const QString &sd : sizeDirs) {
            relDirs << (sd + QStringLiteral("/actions")) << (sd + QStringLiteral("/apps"));
        }
        relDirs << QStringLiteral("48x48/legacy") << QStringLiteral("32x32/legacy") << QStringLiteral("16x16/legacy");

        const QStringList themeRoots = QIcon::themeSearchPaths();
        const auto tryFile = [&](const QString &filePath) -> QIcon {
            if (!QFile::exists(filePath)) {
                return {};
            }
            QIcon icon(filePath);
            return icon.isNull() ? QIcon() : icon;
        };

        // Try /usr/share/pixmaps first (common for app icons)
        for (const QString &ext : extensions) {
            if (QIcon icon = tryFile(QDir(SystemPixmapsPath).filePath(baseName + ext));
                !icon.isNull()) {
                return icon;
            }
        }

        // Then scan installed themes under the standard icon roots.
        for (const QString &root : themeRoots) {
            const QDir rootDir(root);
            if (!rootDir.exists()) {
                continue;
            }

            const QStringList themes = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &theme : themes) {
                const QString themeBase = rootDir.filePath(theme);
                for (const QString &relDir : relDirs) {
                    const QString dirPath = QDir(themeBase).filePath(relDir);
                    for (const QString &ext : extensions) {
                        if (QIcon icon = tryFile(QDir(dirPath).filePath(baseName + ext)); !icon.isNull()) {
                            return icon;
                        }
                    }
                }
            }
        }
        return {};
    };

    QIcon icon = findFromAnyTheme(iconName);

    if (!icon.isNull()) {
        iconCache.insert(key, icon);
        return makePixmap(icon);
    }

    return {};
}
