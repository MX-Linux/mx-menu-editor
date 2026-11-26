/**********************************************************************
 *  desktoputils.cpp
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
#include "desktoputils.h"

#include <QFile>
#include <QLocale>
#include <QStandardPaths>

#include <algorithm>

namespace
{
struct EntryField {
    qsizetype row;
    bool exists;
};

EntryField findEntryField(const QStringList &lines, const QString &key)
{
    const QString prefix = key + '=';
    bool inEntry = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        const auto trimmed = QStringView(line).trimmed();
        if (trimmed.startsWith('[')) {
            if (inEntry) {
                return {i, false};
            }
            inEntry = trimmed == QLatin1String("[Desktop Entry]");
        } else if (inEntry && line.startsWith(prefix)) {
            return {i, true};
        }
    }
    return {inEntry ? lines.size() - (lines.last().isEmpty() ? 1 : 0) : -1, false};
}
} // namespace

QString DesktopUtils::entryValue(const QString &text, const QString &key)
{
    const auto lines = text.split('\n');
    const auto field = findEntryField(lines, key);
    return field.exists ? lines.at(field.row).mid(key.size() + 1).trimmed() : QString();
}

QString DesktopUtils::setEntryValue(const QString &text, const QString &key, const QString &value)
{
    auto lines = text.split('\n');
    const auto field = findEntryField(lines, key);
    if (field.row < 0) {
        return text;
    }
    if (value.isNull()) {
        if (field.exists) {
            lines.removeAt(field.row);
        }
    } else {
        QString line = key + '=' + value;
        const auto &reference = lines.at(field.exists ? field.row : field.row - 1);
        if (reference.endsWith('\r')) {
            line += '\r';
        }
        if (field.exists) {
            lines[field.row] = line;
        } else {
            lines.insert(field.row, line);
            if (field.row == lines.size() - 1) {
                lines.append(QString());
            }
        }
    }
    return lines.join('\n');
}

QString DesktopUtils::renamedCopy(const QString &text, const QString &name)
{
    const auto lines = setEntryValue(text, QStringLiteral("Name"), name).split('\n');
    QStringList copy;
    copy.reserve(lines.size());
    bool inEntry = false;
    for (const auto &line : lines) {
        const auto trimmed = QStringView(line).trimmed();
        if (trimmed.startsWith('[')) {
            inEntry = trimmed == QLatin1String("[Desktop Entry]");
        }
        if (inEntry && line.startsWith(QLatin1String("Name[")) && line.contains(QLatin1String("]="))) {
            continue;
        }
        copy.append(line);
    }
    return copy.join('\n');
}

QString DesktopUtils::pickLocalizedNameForKeys(const QString &defaultName,
                                                const QHash<QString, QString> &localizedNames,
                                                const QStringList &localeKeys)
{
    for (const auto &key : localeKeys) {
        if (localizedNames.contains(key)) {
            return localizedNames.value(key);
        }
    }
    if (!defaultName.isEmpty()) {
        return defaultName;
    }
    if (!localizedNames.isEmpty()) {
        return localizedNames.constBegin().value();
    }
    return {};
}

QStringList DesktopUtils::systemLocaleNameKeys()
{
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    QStringList result;
    result.reserve(uiLanguages.size() * 2);
    for (const auto &lang : uiLanguages) {
        const auto normalized = QString(lang).replace(QLatin1Char('-'), QLatin1Char('_'));
        result << normalized;
        const int sepIndex = normalized.indexOf(QLatin1Char('_'));
        if (sepIndex > 0) {
            result << normalized.left(sepIndex);
        }
    }
    return result;
}

QString DesktopUtils::pickLocalizedName(const QString &defaultName, const QHash<QString, QString> &localizedNames)
{
    // Cached since the system locale doesn't change mid-run and this is consulted
    // once per desktop file.
    static const QStringList keys = systemLocaleNameKeys();
    return pickLocalizedNameForKeys(defaultName, localizedNames, keys);
}

QString DesktopUtils::categoryMatchPattern(const QString &category)
{
    // Match the category as a whole semicolon-delimited token, not a substring of
    // another category.
    return QStringLiteral("Categories=(?:[^;]*;)*") + QRegularExpression::escape(category) + QStringLiteral("(?:;|$)");
}

QString DesktopUtils::encodeExecPath(const QString &path)
{
    // Quote the executable as one Exec argument, then escape it as a desktop-entry string.
    static const QString reserved = QStringLiteral("\"'\\><~|&;$*?#()`");
    const bool quote = std::ranges::any_of(path, [](QChar ch) {
        return ch.isSpace() || reserved.contains(ch);
    });
    QString token = path;
    if (quote) {
        token.replace("\\", "\\\\").replace("\"", "\\\"").replace("`", "\\`").replace("$", "\\$");
        token = '"' + token + '"';
    }
    return token.replace("%", "%%").replace("\\", "\\\\").replace('\t', "\\t").replace('\n', "\\n").replace('\r', "\\r");
}

QString DesktopUtils::parseCommandExecutable(const QString &command)
{
    // Decode desktop-entry string escapes before interpreting Exec argument quoting.
    QString result;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool escaped = false;

    for (int i = 0; i < command.length(); ++i) {
        QChar ch = command.at(i);
        if (ch == '\\' && i + 1 < command.size()) {
            static const QString codes = QStringLiteral("\\stnr");
            static const QString decoded = QStringLiteral("\\ \t\n\r");
            const auto index = codes.indexOf(command.at(i + 1));
            if (index >= 0) {
                ch = decoded.at(index);
                ++i;
            }
        }

        if (escaped) {
            result.append(ch);
            escaped = false;
            continue;
        }

        if (ch == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }

        if (ch == QLatin1Char('\'') && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
            continue;
        }

        if (ch == QLatin1Char('"') && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
            continue;
        }

        if (ch.isSpace() && !inSingleQuote && !inDoubleQuote) {
            // Found first complete token
            if (!result.isEmpty()) {
                break;
            }
            continue; // Skip leading spaces
        }

        result.append(ch);
    }

    return result.replace("%%", "%");
}

bool DesktopUtils::checkExecutableExists(const QString &executable)
{
    if (executable.isEmpty()) {
        return false;
    }

    // Remove surrounding quotes if present (e.g., "/path/to app" -> /path/to app)
    QString cleanExecutable = executable;
    if ((cleanExecutable.startsWith(QLatin1Char('"')) && cleanExecutable.endsWith(QLatin1Char('"'))) ||
        (cleanExecutable.startsWith(QLatin1Char('\'')) && cleanExecutable.endsWith(QLatin1Char('\'')))) {
        cleanExecutable = cleanExecutable.mid(1, cleanExecutable.length() - 2);
    }

    // Check if the executable exists
    if (cleanExecutable.startsWith(QLatin1Char('/'))) {
        // Absolute path - check directly
        return QFile::exists(cleanExecutable);
    }
    // Relative path or command name - check in PATH
    return !QStandardPaths::findExecutable(cleanExecutable).isEmpty();
}

bool DesktopUtils::containsInvalidDesktopChars(const QString &text)
{
    return std::ranges::any_of(text, [](QChar ch) {
        return ch == QLatin1Char('\n') || ch == QLatin1Char('\r')
            || (ch.unicode() < 32 && ch != QLatin1Char('\t'));
    });
}

QString DesktopUtils::sanitizeFileName(const QString &name)
{
    QString sanitized = name;
    // Remove or replace invalid filename characters
    static const QString invalidChars = QStringLiteral("/\\:*?\"<>|");
    for (const QChar &ch : invalidChars) {
        sanitized.replace(ch, QLatin1String("-"));
    }
    // Replace spaces with dashes
    sanitized.replace(QLatin1Char(' '), QLatin1Char('-'));
    // Remove leading/trailing dots and spaces
    sanitized = sanitized.trimmed();
    while (sanitized.startsWith(QLatin1Char('.'))) {
        sanitized.remove(0, 1);
    }
    // Ensure it's not empty
    if (sanitized.isEmpty()) {
        sanitized = QStringLiteral("application");
    }
    return sanitized;
}

QString DesktopUtils::uniqueCopyFileName(const QString &baseName, const std::function<bool(const QString &)> &exists)
{
    const int dotIndex = baseName.lastIndexOf(QLatin1Char('.'));
    const QString stem = dotIndex > 0 ? baseName.left(dotIndex) : baseName;
    const QString suffix = dotIndex > 0 ? baseName.mid(dotIndex) : QString();

    QString candidate = stem + QStringLiteral("-copy") + suffix;
    for (int n = 2; exists(candidate); ++n) {
        candidate = stem + QStringLiteral("-copy-") + QString::number(n) + suffix;
    }
    return candidate;
}
