/*
 * NimblePDF: The PDF reader for Haiku.
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

// Haiku
#include <Alert.h>
#include <locale/Catalog.h>

// xpdf
#include <TextOutputDev.h>

// NimblePDF
#include "CachedPage.h"
#include "PDFView.h"
#include "TextConversion.h"
#include "Thread.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFSearch"

///////////////////////////////////////////////////////////


class FindThread : public Thread {
public:
	FindThread(const char* s, bool ignoreCase, bool backward, PDFView* mainView, FindTextWindow* find, bool* stopThread);

	int32 Run();

private:
	bool CanContinue() { return !(*fStopThread); }
	BWindow* Window() { return fMainView->Window(); }
	CachedPage* GetPage() { return fMainView->GetPage(); }
	PDFDoc* GetPDFDoc() { return fMainView->GetPDFDoc(); }
	int CurrentPage() { return fMainView->Page(); }

	void SendPageMsg(int32 page);

	PDFView* fMainView;
	FindTextWindow* fFindWindow;
	BString fFindText;
	bool fCaseSensitive;
	bool fBackward;
	bool* fStopThread;
};

FindThread::FindThread(const char* s, bool ignoreCase, bool backward, PDFView* mainView, FindTextWindow* find, bool* stopThread)
    : Thread("find_thread", B_LOW_PRIORITY)
{
	fFindText = s;
	fCaseSensitive = !ignoreCase;
	fBackward = backward;
	fMainView = mainView;
	fFindWindow = find;
	fStopThread = stopThread;
}

void FindThread::SendPageMsg(int32 page)
{
	BMessage msg(FindTextWindow::FIND_SET_PAGE_MSG);
	msg.AddInt32("page", page);
	fFindWindow->PostMessage(&msg);
}

int32 FindThread::Run()
{
	TextOutputDev* textOut = NULL;
	double xMin, yMin, xMax, yMax;
	int pg;
	bool startAtTop, startAtLast, stopAtLast;

	bool next = true;
	bool backward = fBackward;
	bool caseSensitive = fCaseSensitive;
	int selectULX, selectLRX, selectULY, selectLRY;
	PDFDoc* doc = GetPDFDoc();
	bool found = false;
	bool onePageOnly = false;
	int topPage = CurrentPage();

	fMainView->GetSelection(selectULX, selectLRX, selectULY, selectLRY);

	int32 len;
	Unicode* u = Utf8ToUnicode(fFindText.String(), &len);
	if (u == NULL) {
		goto done;
	}

	// Begin Code copied from PDFCore::FindU()

	// search current page starting at previous result, current
	// selection, or top/bottom of page
	startAtTop = startAtLast = false;
	xMin = yMin = xMax = yMax = 0;
	pg = CurrentPage();
	if (next) {
		startAtLast = true;
	} else if (selectULX != selectLRX && selectULY != selectLRY) {
		if (backward) {
			xMin = selectULX - 1;
			yMin = selectULY - 1;
		} else {
			xMin = selectULX + 1;
			yMin = selectULY + 1;
		}
	} else {
		startAtTop = true;
	}
	if (GetPage()->FindText(u, len, startAtTop, true, startAtLast, false, caseSensitive, backward, &xMin, &yMin, &xMax, &yMax)) {
		goto found;
	}

	if (!onePageOnly) {
		// search following/previous pages
		// poppler 25.12: no TextOutputControl; headless text-extraction ctor
		// (fileName=nullptr, physLayout, fixedPitch=0, rawOrder=false, append=false).
		textOut = new TextOutputDev(nullptr, true, 0, false, false);
		if (!textOut->isOk()) {
			delete textOut;
			goto notFound;
		}
		for (pg = backward ? pg - 1 : pg + 1; backward ? pg >= 1 : pg <= doc->getNumPages(); pg += backward ? -1 : 1) {
			// Begin NimblePDF
			if (!CanContinue()) {
				delete textOut;
				goto notFound;
			}
			// End NimblePDF
			SendPageMsg(pg);
			doc->displayPage(textOut, pg, 72, 72, 0, false, true, false);
			if (textOut->findText(u,
			        len,
			        true,
			        true,
			        false,
			        false,
			        caseSensitive,
			        backward,
			        false, // TODO/FIXME: wordwise
			        &xMin,
			        &yMin,
			        &xMax,
			        &yMax)) {
				delete textOut;
				goto foundPage;
			}
		}

		// search previous/following pages
		for (pg = backward ? doc->getNumPages() : 1; backward ? pg > topPage : pg < topPage; pg += backward ? -1 : 1) {
			// Begin NimblePDF
			if (!CanContinue()) {
				delete textOut;
				goto notFound;
			}
			// End NimblePDF
			SendPageMsg(pg);
			doc->displayPage(textOut, pg, 72, 72, 0, false, true, false);
			if (textOut->findText(u,
			        len,
			        true,
			        true,
			        false,
			        false,
			        caseSensitive,
			        backward,
			        false, // TODO/FIXME wordwise
			        &xMin,
			        &yMin,
			        &xMax,
			        &yMax)) {
				delete textOut;
				goto foundPage;
			}
		}
		delete textOut;
	}

	// search current page ending at previous result, current selection,
	// or bottom/top of page
	if (!startAtTop) {
		xMin = yMin = xMax = yMax = 0;
		if (next) {
			stopAtLast = true;
		} else {
			stopAtLast = false;
			xMax = selectLRX;
			yMax = selectLRY;
		}
		if (GetPage()->FindText(u, len, true, false, false, stopAtLast, caseSensitive, backward, &xMin, &yMin, &xMax, &yMax)) {
			goto found;
		}
	}

	// End Code copied from PDFCore::FindU()

	// not found
notFound: {
	BAlert* alert = new BAlert("Error", B_TRANSLATE("Search string not found."), B_TRANSLATE("OK"), 0, 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}
	goto done;

	// found on a different page
foundPage:
	if (Window()->Lock()) {
		fMainView->SetPage(pg);
		fMainView->WaitForPage();
		Window()->Unlock();
	}
	if (!GetPage()->FindText(u, len, true, true, false, false, fCaseSensitive, fBackward, &xMin, &yMin, &xMax, &yMax))
		// this can happen if coalescing is bad
		goto notFound;

	// found: change the selection
found:
	found = true;
	fMainView->SetSelection((int)floor(xMin), (int)floor(yMin), (int)ceil(xMax), (int)ceil(yMax), true);
#ifndef NO_TEXT_SELECT
	if (GetPDFDoc()->okToCopy()) {
		fMainView->CopySelection();
	}
#endif

done:
	delete u;
	Window()->PostMessage((uint32)(found ? FindTextWindow::TEXT_FOUND_NOTIFY_MSG : FindTextWindow::TEXT_NOT_FOUND_NOTIFY_MSG));
	return 0 /*found*/;
}

///////////////////////////////////////////////////////////
void PDFView::Find(const char* s, bool ignoreCase, bool backward, FindTextWindow* findWindow)
{
	fStopFindThread = false;
	FindThread* thread = new FindThread(s, ignoreCase, backward, this, findWindow, &fStopFindThread);
	thread->Resume();
}

void PDFView::StopFind()
{
	fStopFindThread = true;
}
