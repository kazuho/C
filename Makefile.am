# $Id: Makefile.am,v 1.4 2006/01/23 06:30:54 labskazuho Exp $
#
# C - a pseudo-interpreter of the C programming language
# http://labs.cybozu.co.jp/blog/kazuhoatwork/
#
# Copyright (C) 2006 Cybozu Labs, Inc.
#
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2 of the
# License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA 02110-1301, USA.
#
# 

bin_PROGRAMS	= C
man_MANS	= C.1
C_SOURCES 	= C.c
AM_CFLAGS	= -Wall
CLEANFILES	= $(man_MANS)
EXTRA_DIST	= $(man_MANS)


C.1:	$(C_SOURCES)
	-$(HELP2MAN) -N --output $@ --name='a pseudo interpreter of the C programming language' ./C
