###############################################################################
##                                                                           ##
##    This file is part of qt-pods-core.                                     ##
##    Copyright (C) 2014-2015 Jacob Dawid <jacob@omg-it.works>               ##
##                                                                           ##
##    qt-pods-core is free software: you can redistribute it and#or modify   ##
##    it under the terms of the GNU General Public License as published by   ##
##    the Free Software Foundation, either version 3 of the License, or      ##
##    (at your option) any later version.                                    ##
##                                                                           ##
##    qt-pods-core is distributed in the hope that it will be useful,        ##
##    but WITHOUT ANY WARRANTY; without even the implied warranty of         ##
##    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          ##
##    GNU General Public License for more details.                           ##
##                                                                           ##
##    You should have received a copy of the GNU General Public License      ##
##    along with qt-pods-core. If not, see <http:##www.gnu.org#licenses#>.   ##
##                                                                           ##
###############################################################################

QT += core widgets network

TEMPLATE = lib
CONFIG += staticlib

SOURCES += \
    podmanager.cpp

HEADERS += \
    pod.h \
    podmanager.h

