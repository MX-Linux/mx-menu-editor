/**********************************************************************
 *  test_desktoputils.cpp
 **********************************************************************
 * Copyright (C) 2026-2026 MX Authors
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
#include <QRegularExpression>
#include <QSet>
#include <QTest>

#include "addappdialog.h"
#include "desktoputils.h"

class TestDesktopUtils : public QObject
{
    Q_OBJECT

private slots:
    // DesktopUtils::setEntryValue - desktop key mutation
    void setEntryValue_replacesExistingKey();
    void setEntryValue_appendsWhenKeyMissing();
    void editMainGroupOnly();
    void localizedCopyName();

    // DesktopUtils::categoryMatchPattern - exact category matching
    void categoryMatchPattern_data();
    void categoryMatchPattern();

    // DesktopUtils::parseCommandExecutable - executable parsing
    void parseCommandExecutable_data();
    void parseCommandExecutable();

    // DesktopUtils::checkExecutableExists - executable parsing
    void checkExecutableExists();

    // DesktopUtils::sanitizeFileName - filename sanitization
    void sanitizeFileName_data();
    void sanitizeFileName();

    // DesktopUtils::uniqueCopyFileName - collision-avoiding "copy" basenames
    void uniqueCopyFileName();

    // DesktopUtils::containsInvalidDesktopChars / DesktopUtils::pickLocalizedNameForKeys
    void containsInvalidDesktopChars_data();
    void containsInvalidDesktopChars();
    void pickLocalizedNameForKeys();

    // AddAppDialog::validateApplicationName/validateComment/validateIconPath - gate
    // failed-save behavior: pushSave_clicked() refuses to write a file when these
    // reject the input, so testing them directly covers what would otherwise require
    // driving the full GUI save flow.
    void validateApplicationName();
    void validateComment();
    void validateIconPath();
};

void TestDesktopUtils::setEntryValue_replacesExistingKey()
{
    const QString text = QStringLiteral("[Desktop Entry]\nName=Old\nExec=foo\n");
    const QString result = DesktopUtils::setEntryValue(text, QStringLiteral("Name"), QStringLiteral("New"));
    QCOMPARE(result, QStringLiteral("[Desktop Entry]\nName=New\nExec=foo\n"));
}

void TestDesktopUtils::setEntryValue_appendsWhenKeyMissing()
{
    const QString text = QStringLiteral("[Desktop Entry]\nExec=foo");
    const QString result = DesktopUtils::setEntryValue(text, QStringLiteral("Name"), QStringLiteral("Fresh"));
    QCOMPARE(result, QStringLiteral("[Desktop Entry]\nExec=foo\nName=Fresh\n"));
}

void TestDesktopUtils::editMainGroupOnly()
{
    const QString actions = "[Desktop Action New]\nName=New\nIcon=action\n";
    const QString entry = "[Desktop Entry]\nName=App\n";
    QCOMPARE(DesktopUtils::setEntryValue(entry + actions, "Icon", "main"), entry + "Icon=main\n" + actions);
    QCOMPARE(DesktopUtils::setEntryValue(entry + actions, "Icon", QString()), entry + actions);
    QCOMPARE(DesktopUtils::setEntryValue(entry + actions, "Name", QString()), "[Desktop Entry]\n" + actions);
    QCOMPARE(DesktopUtils::entryValue(entry + actions, "Name"), "App");
    QVERIFY(DesktopUtils::entryValue(entry + actions, "Icon").isEmpty());
    QCOMPARE(DesktopUtils::setEntryValue(actions, "Name", "Changed"), actions);
}

void TestDesktopUtils::localizedCopyName()
{
    const QString action = "[Desktop Action New]\nName[fr]=Nouveau\n";
    const QString launcher = "[Desktop Entry]\nName[fr]=Éditeur\nName=Editor\nName[de]=Editor\nComment[fr]=Texte\n";
    QCOMPARE(DesktopUtils::renamedCopy(launcher + action, "Editor (copy)"),
             "[Desktop Entry]\nName=Editor (copy)\nComment[fr]=Texte\n" + action);
}

void TestDesktopUtils::categoryMatchPattern_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("category");
    QTest::addColumn<bool>("expectMatch");

    QTest::newRow("substring-not-token") << "Categories=ArcadeGame;Utility;" << "Game" << false;
    QTest::newRow("first-token") << "Categories=Game;Utility;" << "Game" << true;
    QTest::newRow("last-token-with-trailing-semicolon") << "Categories=Utility;Game;" << "Game" << true;
    QTest::newRow("last-token-no-trailing-semicolon") << "Categories=Utility;Game" << "Game" << true;
    QTest::newRow("prefix-substring") << "Categories=GameUtility;" << "Game" << false;
    QTest::newRow("embedded-substring") << "Categories=Utility;GameUtility;" << "Game" << false;
    QTest::newRow("middle-token") << "Categories=Office;Game;Network;" << "Game" << true;
    QTest::newRow("other-substring-of-self") << "Categories=AudioVideo;Audio;" << "Video" << false;
    QTest::newRow("no-such-category") << "Categories=AudioVideo;" << "Video" << false;
    QTest::newRow("exact-whole-value") << "Categories=AudioVideo;" << "AudioVideo" << true;
}

void TestDesktopUtils::categoryMatchPattern()
{
    QFETCH(QString, line);
    QFETCH(QString, category);
    QFETCH(bool, expectMatch);

    const QRegularExpression re(DesktopUtils::categoryMatchPattern(category));
    QCOMPARE(re.match(line).hasMatch(), expectMatch);
}

void TestDesktopUtils::parseCommandExecutable_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<QString>("expected");

    QTest::newRow("simple") << "firefox" << "firefox";
    QTest::newRow("with-args") << "firefox --new-window %u" << "firefox";
    QTest::newRow("double-quoted-path-with-space") << "\"/opt/My App/run.sh\" --flag" << "/opt/My App/run.sh";
    QTest::newRow("single-quoted-path-with-space") << "'/opt/My App/run.sh'" << "/opt/My App/run.sh";
    QTest::newRow("escaped-space") << "/opt/My\\ App/run.sh" << "/opt/My App/run.sh";
    QTest::newRow("leading-spaces") << "   ls -la" << "ls";
    QTest::newRow("empty") << "" << "";
    // Non-empty command, but empty parsed executable: AddAppDialog::validateCommand()
    // must catch this itself (confirmExecutableExists() treats an empty executable as
    // "nothing to check" and would otherwise silently accept it).
    QTest::newRow("empty-single-quotes") << "''" << "";
    QTest::newRow("empty-double-quotes") << "\"\"" << "";
}

void TestDesktopUtils::parseCommandExecutable()
{
    QFETCH(QString, command);
    QFETCH(QString, expected);
    QCOMPARE(DesktopUtils::parseCommandExecutable(command), expected);
}

void TestDesktopUtils::checkExecutableExists()
{
    // /bin/sh is required by POSIX and present on every Debian/MX Linux system.
    QVERIFY(DesktopUtils::checkExecutableExists(QStringLiteral("/bin/sh")));
    QVERIFY(!DesktopUtils::checkExecutableExists(QStringLiteral("/no/such/executable-xyz")));
    QVERIFY(!DesktopUtils::checkExecutableExists(QStringLiteral("definitely-not-a-real-command-xyz")));
    QVERIFY(!DesktopUtils::checkExecutableExists(QString()));
}

void TestDesktopUtils::sanitizeFileName_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");

    QTest::newRow("simple") << "My App" << "My-App";
    QTest::newRow("invalid-chars") << "a/b\\c:d*e?f\"g<h>i|j" << "a-b-c-d-e-f-g-h-i-j";
    QTest::newRow("leading-dots") << "...hidden" << "hidden";
    QTest::newRow("empty-falls-back") << "" << "application";
    QTest::newRow("only-invalid-falls-back-no") << "---" << "---";
}

void TestDesktopUtils::sanitizeFileName()
{
    QFETCH(QString, name);
    QFETCH(QString, expected);
    QCOMPARE(DesktopUtils::sanitizeFileName(name), expected);
}

void TestDesktopUtils::uniqueCopyFileName()
{
    const auto noneExist = [](const QString &) { return false; };
    QCOMPARE(DesktopUtils::uniqueCopyFileName(QStringLiteral("foo.desktop"), noneExist),
             QStringLiteral("foo-copy.desktop"));
    QCOMPARE(DesktopUtils::uniqueCopyFileName(QStringLiteral("foo"), noneExist), QStringLiteral("foo-copy"));

    QSet<QString> taken {QStringLiteral("foo-copy.desktop"), QStringLiteral("foo-copy-2.desktop")};
    const auto existsInSet = [&taken](const QString &candidate) { return taken.contains(candidate); };
    QCOMPARE(DesktopUtils::uniqueCopyFileName(QStringLiteral("foo.desktop"), existsInSet),
             QStringLiteral("foo-copy-3.desktop"));
}

void TestDesktopUtils::containsInvalidDesktopChars_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("expectInvalid");

    QTest::newRow("plain-text") << "My Application" << false;
    QTest::newRow("tab-allowed") << QString("a\tb") << false;
    QTest::newRow("newline") << QString("a\nb") << true;
    QTest::newRow("carriage-return") << QString("a\rb") << true;
    QTest::newRow("control-char") << QString(QChar(0x01)) << true;
    QTest::newRow("empty") << "" << false;
}

void TestDesktopUtils::containsInvalidDesktopChars()
{
    QFETCH(QString, text);
    QFETCH(bool, expectInvalid);
    QCOMPARE(DesktopUtils::containsInvalidDesktopChars(text), expectInvalid);
}

void TestDesktopUtils::pickLocalizedNameForKeys()
{
    QHash<QString, QString> localizedNames;
    localizedNames.insert(QStringLiteral("fr"), QStringLiteral("Éditeur"));
    localizedNames.insert(QStringLiteral("de"), QStringLiteral("Editor"));

    // Exact locale match wins.
    QCOMPARE(DesktopUtils::pickLocalizedNameForKeys(QStringLiteral("Editor (en)"), localizedNames,
                                                     {QStringLiteral("fr_FR"), QStringLiteral("fr")}),
             QStringLiteral("Éditeur"));

    // No matching locale key -> falls back to the plain default name.
    QCOMPARE(DesktopUtils::pickLocalizedNameForKeys(QStringLiteral("Editor (en)"), localizedNames,
                                                     {QStringLiteral("es_ES"), QStringLiteral("es")}),
             QStringLiteral("Editor (en)"));

    // No default name and no matching locale -> falls back to any localized name.
    const QString fallback = DesktopUtils::pickLocalizedNameForKeys(QString(), localizedNames,
                                                                      {QStringLiteral("es_ES")});
    QVERIFY(fallback == QStringLiteral("Éditeur") || fallback == QStringLiteral("Editor"));

    // Nothing at all -> empty.
    QCOMPARE(DesktopUtils::pickLocalizedNameForKeys(QString(), {}, {QStringLiteral("fr")}), QString());
}

void TestDesktopUtils::validateApplicationName()
{
    QString error;
    QVERIFY(AddAppDialog::validateApplicationName(QStringLiteral("My App"), error));
    QVERIFY(error.isEmpty());

    QVERIFY(!AddAppDialog::validateApplicationName(QString(), error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!AddAppDialog::validateApplicationName(QStringLiteral("Bad\nName"), error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!AddAppDialog::validateApplicationName(QString(300, QLatin1Char('a')), error));
    QVERIFY(!error.isEmpty());
}

void TestDesktopUtils::validateComment()
{
    QString error;
    QVERIFY(AddAppDialog::validateComment(QStringLiteral("A nice app"), error));
    QVERIFY(AddAppDialog::validateComment(QString(), error)); // empty comment is allowed

    QVERIFY(!AddAppDialog::validateComment(QStringLiteral("Bad\nComment"), error));
    QVERIFY(!error.isEmpty());
}

void TestDesktopUtils::validateIconPath()
{
    QString error;
    QVERIFY(AddAppDialog::validateIconPath(QString(), error)); // empty icon path is allowed
    QVERIFY(AddAppDialog::validateIconPath(QStringLiteral("/usr/share/icons/foo.svg"), error));

    QVERIFY(!AddAppDialog::validateIconPath(QStringLiteral("/bad\npath.svg"), error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(TestDesktopUtils)
#include "test_desktoputils.moc"
