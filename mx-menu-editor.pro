# **********************************************************************
# * Copyright (C) 2015 MX Authors
# *
# * Authors: Adrian
# *          MX & MEPIS Community <http://forum.mepiscommunity.org>
# *
# * This file is part of MX Menu Editor.
# *
# * MX Menu Editor is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * MX Menu Editor is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with MX Menu Editor.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

QT       += core gui widgets

TARGET = mx-menu-editor
TEMPLATE = app


SOURCES += main.cpp\
        mxmenueditor.cpp \
    addappdialog.cpp

HEADERS  += mxmenueditor.h \
    addappdialog.h

FORMS    += mxmenueditor.ui \
    addappdialog.ui

TRANSLATIONS += translations/mx-menu-editor_ca.ts \
                translations/mx-menu-editor_de.ts \
                translations/mx-menu-editor_el.ts \
                translations/mx-menu-editor_es.ts \
                translations/mx-menu-editor_fr.ts \
                translations/mx-menu-editor_it.ts \
                translations/mx-menu-editor_ja.ts \
                translations/mx-menu-editor_nl.ts \
                translations/mx-menu-editor_pl.ts \
                translations/mx-menu-editor_ro.ts \
                translations/mx-menu-editor_ru.ts \
                translations/mx-menu-editor_sv.ts


