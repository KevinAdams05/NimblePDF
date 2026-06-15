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
#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QScrollArea>
#include <QStatusBar>
#include <QToolBar>


namespace {
const double kBaseDpi = 72.0;  // 1 PDF point == 1 px at 100%
const double kZoomStep = 1.25;
const double kMinZoom = 0.1;
const double kMaxZoom = 8.0;
}  // namespace


MainWindow::MainWindow(QWidget* parent)
	:
	QMainWindow(parent)
{
	setWindowTitle("NimblePDF");
	resize(900, 1000);

	fPageView = new QLabel(this);
	fPageView->setAlignment(Qt::AlignCenter);
	fPageView->setBackgroundRole(QPalette::Dark);

	fScrollArea = new QScrollArea(this);
	fScrollArea->setWidget(fPageView);
	fScrollArea->setAlignment(Qt::AlignCenter);
	fScrollArea->setWidgetResizable(false);
	setCentralWidget(fScrollArea);

	_BuildActions();
	_UpdateState();
}


void
MainWindow::OpenPath(const QString& path)
{
	std::unique_ptr<nimblepdf::Document> document =
		nimblepdf::Document::Open(path.toStdString());
	if (!document || document->PageCount() == 0) {
		QMessageBox::warning(this, "NimblePDF",
			QString("Could not open \"%1\".").arg(path));
		return;
	}

	fDocument = std::move(document);
	fPage = 0;
	setWindowTitle(QString("%1 - NimblePDF").arg(QFileInfo(path).fileName()));
	_RenderCurrent();
	_UpdateState();
}


void
MainWindow::_BuildActions()
{
	QToolBar* toolBar = addToolBar("Main");
	toolBar->setMovable(false);

	QAction* openAction = toolBar->addAction("Open" + QString::fromUtf8("…"));
	connect(openAction, &QAction::triggered, this, &MainWindow::_OpenFile);
	toolBar->addSeparator();

	fPreviousAction = toolBar->addAction("Previous");
	connect(fPreviousAction, &QAction::triggered, this, &MainWindow::_PreviousPage);
	fNextAction = toolBar->addAction("Next");
	connect(fNextAction, &QAction::triggered, this, &MainWindow::_NextPage);
	toolBar->addSeparator();

	fZoomOutAction = toolBar->addAction("Zoom -");
	connect(fZoomOutAction, &QAction::triggered, this, &MainWindow::_ZoomOut);
	fZoomInAction = toolBar->addAction("Zoom +");
	connect(fZoomInAction, &QAction::triggered, this, &MainWindow::_ZoomIn);

	statusBar();
}


void
MainWindow::_OpenFile()
{
	const QString path = QFileDialog::getOpenFileName(
		this, "Open PDF", QString(), "PDF documents (*.pdf);;All files (*)");
	if (!path.isEmpty())
		OpenPath(path);
}


void
MainWindow::_RenderCurrent()
{
	if (!fDocument || fDocument->PageCount() == 0) {
		fPageView->clear();
		fPageView->setText("Open a PDF to begin.");
		return;
	}

	nimblepdf::RenderedPage page = fDocument->Render(fPage, kBaseDpi * fZoom);
	if (!page.IsValid()) {
		fPageView->setText("(failed to render this page)");
		return;
	}

	// Wrap the core's ARGB buffer; copy() detaches from the temporary vector.
	QImage image(page.argb.data(), page.width, page.height, page.stride,
		QImage::Format_ARGB32);
	fPageView->setPixmap(QPixmap::fromImage(image.copy()));
	fPageView->resize(page.width, page.height);
}


void
MainWindow::_NextPage()
{
	if (fDocument && fPage + 1 < fDocument->PageCount()) {
		++fPage;
		_RenderCurrent();
		_UpdateState();
	}
}


void
MainWindow::_PreviousPage()
{
	if (fDocument && fPage > 0) {
		--fPage;
		_RenderCurrent();
		_UpdateState();
	}
}


void
MainWindow::_ZoomIn()
{
	fZoom = qMin(fZoom * kZoomStep, kMaxZoom);
	_RenderCurrent();
	_UpdateState();
}


void
MainWindow::_ZoomOut()
{
	fZoom = qMax(fZoom / kZoomStep, kMinZoom);
	_RenderCurrent();
	_UpdateState();
}


void
MainWindow::_UpdateState()
{
	const bool hasDocument = fDocument && fDocument->PageCount() > 0;
	const int count = hasDocument ? fDocument->PageCount() : 0;

	fPreviousAction->setEnabled(hasDocument && fPage > 0);
	fNextAction->setEnabled(hasDocument && fPage + 1 < count);
	fZoomInAction->setEnabled(hasDocument);
	fZoomOutAction->setEnabled(hasDocument);

	if (hasDocument) {
		statusBar()->showMessage(QString("Page %1 of %2     %3%")
			.arg(fPage + 1).arg(count).arg(int(fZoom * 100 + 0.5)));
	} else {
		statusBar()->showMessage("No document");
	}
}
