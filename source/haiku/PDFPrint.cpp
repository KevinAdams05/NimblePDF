/*  
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013 waddlesplash.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

// BeOS
#include <PrintJob.h>
// xpdf
#include <Object.h>
#include <Gfx.h>
// BePDF
#include "Logging.h"
#include "BepdfApplication.h"
#include "PDFView.h"
#include "PrintingProgressWindow.h"
#include "BePDF.h"
#include "AnnotationRenderer.h"

///////////////////////////////////////////////////////////////////////////
/*
	based up Be sample code in "Be Newsletter Volume 2, Issue 18 -- May 6, 1998"
*/

status_t PDFView::PageSetup()
{
	status_t result = B_ERROR;

	BPrintJob printJob(this->fTitle->String());


	if (fPrintSettings != NULL) {
		/* page setup has already been run */
		printJob.SetSettings(new BMessage(*fPrintSettings));
	}

	result = printJob.ConfigPage();

	if (result == B_NO_ERROR) {
		delete fPrintSettings;
		fPrintSettings = printJob.Settings();
	}

	return result;
}


///////////////////////////////////////////////////////////////////////////
class PrintView : public BView {
public:
	PrintView(PDFView* view, PDFDoc* fDoc, PageRenderer* pageRenderer, BMessage* printSettings, const char* title, BRect rect);

	void SetPage(int32 page);
	void Draw(BRect updateRect);
	friend int32 printing_thread(void* data);

	void Print();

private:
	static void RedrawCallback(void* data, int left, int top, int right, int bottom, bool composited);
	void Redraw(int left, int top, int right, int bottom, bool composited);
	static bool AbortCheckCallback(void* data);

	PDFView* fView;
	PDFDoc* fDoc;
	bool fColorMode;
	PageRenderer* fPageRenderer;
	BeSplashOutputDev* fOutputDev;
	int fPageWidth;  // of print page
	int fPageHeight; // of print page
	int fSliceY;
	int fSliceHeight;
	int32 fCurrentPage;
	char* fTitle;
	int fZoom;
	double fScale;
	int fRotation;
	int16 fPrintSelection;
	int16 fPrintOrder;
	BMessage* fPrintSettings;
	BRect fRect;
	PrintingProgressWindow* fProgressWindow;
};

///////////////////////////////////////////////////////////////////////////
PrintView::PrintView(PDFView* view, PDFDoc* doc, PageRenderer* pageRenderer, BMessage* printSettings, const char* title, BRect rect)
    : BView(BRect(1000, 1000, 1000 + rect.Width(), 1000 + rect.Height()), "print_view", B_FOLLOW_NONE, B_WILL_DRAW)
{
	GlobalSettings* s = gApp->GetSettings();
	fView = view; // PDFView
	fDoc = doc;
	fPageRenderer = pageRenderer;
	fPrintSettings = printSettings;
	fTitle = (char*)title;
	fZoom = s->GetZoomPrinter();
	fRotation = (int)s->GetRotationPrinter();
	fRect = rect;
	fScale = s->GetDPI() / 72.0;
	fPrintSelection = s->GetPrintSelection();
	fPrintOrder = s->GetPrintOrder();

	SplashColor backgroundColor;
	backgroundColor[0] = 255;
	backgroundColor[1] = 255;
	backgroundColor[2] = 255;

	BeSplashOutputDev::ColorMode colorMode;
	fColorMode = s->GetPrintColorMode() == GlobalSettings::PRINT_COLOR_MODE;
	if (fColorMode) {
		colorMode = BeSplashOutputDev::kColorMode;
	} else {
		colorMode = BeSplashOutputDev::kGrayScaleMode;
	}

	fOutputDev = new BeSplashOutputDev(false, backgroundColor, false, RedrawCallback, this, colorMode);

	fOutputDev->startDoc(NULL);
	fOutputDev->startDoc(doc->getXRef());
}


///////////////////////////////////////////////////////////////////////////
bool PrintView::AbortCheckCallback(void* data)
{
	PrintView* printView = static_cast<PrintView*>(data);
	PrintingProgressWindow* progress = printView->fProgressWindow;
	return progress->Stopped() || progress->Aborted();
}

