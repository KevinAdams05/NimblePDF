/*
 * NimblePDF: A native PDF reader.
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <QApplication>

#include "MainWindow.h"


int
main(int argc, char* argv[])
{
	QApplication application(argc, argv);
	QApplication::setApplicationName("NimblePDF");
	QApplication::setApplicationVersion("2.2.0");

	MainWindow window;
	window.show();

	// Open a document passed on the command line, if any.
	if (argc > 1)
		window.OpenPath(QString::fromLocal8Bit(argv[1]));

	return application.exec();
}
