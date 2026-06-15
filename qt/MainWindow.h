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
#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <memory>

#include <QMainWindow>
#include <QString>

#include "Document.h"


class QAction;
class QLabel;
class QScrollArea;


// Minimal Qt PDF viewer: open, render, page navigation, zoom. First milestone
// of the desktop port -- proves the core (poppler-cpp) + Qt rendering stack.
class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = NULL);

	void OpenPath(const QString& path);

private slots:
	void _OpenFile();
	void _NextPage();
	void _PreviousPage();
	void _ZoomIn();
	void _ZoomOut();

private:
	void _BuildActions();
	void _RenderCurrent();
	void _UpdateState();

	std::unique_ptr<nimblepdf::Document> fDocument;
	int fPage = 0;
	double fZoom = 1.0;  // 1.0 == 100% (72 dpi base)

	QScrollArea* fScrollArea = NULL;
	QLabel* fPageView = NULL;

	QAction* fPreviousAction = NULL;
	QAction* fNextAction = NULL;
	QAction* fZoomInAction = NULL;
	QAction* fZoomOutAction = NULL;
};


#endif  // MAIN_WINDOW_H