///////////////////////////////////////////////////////////////////////////
void PrintView::RedrawCallback(void* data, int left, int top, int right, int bottom, bool composited)
{
	PrintView* printView = (PrintView*)data;
	printView->Redraw(left, top, right, bottom, composited);
}

#define SLICE 1

void PrintView::Redraw(int left, int top, int right, int bottom, bool composited)
{
#if SLICE
	fOutputDev->redraw(0, 0, this, 0, fSliceY, fPageWidth, fSliceHeight, composited);
#else
	fOutputDev->redraw(0, 0, this, 0, 0, fPageWidth, fPageHeight, composited);
#endif
}

///////////////////////////////////////////////////////////////////////////
void PrintView::SetPage(int32 page)
{
	fCurrentPage = page;
}

///////////////////////////////////////////////////////////////////////////
void PrintView::Draw(BRect updateRect)
{
	if (Window()->Lock()) {
		// PDFLock lock;
		int32 zoomDPI = fZoom * 72 / 100;
		double dpi = fScale * zoomDPI;
#if SLICE
		// About 4 MB per slice (color mode requires 4 bytes per pixel;
		// monochrome mode requires 1 bytes per pixel).
		// Note because xpdf rasterizes into a bitmap and when the
		// rendered slice is drawn into the view, this it is converted to a
		// BBitmap actually twice as much memory is allocated temporary.
		const int64 maxSize = fColorMode ? 1024 * 1024 : 4 * 1024 * 1024;
		int64 slices = fPageWidth * (int64)fPageHeight / maxSize;
		if (slices <= 0) {
			slices = 1;
		}
		fSliceHeight = fPageHeight / slices;
		if (fSliceHeight <= 0) {
			fSliceHeight = 1;
		}
		for (fSliceY = 0; fSliceY < fPageHeight; fSliceY += fSliceHeight) {
			if (fSliceY + fSliceHeight > fPageHeight) {
				fSliceHeight = fPageHeight - fSliceY;
			}

			if (fSliceHeight <= 0) {
				break;
			}

			fDoc->displayPageSlice(fOutputDev,
			    fCurrentPage,
			    dpi,
			    dpi, // h/v DPI
			    fRotation,
			    false, // use media box
			    false, // crop
			    true,  // printing
			    0,      // slice X
			    fSliceY,
			    fPageWidth, // slice width
			    fSliceHeight,
			    AbortCheckCallback,
			    this);
		}
#else
		fDoc->displayPage(fOutputDev,
		    fCurrentPage,
		    dpi,
		    dpi, // h/v DPI
		    fRotation,
		    false, // use media box
		    false, // crop
		    true,  // printing
		    AbortCheckCallback,
		    this);
#endif

		Annotations* annots = fPageRenderer->GetAnnotationsForPage(fCurrentPage);
		{ // Don't remove this block; ar-dtor must be called after Iterate()!
			AnnotationRenderer ar(this, fOutputDev->getDefCTM(), fScale * zoomDPI, false);
			annots->Iterate(&ar);
		}
		Flush();
		Window()->Unlock();
	}
}


// printing thread
int32 printing_thread(void* data)
{
	PrintView* view = (PrintView*)data;
	view->Print();
	delete view;
	return 0;
}


