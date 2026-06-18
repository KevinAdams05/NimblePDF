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
#include <Messenger.h>
#include <Window.h>
#include <locale/Catalog.h>

// xpdf
#include <TextOutputDev.h>

// NimblePDF
#include "CachedPage.h"
#include "NimblePDF.h"
#include "PDFView.h"
#include "TextConversion.h"
#include "Thread.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFSearch"

///////////////////////////////////////////////////////////
//
// Threading model (batch-2 restructure):
//
//   The worker only SCANS pages that are not currently displayed, using its
//   own headless TextOutputDev, and reads the shared PDFDoc under gPdfLock.
//   It never locks the window and never touches fPage / the selection /
//   the clipboard. When it finishes it posts FIND_RESULT_MSG and exits.
//
//   The window thread (PDFView) does everything that touches view state:
//   the current-page search, jumping to the result page, setting the
//   selection, and copying. That keeps fPage access single-threaded and
//   lets the view join the worker with no window<->worker deadlock (the
//   worker can never be blocked on the window lock).
//
//   Invariant preserved: never take the window lock while holding gPdfLock.
//
///////////////////////////////////////////////////////////


class FindThread : public Thread {
public:
	FindThread(PDFDoc* doc, int startPage, int numPages, std::atomic<bool>* stopThread,
	    const BMessenger& findWindow, const BMessenger& view, int32 generation,
	    const char* text, bool caseSensitive, bool backward)
	    : Thread("find_thread", B_LOW_PRIORITY),
	      fDoc(doc),
	      fStartPage(startPage),
	      fNumPages(numPages),
	      fStopThread(stopThread),
	      fFindWindow(findWindow),
	      fView(view),
	      fGeneration(generation),
	      fFindText(text),
	      fCaseSensitive(caseSensitive),
	      fBackward(backward)
	{
	}

	int32 Run();

private:
	bool CanContinue() { return !fStopThread->load(); }
	void PostSetPage(int32 page);
	void PostResult(bool found, int32 page, bool aborted);
	bool ScanPage(TextOutputDev* textOut, Unicode* u, int len, int pg,
	    double* xMin, double* yMin, double* xMax, double* yMax);

	PDFDoc* fDoc;
	int fStartPage;
	int fNumPages;
	std::atomic<bool>* fStopThread;
	BMessenger fFindWindow;
	BMessenger fView;
	int32 fGeneration;
	BString fFindText;
	bool fCaseSensitive;
	bool fBackward;
};

void FindThread::PostSetPage(int32 page)
{
	BMessage msg(FindTextWindow::FIND_SET_PAGE_MSG);
	msg.AddInt32("page", page);
	fFindWindow.SendMessage(&msg);
}

void FindThread::PostResult(bool found, int32 page, bool aborted)
{
	BMessage msg(FindTextWindow::FIND_RESULT_MSG);
	msg.AddInt32("nimblepdf:generation", fGeneration);
	msg.AddBool("found", found);
	msg.AddInt32("page", page);
	msg.AddBool("aborted", aborted);
	fView.SendMessage(&msg);
}

// Render one page into the headless text device and search it. The PDFDoc is
// shared with the render thread, so displayPage() runs under gPdfLock; the
// TextOutputDev is thread-local and needs no lock.
bool FindThread::ScanPage(TextOutputDev* textOut, Unicode* u, int len, int pg,
    double* xMin, double* yMin, double* xMax, double* yMax)
{
	{
		PDFLock lock;
		fDoc->displayPage(textOut, pg, 72, 72, 0, false, true, false);
	}
	return textOut->findText(u, len, true, true, false, false, fCaseSensitive,
	    fBackward, false /* TODO/FIXME: wordwise */, xMin, yMin, xMax, yMax);
}

int32 FindThread::Run()
{
	int32 len;
	Unicode* u = Utf8ToUnicode(fFindText.String(), &len);
	if (u == NULL) {
		PostResult(false, 0, false);
		return 0;
	}

	// poppler 25.12: no TextOutputControl; headless text-extraction ctor
	// (fileName=NULL, physLayout, fixedPitch=0, rawOrder=false, append=false).
	TextOutputDev* textOut = new TextOutputDev(NULL, true, 0, false, false);
	if (!textOut->isOk()) {
		delete textOut;
		delete[] u;
		PostResult(false, 0, false);
		return 0;
	}

	double xMin, yMin, xMax, yMax;
	bool found = false;
	bool aborted = false;
	int foundPage = 0;
	int pg;

	// Pages after (forward) / before (backward) the start page.
	for (pg = fBackward ? fStartPage - 1 : fStartPage + 1;
	     fBackward ? pg >= 1 : pg <= fNumPages;
	     pg += fBackward ? -1 : 1) {
		if (!CanContinue()) {
			aborted = true;
			goto done;
		}
		PostSetPage(pg);
		if (ScanPage(textOut, u, len, pg, &xMin, &yMin, &xMax, &yMax)) {
			found = true;
			foundPage = pg;
			goto done;
		}
	}

	// Wrap around: the remaining pages up to (but not including) the start page.
	for (pg = fBackward ? fNumPages : 1;
	     fBackward ? pg > fStartPage : pg < fStartPage;
	     pg += fBackward ? -1 : 1) {
		if (!CanContinue()) {
			aborted = true;
			goto done;
		}
		PostSetPage(pg);
		if (ScanPage(textOut, u, len, pg, &xMin, &yMin, &xMax, &yMax)) {
			found = true;
			foundPage = pg;
			goto done;
		}
	}

done:
	delete textOut;
	delete[] u;
	PostResult(found, foundPage, aborted);
	return 0;
}