void PrintView::Print()
{
	BPrintJob printJob(fTitle);
	printJob.SetSettings(new BMessage(*fPrintSettings));
	PrintingProgressWindow* progress = NULL;
	PrintingHiddenWindow* hiddenWin = NULL;

	if (printJob.ConfigJob() == B_OK) {
		int32 curPage = 1;
		int32 firstPage;
		int32 lastPage;
		int32 pagesInDocument;
		BRect pageRect = printJob.PrintableRect();

		pagesInDocument = fDoc->getNumPages();
		firstPage = printJob.FirstPage();
		lastPage = printJob.LastPage();
		if (firstPage < 1) {
			firstPage = 1;
		}
		if (lastPage > pagesInDocument) {
			lastPage = pagesInDocument;
		}

		if (fScale == 0) { // set DPI to maximum of printer resolution
			int32 xdpi, ydpi;
			printJob.GetResolution(&xdpi, &ydpi);
			// Max. 300 DPI otherwise we might run out of memory
			// and freeze BeOS!
			// TODO Change if/when Zeta/Haiku can handle more memory!
			if (xdpi > 300) {
				xdpi = 300;
			}
			if (ydpi > 300) {
				ydpi = 300;
			}
#ifdef MORE_DEBUG
			Trace(LOG_DEBUG, "print resolution= %d %d\n", xdpi, ydpi);
#endif
			if (xdpi > 0 && ydpi > 0) {
				if (xdpi > ydpi) {
					fScale = xdpi / 72;
				} else {
					fScale = ydpi / 72;
				}
			} else {
				fScale = 300 / 72; // default
			}
		}

		bool normalOrder = fPrintOrder == GlobalSettings::NORMAL_PRINT_ORDER;
		int16 incr = (fPrintSelection == GlobalSettings::PRINT_ALL_PAGES) ? 1 : 2;
		int32 pages = 0;

		switch (fPrintSelection) {
		case GlobalSettings::PRINT_ALL_PAGES:
			pages = lastPage - firstPage + 1;
			break;
		case GlobalSettings::PRINT_EVEN_PAGES:
			if (firstPage % 2 == 1)
				firstPage++;
			if (lastPage % 2 == 1)
				lastPage--;
			pages = (lastPage - firstPage) / 2 + 1;
			break;
		case GlobalSettings::PRINT_ODD_PAGES:
			if (firstPage % 2 == 0)
				firstPage++;
			if (lastPage % 2 == 0)
				lastPage--;
			pages = (lastPage - firstPage) / 2 + 1;
			break;
		}

		if (normalOrder) {
			curPage = firstPage;
		} else {
			curPage = lastPage;
			incr = -incr;
		}

		hiddenWin = new PrintingHiddenWindow(BRect(-100, -100, -10, -10));
		fProgressWindow = progress = new PrintingProgressWindow(fTitle, fRect, pages);
		if (hiddenWin->Lock()) {
			hiddenWin->AddChild(this);
			SetScale(1.0 / fScale);
			hiddenWin->Unlock();
		}

		int32 zoomDPI = fZoom * 72 / 100;
		zoomDPI = (int32)(zoomDPI * fScale);

		printJob.BeginJob();

		for (; ((normalOrder && (curPage <= lastPage)) || (!normalOrder && (curPage >= firstPage))) && !progress->Stopped();
		     curPage += incr) {
			SetPage(curPage);
			progress->SetPage(curPage);
			float width = RealSize(fDoc->getPageCropWidth(curPage), zoomDPI);
			float height = RealSize(fDoc->getPageCropHeight(curPage), zoomDPI);

			if ((fDoc->getPageRotate(curPage) == 90) || (fDoc->getPageRotate(curPage) == 270)) {
				float h = width;
				width = height;
				height = h;
			}

			if ((fRotation == 90) || (fRotation == 270)) {
				float h = width;
				width = height;
				height = h;
			}

			fPageWidth = (int)width;
			fPageHeight = (int)height;
			BRect curPageRect(0, 0, width, height);
			// center page
			BPoint origin((pageRect.Width() - width / fScale) / 2, (pageRect.Height() - height / fScale) / 2);

			printJob.DrawView(this, curPageRect, origin);

			printJob.SpoolPage();
			if (!printJob.CanContinue() || progress->Aborted()) {
				if (hiddenWin->Lock()) {
					hiddenWin->RemoveChild(this);
					hiddenWin->Unlock();
				}
				goto catastrophic_exit;
			}
		}
		if (hiddenWin->Lock()) {
			hiddenWin->RemoveChild(this);
			hiddenWin->Unlock();
		}

		printJob.CommitJob();
	}

catastrophic_exit:
	if (progress != NULL)
		progress->PostMessage(B_QUIT_REQUESTED);
	if (hiddenWin != NULL)
		hiddenWin->PostMessage(B_QUIT_REQUESTED);
	delete fOutputDev;

	// restore the page
	BWindow* w = fView->Window();
	if (w->Lock()) {
		fView->RestartDoc();
		w->Unlock();
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::Print()
{
	if (fPrintSettings == NULL && PageSetup() != B_NO_ERROR) {
		return;
	}

	PrintView* pView = new PrintView(this, fDoc, &fPageRenderer, fPrintSettings, fTitle->String(), Bounds());

	thread_id tid = spawn_thread(printing_thread, "printing_thread", B_NORMAL_PRIORITY, pView);
	resume_thread(tid);
}