///////////////////////////////////////////////////////////
// PDFView find driver (all of this runs on the window thread).

// Search the currently displayed page and, on a hit, set + copy the selection.
// The flag quartet matches CachedPage::FindText (startAtTop, stopAtBottom,
// startAtLast, stopAtLast).
bool PDFView::FindTextHere(bool startAtTop, bool stopAtBottom, bool startAtLast, bool stopAtLast)
{
	int32 len;
	Unicode* u = Utf8ToUnicode(fFindString.String(), &len);
	if (u == NULL)
		return false;

	double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
	bool found = GetPage()->FindText(u, len, startAtTop, stopAtBottom, startAtLast,
	    stopAtLast, fFindCaseSensitive, fFindBackward, &xMin, &yMin, &xMax, &yMax);
	delete[] u;

	if (found) {
		SetSelection((int)floor(xMin), (int)floor(yMin), (int)ceil(xMax), (int)ceil(yMax), true);
#ifndef NO_TEXT_SELECT
		if (GetPDFDoc()->okToCopy())
			CopySelection();
#endif
	}
	return found;
}

void PDFView::ShowNotFoundAlert()
{
	BAlert* alert = new BAlert("Error", B_TRANSLATE("Search string not found."),
	    B_TRANSLATE("OK"), 0, 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	// Asynchronous: this runs on the window thread, so a modal Go() would
	// block the looper.
	alert->Go(NULL);
}

void PDFView::NotifyFind(bool found)
{
	BWindow* window = Window();
	if (window != NULL) {
		window->PostMessage((uint32)(found ? FindTextWindow::TEXT_FOUND_NOTIFY_MSG
		                                    : FindTextWindow::TEXT_NOT_FOUND_NOTIFY_MSG));
	}
}

// Re-find on the currently displayed page for an accurate rectangle (the
// headless scan and the displayed page can coalesce text differently), then
// select/copy and notify. Runs on the window thread.
void PDFView::FinalizeFindSelection()
{
	bool ok = FindTextHere(true, true, false, false);
	if (!ok)
		ShowNotFoundAlert();
	NotifyFind(ok);
}

// Finalize a background scan on the window thread.
void PDFView::HandleFindResult(BMessage* msg)
{
	int32 generation = -1;
	msg->FindInt32("nimblepdf:generation", &generation);
	if (generation != fFindGeneration) {
		// Stale result from a worker that was already superseded/stopped.
		return;
	}
	fFindThreadId = -1;

	bool found = false;
	bool aborted = false;
	msg->FindBool("found", &found);
	msg->FindBool("aborted", &aborted);

	if (found) {
		int32 page = 1;
		msg->FindInt32("page", &page);
		SetPage(page);
		if (fRendering) {
			// The result page is now rendering. Finalize from PostRedraw when
			// it completes, instead of blocking the looper in WaitForPage().
			fPendingFindSelect = true;
		} else {
			// Already current/rendered (no new render started) — finalize now.
			FinalizeFindSelection();
		}
	} else if (aborted) {
		// User pressed Stop; just let the dialog reset, no "not found" alert.
		NotifyFind(false);
	} else {
		// Scanned everything; finish the wrap-around on the still-current page.
		bool ok = FindTextHere(true, false, false, true);
		if (!ok)
			ShowNotFoundAlert();
		NotifyFind(ok);
	}
}

void PDFView::StopAndJoinFind()
{
	fStopFindThread = true;
	if (fFindThreadId >= 0) {
		thread_id id = fFindThreadId;
		fFindThreadId = -1;
		// Safe even though we may hold the window lock: the find worker never
		// blocks on the window lock (it only takes gPdfLock briefly and posts
		// async messages), and this thread does not hold gPdfLock here.
		status_t result;
		wait_for_thread(id, &result);
	}
}

void PDFView::Find(const char* s, bool ignoreCase, bool backward, FindTextWindow* findWindow)
{
	// One search at a time: stop and join any previous worker before touching
	// the shared search state or starting a new scan.
	StopAndJoinFind();

	fFindString = s;
	fFindCaseSensitive = !ignoreCase;
	fFindBackward = backward;
	fFindGeneration++;

	// Phase 1: search the current page (from the last match / selection) on the
	// window thread. The common case finishes here with no worker at all.
	if (FindTextHere(false, true, true, false)) {
		NotifyFind(true);
		return;
	}

	// Phase 2: scan the other pages on a background worker.
	int numPages = 0;
	{
		PDFLock lock;
		numPages = GetPDFDoc()->getNumPages();
	}

	fStopFindThread = false;
	BMessenger findMessenger(findWindow);
	BMessenger viewMessenger(this);
	FindThread* thread = new FindThread(GetPDFDoc(), Page(), numPages, &fStopFindThread,
	    findMessenger, viewMessenger, fFindGeneration, s, fFindCaseSensitive, backward);
	// Read the id before Resume(): once resumed, DoRun() owns and may delete
	// the Thread object at any time.
	fFindThreadId = thread->GetThreadId();
	thread->Resume();
}

void PDFView::StopFind()
{
	// Non-blocking: the worker observes the atomic flag, bails, and posts its
	// (aborted) result; the join happens at the next Find()/teardown.
	fStopFindThread = true;
}
