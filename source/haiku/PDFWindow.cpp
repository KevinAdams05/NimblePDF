/*
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013-2016 waddlesplash.
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

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

// xpdf
#include <Object.h>
#include <Gfx.h>

// BeOS
#include <locale/Catalog.h>
#include <Roster.h>
#include <MessageQueue.h>
#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <Screen.h>
#include <ScrollView.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <TabView.h>
#include <View.h>
#include <Path.h>
#include <Directory.h>
#include <Entry.h>
#include <NodeMonitor.h>
#include <Beep.h>
#include <Debug.h>
#include <LayoutBuilder.h>

// BePDF
#include "Logging.h"
#include "AnnotationWindow.h"
#include "AnnotWriter.h"
#include "AttachmentView.h"
#include "BePDF.h"
#include "BepdfApplication.h"
#include "EntryMenuItem.h"
#include "FileInfoWindow.h"
#include "FindTextWindow.h"
#include "LayoutUtils.h"
#include "OutlinesWindow.h"
#include "PageLabels.h"
#include "PageRenderer.h"
#include "PasswordWindow.h"
#include "PDFView.h"
#include "PDFWindow.h"
#include "PreferencesWindow.h"
#include "PrintSettingsWindow.h"
#include "ResourceLoader.h"
#include "SaveThread.h"
#include "StatusBar.h"
#include "TraceWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFWindow"


char* PDFWindow::PAGE_MSG_LABEL = "page";

// Implementation of RecentDocumentsMenu

RecentDocumentsMenu::RecentDocumentsMenu(const char* title, uint32 what, menu_layout layout)
    : BMenu(title, layout),
      fWhat(what)
{}

bool RecentDocumentsMenu::AddDynamicItem(add_state s)
{
	if (s != B_INITIAL_ADD)
		return false;

	BMenuItem* item;
	BMessage list, *msg;
	entry_ref ref;
	char name[B_FILE_NAME_LENGTH];

	while ((item = RemoveItem((int32)0)) != NULL) {
		delete item;
	}

	be_roster->GetRecentDocuments(&list, 20, NULL, BEPDF_APP_SIG);
	for (int i = 0; list.FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref);
		if (entry.Exists() && entry.GetName(name) == B_OK) {
			msg = new BMessage(fWhat);
			msg->AddRef("refs", &ref);
			item = new EntryMenuItem(&ref, name, msg, 0, 0);
			AddItem(item);
			if (fWhat == B_REFS_RECEIVED) {
				item->SetTarget(be_app, NULL);
			}
		}
	}

	return false;
}


///////////////////////////////////////////////////////////
/*
	Check errors that may happen
*/
PDFWindow::PDFWindow(entry_ref* ref, BRect frame, const char* ownerPassword, const char* userPassword, bool* encrypted)
    : BWindow(frame, "PDF", B_DOCUMENT_WINDOW, 0)
{
	fMainView = NULL;
	fPagesView = NULL;
	fAttachmentView = NULL;
	fPageNumberItem = NULL;
	fPrintSettings = NULL;
	fTotalPageNumberItem = NULL;
	fFindWindow = NULL;
	fPreferencesItem = NULL;
	fFileInfoItem = NULL;
	fFindInProgress = false;

	fZoomMenu = fRotationMenu = NULL;
	fLayerView = NULL;

	fOWMessenger = NULL;
	fFIWMessenger = NULL;
	fPSWMessenger = NULL;
	fAWMessenger = NULL;

	fPrintSettingsWindowOpen = false;

	fShowLeftPanel = true;
	fFullScreen = false;

	fPendingMask = 0;

	fPressedAnnotationButton = NULL;

	AddHandler(&fEntryChangedMonitor);
	fEntryChangedMonitor.SetEntryChangedListener(this);

	InitAnnotTemplates();

	SetUpViews(ref, ownerPassword, userPassword, encrypted);

	GlobalSettings* settings = gApp->GetSettings();
	int32 ws = settings->GetWorkspace();
	if (settings->GetOpenInWorkspace() && ws >= 1 && ws <= count_workspaces()) {
		SetWorkspaces(1 << (ws - 1));
	}
	fCurrentWorkspace = Workspaces();

	if (fMainView != NULL) {
		fMainView->Redraw();
		InitAfterOpen();
	}

	SetSizeLimits(938, 10000, 131, 10000);
}


///////////////////////////////////////////////////////////
PDFWindow::~PDFWindow()
{
	RemoveHandler(&fEntryChangedMonitor);

	DeleteAnnotTemplates();
	if (fPagesView) {
		MakeEmpty(fPagesView);
	}
}

void PDFWindow::SetTotalPageNumber(int pages)
{
	const char* fmt = B_TRANSLATE("of %d");
	int len = strlen(fmt) + 30;
	char* label = new char[len];
	snprintf(label, len, fmt, pages);
	fTotalPageNumberItem->SetText(label);
	delete label;
}

void PDFWindow::InitAfterOpen()
{
	GlobalSettings* s = gApp->GetSettings();
	if (Lock()) {
		// set page number text
		SetTotalPageNumber(fMainView->GetNumPages());

		// set window frame
		if (s->GetRestoreWindowFrame()) {
			float left, top;
			fFileAttributes.GetLeftTop(left, top);
			fMainView->ScrollTo(left, top);
		}

		// set page number list
		if (gPdfLock->LockWithTimeout(0) == B_OK) {
			UpdatePageList();
			gPdfLock->Unlock();
		} else {
			FillPageList();
			SetPending(UPDATE_PAGE_LIST_PENDING);
		}
		// select page number
		fPagesView->Select(fFileAttributes.GetPage() - 1);
		fPagesView->ScrollToSelection();
		if (s->GetRestorePageNumber()) {
			SetZoom(s->GetZoom());
			SetRotation(s->GetRotation());
		}
		Unlock();
	}
}


void PDFWindow::FillPageList()
{
	BList list;
	for (int32 i = 0; i < fMainView->GetNumPages(); i++) {
		char pageNo[20];
		sprintf(pageNo, "%5.0d", (int)(i + 1));
		list.AddItem(new BStringItem(pageNo));
	}

	MakeEmpty(fPagesView);
	fPagesView->AddList(&list);
	// clear attachments
	fAttachmentView->Empty();
}


void PDFWindow::UpdatePageList()
{
	gPdfLock->Lock();
	PageLabels labels(fMainView->GetNumPages() - 1);
	Object catDict;
	fMainView->GetPDFDoc()->getXRef()->getCatalog(&catDict);
	Object* pageLabels = new Object;
	catDict.dictLookup("PageLabels", pageLabels);
	if (labels.Parse(pageLabels)) {
		labels.Replace(fPagesView);
	}

	// update attachments as well
	fAttachmentView->Fill(fMainView->GetPDFDoc()->getXRef(), fMainView->GetPDFDoc());

	gPdfLock->Unlock();
}

bool PDFWindow::SetPendingIfLocked(uint32 mask)
{
	if (gPdfLock->LockWithTimeout(0) == B_OK) {
		gPdfLock->Unlock();
		return false;
	} else {
		// could not lock, schedule action later
		SetPending(mask);
		return true;
	}
}

void PDFWindow::HandlePendingActions(bool ok)
{
	if (IsPending(UPDATE_PAGE_LIST_PENDING))
		UpdatePageList();

	if (ok) {
		BMessage msg;
		if (IsPending(UPDATE_OUTLINE_LIST_PENDING)) {
			msg.what = SHOW_BOOKMARKS_CMD;
			MessageReceived(&msg);
		}
		if (IsPending(FILE_INFO_PENDING)) {
			msg.what = FILE_INFO_CMD;
			MessageReceived(&msg);
		}
		if (IsPending(PRINT_SETTINGS_PENDING)) {
			msg.what = PRINT_SETTINGS_CMD;
			MessageReceived(&msg);
		}
	}
	ClearPending();
}

///////////////////////////////////////////////////////////
void PDFWindow::StoreFileAttributes()
{
	// store file settings
	if (fMainView && Lock()) {
		entry_ref cur_ref;
		if (fCurrentFile.InitCheck() == B_OK) {
			fCurrentFile.GetRef(&cur_ref);
			// BePDF #115 defensive guard: only update bookmarks if the
			// outlines view actually loaded them this session. Without
			// this check, closing a file whose bookmark panel was never
			// opened would write an empty list back to the
			// bepdf:bookmarks attribute and silently destroy the user's
			// saved bookmarks.
			if (fOutlinesView->WasActivated()) {
				BMessage bm;
				if (fOutlinesView->GetBookmarks(&bm))
					fFileAttributes.SetBookmarks(&bm);
			}
			fFileAttributes.Write(&cur_ref, gApp->GetSettings());
		}
		Unlock();
	}
}

///////////////////////////////////////////////////////////
bool PDFWindow::QuitRequested()
{
	gApp->WindowClosed();
	fMainView->WaitForPage(true);
	StoreFileAttributes();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

///////////////////////////////////////////////////////////
void PDFWindow::CleanUpBeforeLoad()
{
	EditAnnotation(false);
}


///////////////////////////////////////////////////////////
bool PDFWindow::IsCurrentFile(entry_ref* ref) const
{
	entry_ref r;
	fCurrentFile.GetRef(&r);
	return r == *ref;
}

///////////////////////////////////////////////////////////
bool PDFWindow::LoadFile(entry_ref* ref, const char* ownerPassword, const char* userPassword, bool* encrypted)
{
	if (fMainView != NULL) {
		StoreFileAttributes();
		CleanUpBeforeLoad();
		// load new file
		if (fMainView->LoadFile(ref, &fFileAttributes, ownerPassword, userPassword, false, encrypted)) {
			fEntryChangedMonitor.StartWatching(ref);
			be_roster->AddToRecentDocuments(ref, BEPDF_APP_SIG);
			fCurrentFile.SetTo(ref);
			InitAfterOpen();
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////
void PDFWindow::Reload(void)
{
	BMessage m(B_REFS_RECEIVED);
	entry_ref ref;
	fCurrentFile.GetRef(&ref);
	m.AddRef("refs", &ref);
	be_app->PostMessage(&m);
}

///////////////////////////////////////////////////////////
void PDFWindow::EntryChanged()
{
	Reload();
}

///////////////////////////////////////////////////////////
bool PDFWindow::CancelCommand(BMessage* msg)
{
	// This is a work around:
	// This commands aren't allowed in fullscreen mode, otherwise
	// the windows opened by this commands would be behind the
	// main window and would not block the main window.
	if (fFullScreen) {
		switch (msg->what) {
		case OPEN_FILE_CMD:
		case RELOAD_FILE_CMD:
		case PAGESETUP_FILE_CMD:
		case ABOUT_APP_CMD:
		case FIND_CMD:
		case FIND_NEXT_CMD:
		case PREFERENCES_FILE_CMD:
		case FILE_INFO_CMD:
		case PRINT_SETTINGS_CMD:
			beep();
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////
bool PDFWindow::ActivateWindow(BMessenger* messenger)
{
	if (messenger && messenger->LockTarget()) {
		BLooper* looper;
		messenger->Target(&looper);
		((BWindow*)looper)->Activate(true);
		looper->Unlock();
		return true;
	} else {
		return false;
	}
}

///////////////////////////////////////////////////////////
bool PDFWindow::CanClose()
{
	return true;
}

///////////////////////////////////////////////////////////
AnnotationWindow* PDFWindow::GetAnnotationWindow()
{
	if (fAWMessenger && fAWMessenger->LockTarget()) {
		BLooper* looper;
		fAWMessenger->Target(&looper);
		return (AnnotationWindow*)looper;
	} else {
		return NULL;
	}
}

///////////////////////////////////////////////////////////
AnnotationWindow* PDFWindow::ShowAnnotationWindow()
{
	AnnotationWindow* w = GetAnnotationWindow();
	if (!w) {
		delete fAWMessenger;
		w = new AnnotationWindow(gApp->GetSettings(), this);
		fAWMessenger = new BMessenger(w);
		w->Lock();
	}
	return w;
}


void PDFWindow::UpdateInputEnabler()
{
	if (fMainView) {
		PDFDoc* doc = fMainView->GetPDFDoc();
		int num_pages = fMainView->GetNumPages();
		int page = fMainView->Page();
		bool b = num_pages > 1 && page != 1;

		fMenuBar->FindItem(FIRST_PAGE_CMD)->SetEnabled(b);
		fToolBar->SetActionEnabled(FIRST_PAGE_CMD, b);
		fMenuBar->FindItem(PREVIOUS_PAGE_CMD)->SetEnabled(b);
		fToolBar->SetActionEnabled(PREVIOUS_N_PAGE_CMD, b);

		b = num_pages > 1 && page != num_pages;
		fMenuBar->FindItem(LAST_PAGE_CMD)->SetEnabled(b);
		fToolBar->SetActionEnabled(LAST_PAGE_CMD, b);
		fMenuBar->FindItem(NEXT_PAGE_CMD)->SetEnabled(b);
		fToolBar->SetActionEnabled(NEXT_PAGE_CMD, b);
		fToolBar->SetActionEnabled(NEXT_N_PAGE_CMD, b);

		fPageNumberItem->SetEnabled(num_pages > 1);

		fToolBar->SetActionEnabled(HISTORY_FORWARD_CMD, fMainView->CanGoForward());
		fToolBar->SetActionEnabled(HISTORY_BACK_CMD, fMainView->CanGoBack());

		int32 dpi = fMainView->GetZoomDPI();
		fToolBar->SetActionEnabled(ZOOM_IN_CMD, dpi != ZOOM_DPI_MAX);
		fToolBar->SetActionEnabled(ZOOM_OUT_CMD, dpi != ZOOM_DPI_MIN);

		fToolBar->SetActionEnabled(FIND_NEXT_CMD, fFindText.Length() > 0);

		int active = fLayerView->CardLayout()->VisibleIndex();
		fToolBar->SetActionPressed(SHOW_PAGE_LIST_CMD, fShowLeftPanel && active == PAGE_LIST_PANEL);
		fToolBar->SetActionPressed(SHOW_BOOKMARKS_CMD, fShowLeftPanel && active == BOOKMARKS_PANEL);
		fToolBar->SetActionPressed(SHOW_ANNOT_TOOLBAR_CMD, fShowLeftPanel && active == ANNOTATIONS_PANEL);
		fToolBar->SetActionPressed(SHOW_ATTACHMENTS_CMD, fShowLeftPanel && active == ATTACHMENTS_PANEL);
		fToolBar->SetActionPressed(FULL_SCREEN_CMD, fFullScreen);

		fMenuBar->FindItem(SHOW_PAGE_LIST_CMD)->SetMarked(fShowLeftPanel && active == PAGE_LIST_PANEL);
		fMenuBar->FindItem(SHOW_BOOKMARKS_CMD)->SetMarked(fShowLeftPanel && active == BOOKMARKS_PANEL);
		fMenuBar->FindItem(SHOW_ANNOT_TOOLBAR_CMD)->SetMarked(fShowLeftPanel && active == ANNOTATIONS_PANEL);
		fMenuBar->FindItem(SHOW_ATTACHMENTS_CMD)->SetMarked(fShowLeftPanel && active == ATTACHMENTS_PANEL);
		fMenuBar->FindItem(HIDE_LEFT_PANEL_CMD)->SetEnabled(fShowLeftPanel);

		fMenuBar->FindItem(OPEN_FILE_CMD)->SetEnabled(!fFullScreen);
		fToolBar->SetActionEnabled(OPEN_FILE_CMD, !fFullScreen);
		fMenuBar->FindItem(RELOAD_FILE_CMD)->SetEnabled(!fFullScreen);
		fToolBar->SetActionEnabled(RELOAD_FILE_CMD, !fFullScreen);
		fMenuBar->FindItem(PRINT_SETTINGS_CMD)->SetEnabled(!fFullScreen && !fPrintSettingsWindowOpen && doc->okToPrint());
		fToolBar->SetActionEnabled(PRINT_SETTINGS_CMD, !fFullScreen && !fPrintSettingsWindowOpen && doc->okToPrint());

		// PDF security settings
		bool okToCopy = doc->okToCopy();
		fMenuBar->FindItem(COPY_SELECTION_CMD)->SetEnabled(okToCopy);
		fMenuBar->FindItem(SELECT_ALL_CMD)->SetEnabled(okToCopy);
		fMenuBar->FindItem(SELECT_NONE_CMD)->SetEnabled(okToCopy);

		bool hasBookmark = fOutlinesView->HasUserBookmark(page);
		bool selected = hasBookmark && fOutlinesView->IsUserBMSelected();
		fMenuBar->FindItem(ADD_BOOKMARK_CMD)->SetEnabled(!hasBookmark);
		fMenuBar->FindItem(EDIT_BOOKMARK_CMD)->SetEnabled(selected);
		fMenuBar->FindItem(DELETE_BOOKMARK_CMD)->SetEnabled(selected);

		// Annotation
		bool editAnnot = fMainView->EditingAnnot();
		fToolBar->SetActionEnabled(DONE_EDIT_ANNOT_CMD, editAnnot);
	}
}


void PDFWindow::AddItem(BMenu* subMenu, const char* label, uint32 cmd, bool marked, char shortcut, uint32 modifiers)
{
	BMenuItem* item = new BMenuItem(label, new BMessage(cmd), shortcut, modifiers);
	item->SetMarked(marked);
	subMenu->AddItem(item);
}


void PDFWindow::UpdateWindowsMenu()
{
	/*
	BMenuItem *item;
	while ((item = fWindowsMenu->RemoveItem((int32)0)) != NULL) delete item;
	BList list;
	be_roster->GetAppList(BEPDF_APP_SIG, &list);
	entry_ref ref;
	const int n = list.CountItems();

	for (int i = n-1; i >= 0; i --) {
		team_id who = (team_id)list.ItemAt(i);
		char s[256];
		sprintf(s, "BePDF %d", who);
		fWindowsMenu->AddItem(new BMenuItem(s, NULL));
	}
*/
}


BMenuBar* PDFWindow::BuildMenu()
{
	BString label;
	GlobalSettings* settings = gApp->GetSettings();
	int16 zoom = settings->GetZoom();
	float rotation = settings->GetRotation();

	BMenuBar* menuBar = new BMenuBar("mainBar");
	BLayoutBuilder::Menu<>(menuBar)
	    .AddMenu(B_TRANSLATE("File"))
	    .AddItem(fOpenMenu = new RecentDocumentsMenu(B_TRANSLATE("Open" B_UTF8_ELLIPSIS), B_REFS_RECEIVED))
	    .AddItem(fNewMenu = new RecentDocumentsMenu(B_TRANSLATE("Open in new window" B_UTF8_ELLIPSIS), OPEN_IN_NEW_WINDOW_CMD))
	    .AddItem(B_TRANSLATE("Reload"), RELOAD_FILE_CMD, 'R')
	    .AddItem(B_TRANSLATE("Save as" B_UTF8_ELLIPSIS), SAVE_FILE_AS_CMD, 'S', B_SHIFT_KEY)
	    .AddItem(fFileInfoItem = new BMenuItem(B_TRANSLATE("File info" B_UTF8_ELLIPSIS), new BMessage(FILE_INFO_CMD), 'I'))
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Page setup" B_UTF8_ELLIPSIS), PAGESETUP_FILE_CMD, 'S')
	    .AddItem(B_TRANSLATE("Print" B_UTF8_ELLIPSIS), PRINT_SETTINGS_CMD, 'P')
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Close"), CLOSE_FILE_CMD, 'W')
	    .AddItem(B_TRANSLATE("Quit"), QUIT_APP_CMD, 'Q')
	    .End()

	    .AddMenu(B_TRANSLATE("Edit"))
	    .AddItem(B_TRANSLATE("Copy selection"), COPY_SELECTION_CMD, 'C')
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Select all"), SELECT_ALL_CMD, 'A')
	    .AddItem(B_TRANSLATE("Select none"), SELECT_NONE_CMD, 'A', B_SHIFT_KEY)
	    .AddSeparator()
	    .AddItem(fPreferencesItem = new BMenuItem(B_TRANSLATE("Preferences" B_UTF8_ELLIPSIS), new BMessage(PREFERENCES_FILE_CMD), ','))
	    .End()

	    .AddMenu(B_TRANSLATE("View"))
	    .AddItem(B_TRANSLATE("Show bookmarks"), SHOW_BOOKMARKS_CMD, 'B')
	    .AddItem(B_TRANSLATE("Show page list"), SHOW_PAGE_LIST_CMD, 'L')
	    .AddItem(B_TRANSLATE("Show annotation tool bar"), SHOW_ANNOT_TOOLBAR_CMD)
	    .AddItem(B_TRANSLATE("Show attachments"), SHOW_ATTACHMENTS_CMD)
	    .AddItem(B_TRANSLATE("Hide side bar"), HIDE_LEFT_PANEL_CMD, 'H')
	    .AddSeparator()
	    .AddItem(fFullScreenItem = new BMenuItem(B_TRANSLATE("Fullscreen"), new BMessage(FULL_SCREEN_CMD), B_RETURN))
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Fit to page width"), (FIT_TO_PAGE_WIDTH_CMD), '/')
	    .AddItem(B_TRANSLATE("Fit to page"), (FIT_TO_PAGE_CMD), '*')
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Zoom in"), (ZOOM_IN_CMD), '+')
	    .AddItem(B_TRANSLATE("Zoom out"), (ZOOM_OUT_CMD), '-')
	    .AddSeparator()

	    .AddMenu(fZoomMenu = new BMenu(B_TRANSLATE("Zoom")))
	    .AddItem("25%", SET_ZOOM_VALUE_CMD, MIN_ZOOM == zoom)
	    .AddItem("33%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 1 == zoom)
	    .AddItem("50%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 2 == zoom)
	    .AddItem("66%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 3 == zoom)
	    .AddItem("75%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 4 == zoom)
	    .AddItem("100%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 5 == zoom)
	    .AddItem("125%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 6 == zoom)
	    .AddItem("150%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 7 == zoom)
	    .AddItem("175%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 8 == zoom)
	    .AddItem("200%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 9 == zoom)
	    .AddItem("300%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 10 == zoom)
	    .End()

	    .AddSeparator()

	    .AddMenu(fRotationMenu = new BMenu(B_TRANSLATE("Rotation")))
	    .AddItem("0°", SET_ROTATE_VALUE_CMD, rotation == 0)
	    .AddItem("90°", SET_ROTATE_VALUE_CMD, rotation == 90)
	    .AddItem("180°", SET_ROTATE_VALUE_CMD, rotation == 180)
	    .AddItem("270°", SET_ROTATE_VALUE_CMD, rotation == 270)
	    .End()

	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Show error messages"), SHOW_TRACER_CMD, 'M')
	    .End()

	    .AddMenu(B_TRANSLATE("Search"))
	    .AddItem(B_TRANSLATE("Find" B_UTF8_ELLIPSIS), FIND_CMD, 'F')
	    .AddItem(B_TRANSLATE("Find next" B_UTF8_ELLIPSIS), new BMessage(FIND_NEXT_CMD), 'G')
	    .End()

	    .AddMenu(B_TRANSLATE("Page"))
	    .AddItem(B_TRANSLATE("First"), FIRST_PAGE_CMD)
	    .AddItem(B_TRANSLATE("Previous"), PREVIOUS_PAGE_CMD)
	    .AddItem(B_TRANSLATE("Jump to page"), GOTO_PAGE_MENU_CMD, 'J')
	    .AddItem(B_TRANSLATE("Next"), NEXT_PAGE_CMD)
	    .AddItem(B_TRANSLATE("Last"), LAST_PAGE_CMD)
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Back"), HISTORY_BACK_CMD, B_LEFT_ARROW)
	    .AddItem(B_TRANSLATE("Forward"), HISTORY_FORWARD_CMD, B_RIGHT_ARROW)
	    .End()

	    .AddMenu(B_TRANSLATE("Bookmark"))
	    .AddItem(B_TRANSLATE("Add"), ADD_BOOKMARK_CMD)
	    .AddItem(B_TRANSLATE("Delete"), DELETE_BOOKMARK_CMD)
	    .AddItem(B_TRANSLATE("Edit"), EDIT_BOOKMARK_CMD)
	    .End()

	    .AddMenu(B_TRANSLATE("Help"))
	    .AddItem(B_TRANSLATE("Show help" B_UTF8_ELLIPSIS), HELP_CMD)
	    .AddItem(B_TRANSLATE("Online help" B_UTF8_ELLIPSIS), ONLINE_HELP_CMD)
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("Visit homepage" B_UTF8_ELLIPSIS), HOME_PAGE_CMD)
	    .AddItem(B_TRANSLATE("Issue tracker" B_UTF8_ELLIPSIS), BUG_REPORT_CMD)
	    .AddSeparator()
	    .AddItem(B_TRANSLATE("About BePDF" B_UTF8_ELLIPSIS), ABOUT_APP_CMD)
	    .End();

	fZoomMenu->SetRadioMode(true);
	if (zoom < MIN_ZOOM)
		SetZoom(zoom);

	fRotationMenu->SetRadioMode(true);

	//		menuBar->AddItem ( fWindowsMenu = new BMenu(B_TRANSLATE("Window")) );
	UpdateWindowsMenu();

	fOpenMenu->Superitem()->SetTrigger('O');
	fOpenMenu->Superitem()->SetMessage(new BMessage(OPEN_FILE_CMD));
	fOpenMenu->Superitem()->SetShortcut('O', 0);

	fNewMenu->Superitem()->SetTrigger('N');
	fNewMenu->Superitem()->SetMessage(new BMessage(NEW_WINDOW_CMD));
	fNewMenu->Superitem()->SetShortcut('N', 0);

	return menuBar;
}


BToolBar* PDFWindow::BuildToolBar()
{
	fToolBar = new BToolBar;
	fToolBar->SetName("toolbar");
	fToolBar->SetResizingMode(B_FOLLOW_TOP | B_FOLLOW_LEFT_RIGHT);
	fToolBar->SetFlags(B_WILL_DRAW | B_FRAME_EVENTS);

	fToolBar->AddAction(OPEN_FILE_CMD, this, LoadVectorIcon("OPEN_FILE"), B_TRANSLATE("Open file"));
	fToolBar->AddAction(RELOAD_FILE_CMD, this, LoadVectorIcon("RELOAD_FILE"), B_TRANSLATE("Reload file"));
	fToolBar->AddAction(PRINT_SETTINGS_CMD, this, LoadVectorIcon("PRINT"), B_TRANSLATE("Print"));

	fToolBar->AddSeparator();

	fToolBar->AddAction(SHOW_BOOKMARKS_CMD, this, LoadVectorIcon("BOOKMARKS"), B_TRANSLATE("Show bookmarks"), NULL, true);
	fToolBar->AddAction(SHOW_PAGE_LIST_CMD, this, LoadVectorIcon("SHOW_PAGE_LIST"), B_TRANSLATE("Show page list"), NULL, true);
	fToolBar->AddAction(SHOW_ANNOT_TOOLBAR_CMD, this, LoadVectorIcon("SHOW_ANNOT"), B_TRANSLATE("Show annotation toolbar"), NULL, true);
	fToolBar->AddAction(SHOW_ATTACHMENTS_CMD, this, LoadVectorIcon("SHOW_ATTACHMENTS"), B_TRANSLATE("Show attachments"), NULL, true);
	// fToolBar->AddAction(HIDE_LEFT_PANEL_CMD, this,
	//	LoadVectorIcon("HIDE_PAGE_LIST"), B_TRANSLATE("Hide page list"),
	//	NULL, true);

	fToolBar->AddSeparator();

	fToolBar->AddAction(FULL_SCREEN_CMD, this, LoadVectorIcon("FULL_SCREEN"), B_TRANSLATE("Fullscreen mode"), NULL, true);

	fToolBar->AddSeparator();

	fToolBar->AddAction(FIRST_PAGE_CMD, this, LoadVectorIcon("FIRST"), B_TRANSLATE("Go to start of document"));
	fToolBar->AddAction(PREVIOUS_N_PAGE_CMD, this, LoadVectorIcon("PREVIOUS_N"), B_TRANSLATE("Go back 10 pages"));
	fToolBar->AddAction(PREVIOUS_PAGE_CMD, this, LoadVectorIcon("PREVIOUS"), B_TRANSLATE("Go to previous page"));
	fToolBar->AddAction(NEXT_PAGE_CMD, this, LoadVectorIcon("NEXT"), B_TRANSLATE("Go to next page"));
	fToolBar->AddAction(NEXT_N_PAGE_CMD, this, LoadVectorIcon("NEXT_N"), B_TRANSLATE("Go forward 10 pages"));
	fToolBar->AddAction(LAST_PAGE_CMD, this, LoadVectorIcon("LAST"), B_TRANSLATE("Go to end of document"));

	fToolBar->AddSeparator();

	fToolBar->AddAction(HISTORY_BACK_CMD, this, LoadVectorIcon("BACK"), B_TRANSLATE("Back in page history list"));
	fToolBar->AddAction(HISTORY_FORWARD_CMD, this, LoadVectorIcon("FORWARD"), B_TRANSLATE("Forward in page history list"));

	fToolBar->AddSeparator();

	// Add "go to page number" TextControl
	fPageNumberItem = new BTextControl("goto_page", "", "", new BMessage(GOTO_PAGE_CMD));
	fPageNumberItem->SetExplicitMaxSize(BSize(50, 25));
	fPageNumberItem->SetAlignment(B_ALIGN_CENTER, B_ALIGN_CENTER);
	fPageNumberItem->SetTarget(this);
	fPageNumberItem->TextView()->DisallowChar(B_ESCAPE);

	BTextView* t = fPageNumberItem->TextView();
	BFont font(be_plain_font);
	t->GetFontAndColor(0, &font);
	font.SetSize(10);
	t->SetFontAndColor(0, 1000, &font, B_FONT_SIZE);
	fToolBar->AddView(fPageNumberItem);

	// display total number of pages
	fTotalPageNumberItem = new BStringView("total_num_of_pages", "");
	fTotalPageNumberItem->SetAlignment(B_ALIGN_CENTER);
	fTotalPageNumberItem->SetFontSize(10);
	fToolBar->AddView(fTotalPageNumberItem);

	fToolBar->AddSeparator();

	fToolBar->AddAction(FIT_TO_PAGE_WIDTH_CMD, this, LoadVectorIcon("FIT_TO_PAGE_WIDTH"), B_TRANSLATE("Fit to page width"));
	fToolBar->AddAction(FIT_TO_PAGE_CMD, this, LoadVectorIcon("FIT_TO_PAGE"), B_TRANSLATE("Fit to page"));

	fToolBar->AddSeparator();

	fToolBar->AddAction(ROTATE_CLOCKWISE_CMD, this, LoadVectorIcon("ROTATE_CLOCKWISE"), B_TRANSLATE("Rotate clockwise"));
	fToolBar->AddAction(ROTATE_ANTI_CLOCKWISE_CMD, this, LoadVectorIcon("ROTATE_ANTI_CLOCKWISE"), B_TRANSLATE("Rotate counter-clockwise"));
	fToolBar->AddAction(ZOOM_IN_CMD, this, LoadVectorIcon("ZOOM_IN"), B_TRANSLATE("Zoom in"));
	fToolBar->AddAction(ZOOM_OUT_CMD, this, LoadVectorIcon("ZOOM_OUT"), B_TRANSLATE("Zoom out"));

	fToolBar->AddSeparator();

	fToolBar->AddAction(FIND_CMD, this, LoadVectorIcon("FIND"), B_TRANSLATE("Find"));
	fToolBar->AddAction(FIND_NEXT_CMD, this, LoadVectorIcon("FIND_NEXT"), B_TRANSLATE("Find next"));
	fToolBar->AddGlue();
	return fToolBar;
}


BCardView* PDFWindow::BuildLeftPanel()
{
	BCardView* layerView = new BCardView("layers");

	// PageList
	fOutlinesView =
	    new OutlinesView(fMainView->GetPDFDoc()->getCatalog(), fFileAttributes.GetBookmarks(), gApp->GetSettings(), this, B_FRAME_EVENTS);

	fAttachmentView = new AttachmentView(gApp->GetSettings(), this, 0);

	// LayerView contains the page numbers
	fPagesView = new BListView("pagesList", B_SINGLE_SELECTION_LIST, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS);
	fPagesView->SetSelectionMessage(new BMessage(PAGE_SELECTED_CMD));

	BView* pageView = new BScrollView("pageScrollView", fPagesView, B_FRAME_EVENTS, true, true, B_FANCY_BORDER);

	layerView->CardLayout()->AddView(fOutlinesView);
	layerView->CardLayout()->AddView(pageView);
	layerView->CardLayout()->AddView(BuildAnnotToolBar("annotationToolBar", NULL));
	layerView->CardLayout()->AddView(fAttachmentView);

	return layerView;
}


void PDFWindow::SetUpViews(entry_ref* ref, const char* ownerPassword, const char* userPassword, bool* encrypted)
{
	fMenuBar = BuildMenu();
	BuildToolBar();

	fMainView =
	    new PDFView(ref, &fFileAttributes, "mainView", B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS, ownerPassword, userPassword, encrypted);

	fCurrentFile.SetTo(ref);
	if (!fMainView->IsOk()) {
		delete fMainView;
		fMainView = NULL;
		return; // ERROR!
	}
	fEntryChangedMonitor.StartWatching(ref);

	fMainContainer = new BView("ScrollContainer", 0);
	BScrollView* mainScrollView = new BScrollView("scrollView", fMainView, 0, true, true, B_FANCY_BORDER);
	mainScrollView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	mainScrollView->SetExplicitMinSize(BSize(0, 0));

	BLayoutBuilder::Group<>(fMainContainer, B_VERTICAL, 0).SetInsets(0, 0, -1, -1).Add(mainScrollView).End();
	fMainContainer->SetExplicitMinSize(BSize(0, 0));

	// left view of SplitView is a LayerView
	fLayerView = BuildLeftPanel();

	// SplitView
	fSplitView = new BSplitView(B_HORIZONTAL);
	fSplitView->AddChild(fLayerView, 1);
	fSplitView->AddChild(fMainContainer, 9);
	fSplitView->SetInsets(0);
	fSplitView->SetSpacing(4);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0).SetInsets(0, 0, -1, -1).Add(fMenuBar).Add(fToolBar).Add(fSplitView).End();

	SetTotalPageNumber(fMainView->GetNumPages());

	GlobalSettings* s = gApp->GetSettings();

	// show or hide panel that is stored in settings
	ShowLeftPanel(s->GetLeftPanel());
	if (!s->GetShowLeftPanel()) {
		// hide panel
		ToggleLeftPanel();
	}

	// set focus to PDFView, so it receives mouse and keyboard events
	fMainView->MakeFocus();
}


void PDFWindow::SetZoom(int16 zoom)
{
	BMenuItem* item;
	gApp->GetSettings()->SetZoom(zoom);
	if (zoom >= MIN_ZOOM) {
		item = fZoomMenu->ItemAt(zoom - MIN_ZOOM);
		if (item != NULL)
			item->SetMarked(true);
	} else {
		item = fZoomMenu->FindItem(CUSTOM_ZOOM_FACTOR_MSG);
		if (item != NULL) {
			fZoomMenu->RemoveItem(item);
			delete item;
		}
		char label[256];
		sprintf(label, B_TRANSLATE("Custom zoom factor (%d%%)"), -zoom * 100 / 72);
		BMessage* msg = new BMessage(CUSTOM_ZOOM_FACTOR_MSG);
		msg->AddInt16("zoom", zoom);
		item = new BMenuItem(label, msg, 0);
		fZoomMenu->AddItem(item);
		item->SetMarked(true);
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::SetRotation(float rotation)
{
	int16 i;
	if (rotation <= 45)
		i = 0;
	else if (rotation <= 90.0 + 45)
		i = 1;
	else if (rotation <= 180.0 + 45)
		i = 2;
	else if (rotation <= 270.0 + 45)
		i = 3;
	else
		i = 0;
	BMenuItem* item = fRotationMenu->ItemAt(i);
	item->SetMarked(true);
}

void PDFWindow::NewDoc(PDFDoc* doc)
{
	Catalog* catalog = doc->getCatalog();

	fOutlinesView->SetCatalog(catalog, fFileAttributes.GetBookmarks());
	ActivateOutlines();

	if (fFIWMessenger && fFIWMessenger->LockTarget()) {
		BLooper* looper;
		FileInfoWindow* w = (FileInfoWindow*)fFIWMessenger->Target(&looper);
		w->Refresh(&fCurrentFile, doc, fFileAttributes.GetPage());
		looper->Unlock();
	}
	if (fPSWMessenger && fPSWMessenger->LockTarget()) {
		BLooper* looper;
		PrintSettingsWindow* w = (PrintSettingsWindow*)fPSWMessenger->Target(&looper);
		w->Refresh(doc);
		looper->Unlock();
	}
	if (fAWMessenger && fAWMessenger->LockTarget()) {
		BLooper* looper;
		AnnotationWindow* w = (AnnotationWindow*)fAWMessenger->Target(&looper);
		w->Quit();
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::NewPage(int page)
{
	UpdateInputEnabler();
	if (fFIWMessenger && fFIWMessenger->LockTarget()) {
		BLooper* looper;
		FileInfoWindow* w = (FileInfoWindow*)fFIWMessenger->Target(&looper);
		w->RefreshFontList(&fCurrentFile, fMainView->GetPDFDoc(), page);
		looper->Unlock();
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::FrameMoved(BPoint p)
{
	if (!fFullScreen) {
		gApp->GetSettings()->SetWindowPosition(p);
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::FrameResized(float width, float height)
{
	if (!fFullScreen) {
		gApp->GetSettings()->SetWindowSize(width, height);
	}
}


void PDFWindow::SetZoomSize(float w, float h)
{
	// TODO / FIXME
}

///////////////////////////////////////////////////////////
// update page list and page number item
void PDFWindow::SetPage(int32 page)
{
	char pageStr[64];
	if (page <= 0)
		page = 1;
	if (page > fPagesView->CountItems()) {
		page = fPagesView->CountItems();
	}
	snprintf(pageStr, sizeof(pageStr), "%" B_PRId32, page);
	fPageNumberItem->SetText(pageStr);
	fPagesView->Select(page - 1);
	fPagesView->ScrollToSelection();
}


void PDFWindow::MessageReceived(BMessage* message)
{
	int32 page;
	const char* text;

	if (CancelCommand(message))
		return;

	switch (message->what) {
	case OPEN_FILE_CMD:
		fMainView->WaitForPage();
		EditAnnotation(false);
		gApp->OpenFilePanel();
		break;
	case NEW_WINDOW_CMD:
		be_roster->Launch(BEPDF_APP_SIG, 0, (char**)NULL);
		break;
	case OPEN_IN_NEW_WINDOW_CMD: {
		BMessage m(B_REFS_RECEIVED);
		entry_ref r;
		if (message->FindRef("refs", 0, &r) == B_OK) {
			BEntry entry(&r);
			BPath path;
			entry.GetPath(&path);
			OpenPDF(path.Path());
		}
	} break;
	case RELOAD_FILE_CMD:
		Reload();
		break;
	case SAVE_FILE_AS_CMD:
		gApp->OpenSaveFilePanel(this, GetPdfFilter());
		break;
	case CLOSE_FILE_CMD:
		fMainView->WaitForPage(true);
		PostMessage(B_QUIT_REQUESTED);
		break;
	case QUIT_APP_CMD:
		gApp->Notify(BepdfApplication::NOTIFY_QUIT_MSG);
		break;
	case PAGESETUP_FILE_CMD:
		fMainView->PageSetup();
		break;
	case ABOUT_APP_CMD:
		be_app->PostMessage(B_ABOUT_REQUESTED);
		break;
	case COPY_SELECTION_CMD:
		fMainView->CopySelection();
		break;
	case SELECT_ALL_CMD:
		fMainView->SelectAll();
		break;
	case SELECT_NONE_CMD:
		fMainView->SelectNone();
		break;
	case FIRST_PAGE_CMD:
		fMainView->MoveToPage(1);
		break;
	case PREVIOUS_N_PAGE_CMD:
		fMainView->MoveToPage(fMainView->Page() - 10);
		break;
	case NEXT_N_PAGE_CMD:
		fMainView->MoveToPage(fMainView->Page() + 10);
		break;
	case PREVIOUS_PAGE_CMD:
		if (B_SHIFT_KEY & modifiers()) {
			fMainView->ScrollVertical(false, 0.95);
		} else {
			page = fMainView->Page();
			fMainView->MoveToPage(page - 1);
		}
		break;
	case NEXT_PAGE_CMD:
		if (B_SHIFT_KEY & modifiers()) {
			fMainView->ScrollVertical(true, 0.95);
		} else {
			page = fMainView->Page();
			fMainView->MoveToPage(page + 1);
		}
		break;
	case LAST_PAGE_CMD:
		fMainView->MoveToPage(fMainView->GetNumPages());
		break;
	case GOTO_PAGE_CMD: {
		status_t result;
		BTextControl* control;
		BControl* ptr;

		result = message->FindPointer("source", (void**)&ptr);
		if (result == B_OK) {
			control = dynamic_cast<BTextControl*>(ptr);
			if (result == B_OK && control != NULL) {
				const char* txt = control->Text();
				page = atoi(txt);
			}
		} else { // may come from external source over page parameter
			result = message->FindInt32("page", &page);
			if (result == B_OK)
				fMainView->WaitForPage();
		}

		if (result == B_OK) {
			LockLooper();
			fMainView->MoveToPage(page);
			UnlockLooper();
			fMainView->MakeFocus();
		}
		break;
	}
	case PAGE_SELECTED_CMD:
		page = fPagesView->CurrentSelection(0) + 1;
		fMainView->MoveToPage(page);
		break;
	case GOTO_PAGE_MENU_CMD:
		fPageNumberItem->MakeFocus();
		break;
	case SET_ZOOM_VALUE_CMD: {
		status_t err;
		BMenuItem* item;
		BMenu* menu;
		BArchivable* ptr;
		int32 idx;

		err = message->FindPointer("source", (void**)&ptr);
		item = dynamic_cast<BMenuItem*>(ptr);
		if (err == B_OK && item != NULL) {
			menu = item->Menu();
			if (menu == NULL) {
				// ERROR
			} else {
				idx = menu->IndexOf(item);
				if (idx > MAX_ZOOM) {
					idx = MAX_ZOOM;
				}
				SetZoom(idx);
				fMainView->SetZoom(idx);
			}
		}
	} break;
	case ZOOM_IN_CMD:
	case ZOOM_OUT_CMD:
		fMainView->Zoom(message->what == ZOOM_IN_CMD);
		break;
	case FIT_TO_PAGE_WIDTH_CMD:
		fMainView->FitToPageWidth();
		break;
	case FIT_TO_PAGE_CMD:
		fMainView->FitToPage();
		break;
	case SET_ROTATE_VALUE_CMD: {
		status_t err;
		BMenuItem* item;
		BMenu* menu;
		BArchivable* ptr;
		int32 idx;

		err = message->FindPointer("source", (void**)&ptr);
		item = dynamic_cast<BMenuItem*>(ptr);
		if (err == B_OK && item != NULL) {
			menu = item->Menu();
			if (menu == NULL) {
				// ERROR
			} else {
				idx = menu->IndexOf(item);
				fMainView->SetRotation(idx * 90);
			}
		}
	} break;
	case ROTATE_CLOCKWISE_CMD:
		fMainView->RotateClockwise();
		break;
	case ROTATE_ANTI_CLOCKWISE_CMD:
		fMainView->RotateAntiClockwise();
		break;
	case HISTORY_BACK_CMD:
		fMainView->Back();
		break;
	case HISTORY_FORWARD_CMD:
		fMainView->Forward();
		break;

	case FIND_CMD:
		fMainView->WaitForPage();
		if (Lock()) {
			fFindWindow = new FindTextWindow(gApp->GetSettings(), fFindText.String(), this);
			Unlock();
		}
		break;
	case FIND_NEXT_CMD:
		fMainView->WaitForPage();
		if (Lock()) {
			fFindWindow = new FindTextWindow(gApp->GetSettings(), fFindText.String(), this);
			Unlock();
			fFindWindow->PostMessage('Find');
		}
		break;
		/*	case KEYBOARD_SHORTCUTS_CMD: {
			BAlert *info = new BAlert("Info",
				"Keyboard Shortcuts:\n\n"
				"Space - scroll forward on a page\n"
				"Backspace - scroll backwards on page\n"
				"Cursor Arrow Keys - scroll incrementally in the direction of the cursor key\n"
				"Page Up - skip to the previous page\n"
				"Page Down - skip to the next page\n"
				"Home - return to the beginning of the document\n"
				"End - advance to the end of the document\n"
				"ALT+B - return to the previously viewed page within the document"
				, "OK");
			info->Go();
		}
		break;*/
	case HELP_CMD:
		OpenHelp();
		break;
	case ONLINE_HELP_CMD:
		LaunchHTMLBrowser("http://haikuarchives.github.io/BePDF/English/table_of_contents.html");
		break;
	case HOME_PAGE_CMD:
		LaunchHTMLBrowser("http://haikuarchives.github.io/BePDF/");
		break;
	case BUG_REPORT_CMD:
		LaunchHTMLBrowser("http://github.com/HaikuArchives/BePDF/issues/");
		break;
	case PREFERENCES_FILE_CMD:
		fPreferencesItem->SetEnabled(false);
		new PreferencesWindow(gApp->GetSettings(), this);
		break;
	case FILE_INFO_CMD:
		if (SetPendingIfLocked(FILE_INFO_PENDING))
			return;
		if (!ActivateWindow(fFIWMessenger)) {
			FileInfoWindow* w;
			fMainView->WaitForPage();
			w = new FileInfoWindow(gApp->GetSettings(), &fCurrentFile, fMainView->GetPDFDoc(), this, fFileAttributes.GetPage());
			fFIWMessenger = new BMessenger(w);
		}
		break;
	case PRINT_SETTINGS_CMD: {
		if (SetPendingIfLocked(PRINT_SETTINGS_PENDING))
			return;
		PrintSettingsWindow* w;
		fPrintSettingsWindowOpen = true;
		UpdateInputEnabler();
		w = new PrintSettingsWindow(fMainView->GetPDFDoc(), gApp->GetSettings(), this);
		fPSWMessenger = new BMessenger(w);
	} break;
	case SHOW_BOOKMARKS_CMD:
		if (fShowLeftPanel && fLayerView->CardLayout()->VisibleIndex() == BOOKMARKS_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(BOOKMARKS_PANEL);
		break;
	case SHOW_PAGE_LIST_CMD:
		if (fShowLeftPanel && fLayerView->CardLayout()->VisibleIndex() == PAGE_LIST_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(PAGE_LIST_PANEL);
		break;
	case SHOW_ANNOT_TOOLBAR_CMD:
		if (fShowLeftPanel && fLayerView->CardLayout()->VisibleIndex() == ANNOTATIONS_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(ANNOTATIONS_PANEL);
		break;
	case SHOW_ATTACHMENTS_CMD:
		if (fShowLeftPanel && fLayerView->CardLayout()->VisibleIndex() == ATTACHMENTS_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(ATTACHMENTS_PANEL);
		break;
	case HIDE_LEFT_PANEL_CMD:
		HideLeftPanel();
		break;
	case FULL_SCREEN_CMD:
		OnFullScreen();
		break;
	case ADD_BOOKMARK_CMD:
		AddBookmark();
		break;
	case DELETE_BOOKMARK_CMD:
		DeleteBookmark();
		break;
	case EDIT_BOOKMARK_CMD:
		EditBookmark();
		break;
	case SHOW_TRACER_CMD:
		OutputTracer::ShowWindow(gApp->GetSettings());
		break;
	// Annotation
	case DONE_EDIT_ANNOT_CMD:
		EditAnnotation(false);
		break;
	// Attachments
	case ATTACHMENT_SELECTION_CHANGED_MSG:
		message->PrintToStream();
		break;

	case CUSTOM_ZOOM_FACTOR_MSG: {
		int16 zoom;
		if (message->FindInt16("zoom", &zoom) == B_OK) {
			SetZoom(zoom);
			fMainView->SetZoom(zoom);
		}
	} break;

	// Find Text Window
	case FindTextWindow::FIND_START_NOTIFY_MSG: {
		bool ignoreCase;
		bool backward;
		fFindInProgress = true;
		fFindState = (uint32)FindTextWindow::FIND_STOP_NOTIFY_MSG;
		message->FindString("text", &text);
		message->FindBool("ignoreCase", &ignoreCase);
		message->FindBool("backward", &backward);
		fFindText.SetTo(text);
		fMainView->Find(text, ignoreCase, backward, fFindWindow);
		break;
	}
	case FindTextWindow::FIND_STOP_NOTIFY_MSG:
	case FindTextWindow::FIND_ABORT_NOTIFY_MSG:
		if (fFindInProgress) {
			fFindState = message->what;
			fMainView->StopFind();
		} else {
			fFindWindow->PostMessage(message->what);
			if (message->what == (uint32)FindTextWindow::FIND_ABORT_NOTIFY_MSG) {
				fFindWindow->PostMessage(FindTextWindow::FIND_QUIT_REQUESTED_MSG);
			}
		}
		UpdateInputEnabler();
		break;
	case FindTextWindow::TEXT_FOUND_NOTIFY_MSG:
	case FindTextWindow::TEXT_NOT_FOUND_NOTIFY_MSG:
		fFindInProgress = false;
		fFindWindow->PostMessage(fFindState);
		if (fFindState == (uint32)FindTextWindow::FIND_ABORT_NOTIFY_MSG) {
			fFindWindow->PostMessage(FindTextWindow::FIND_QUIT_REQUESTED_MSG);
		}
		break;

	// Page Renderer
	case PageRenderer::UPDATE_MSG:
	case PageRenderer::FINISH_MSG: {
		thread_id id;
		BBitmap* bitmap;
		PageRenderer::GetParameter(message, &id, &bitmap);
		fMainView->PostRedraw(id, bitmap);
		HandlePendingActions(message->what == PageRenderer::FINISH_MSG);
	} break;
	case PageRenderer::ABORT_MSG: {
		thread_id id;
		BBitmap* bitmap;
		PageRenderer::GetParameter(message, &id, &bitmap);
		fMainView->RedrawAborted(id, bitmap);
		HandlePendingActions(false);
	} break;

	// Preferences Window
	case PreferencesWindow::RESTART_DOC_NOTIFY:
		fMainView->WaitForPage(true);
		fMainView->RestartDoc();
		break;
	case PreferencesWindow::CHANGE_NOTIFY: {
		int16 kind, which, index;
		if (PreferencesWindow::DecodeMessage(message, kind, which, index)) {
			switch (kind) {
			case PreferencesWindow::DISPLAY:
				switch (which) {
				case PreferencesWindow::DISPLAY_FILLED_SELECTION:
					fMainView->SetFilledSelection(index == 0);
					break;
				}
			}
		}
	} break;
	case PreferencesWindow::QUIT_NOTIFY:
		fPreferencesItem->SetEnabled(true);
		break;
	case PreferencesWindow::UPDATE_NOTIFY:
		fMainView->UpdateSettings(gApp->GetSettings());
		break;

	// File Info Window
	case FileInfoWindow::QUIT_NOTIFY:
		fFileInfoItem->SetEnabled(true);
		delete fFIWMessenger;
		fFIWMessenger = NULL;
		break;
	case FileInfoWindow::START_QUERY_ALL_FONTS_MSG:
		fMainView->WaitForPage(); // need exculsive access to PDFDoc
		if (fFIWMessenger && fFIWMessenger->LockTarget()) {
			BLooper* looper;
			FileInfoWindow* w = (FileInfoWindow*)fFIWMessenger->Target(&looper);
			looper->Unlock();
			w->QueryAllFonts(fMainView->GetPDFDoc());
		}
		break;
	// Print Settings Window
	case PrintSettingsWindow::QUIT_NOTIFY:
		// fPrintSettingsItem->SetEnabled(true);
		fPrintSettingsWindowOpen = false;
		UpdateInputEnabler();
		delete fPSWMessenger;
		fPSWMessenger = NULL;
		break;
	case PrintSettingsWindow::PRINT_NOTIFY:
		fMainView->WaitForPage();
		fMainView->Print();
		break;

	// Outlines View (TODO simplify, BMessenger not needed any more)
	case OutlinesView::PAGE_NOTIFY: {
		int32 page;
		if (message->FindInt32("page", &page) == B_OK) {
			fMainView->MoveToPage(page);
			UpdateInputEnabler();
		}
	} break;
	case OutlinesView::REF_NOTIFY: {
		int32 num, gen;
		if (message->FindInt32("num", &num) == B_OK && message->FindInt32("gen", &gen) == B_OK) {
			fMainView->MoveToPage(num, gen, true);
		}
	} break;
	case OutlinesView::STRING_NOTIFY: {
		BString s;
		if (message->FindString("string", &s) == B_OK) {
			fMainView->MoveToPage(s.String());
		}
	} break;
	case OutlinesView::DEST_NOTIFY: {
		void* link;
		if (message->FindPointer("dest", &link) == B_OK) {
			LinkDest* dest = static_cast<LinkDest*>(link);
			fMainView->GotoDest(dest);
		}
	} break;
	case OutlinesView::QUIT_NOTIFY:
		delete fOWMessenger;
		fOWMessenger = NULL;
		break;
	case OutlinesView::STATE_CHANGE_NOTIFY:
		UpdateInputEnabler();
		break;
	case BookmarkWindow::BOOKMARK_ENTERED_NOTIFY: {
		BString label;
		int32 pageNum;
		if (message->FindString("label", &label) == B_OK && message->FindInt32("pageNum", &pageNum) == B_OK) {
			fOutlinesView->AddUserBookmark(pageNum, label.String());
			UpdateInputEnabler();
		}
	}
	case AnnotationWindow::QUIT_NOTIFY:
		delete fAWMessenger;
		fAWMessenger = NULL;
		break;
	case AnnotationWindow::CHANGE_NOTIFY: {
		void* p;
		if (message->FindPointer("annotation", &p) == B_OK) {
			fMainView->UpdateAnnotation((Annotation*)p, message);
		}
	} break;

	case B_SAVE_REQUESTED:
		SaveFile(message);
		break;

	default:
		if (FIRST_ANNOT_CMD <= message->what && message->what <= LAST_ANNOT_CMD) {
			InsertAnnotation(message->what);
		} else
			BWindow::MessageReceived(message);
	}
}


void PDFWindow::OpenPDF(const char* file)
{
	char* argv[2] = {(char*)file, NULL};
	be_roster->Launch(BEPDF_APP_SIG, 1, argv);
}


bool PDFWindow::OpenPDFHelp(const char* name)
{
	BPath path(*gApp->GetAppPath());
	path.Append("docs");
	path.Append(name);
	BEntry entry(path.Path());
	if (entry.InitCheck() == B_OK && entry.Exists()) {
		OpenPDF(path.Path());
		return true;
	}
	return false;
}


void PDFWindow::OpenHelp()
{
	OpenPDFHelp(B_TRANSLATE_COMMENT("English.pdf", "Replace with the PDF name of the help document, if there is one for your language."));
}


void PDFWindow::LaunchHTMLBrowser(const char* path)
{
	char* argv[2] = {(char*)path, NULL};
	be_roster->Launch("text/html", 1, argv);
}


void PDFWindow::LaunchInHome(const char* rel_path)
{
	BPath path(*gApp->GetAppPath());
	path.Append(rel_path);
	Launch(path.Path());
}

bool PDFWindow::FindFile(BPath* path)
{
	if (path->InitCheck() != B_OK)
		return false;
	BString leaf(path->Leaf());
	if (leaf.Length() == 0) {
		path->SetTo("/");
		return true;
	}
	if (path->GetParent(path) != B_OK)
		return false;
	if (FindFile(path)) {
		BPath p(path->Path());
		path->Append(leaf.String());
		BEntry entry(path->Path());
		if (entry.Exists())
			return true;

		*path = p;
		entry.SetTo(p.Path());
		BDirectory dir(&entry);
		char name[B_FILE_NAME_LENGTH];
		while (dir.GetNextEntry(&entry) == B_OK) {
			entry.GetName(name);
			if (leaf.ICompare(name) == 0) {
				path->Append(name);
				return true;
			}
		}
	}
	return false;
}

bool PDFWindow::GetEntryRef(const char* file, entry_ref* ref)
{
	BEntry entry(file);
	BPath path(file);
	if (!entry.Exists() && FindFile(&path)) {
		entry.SetTo(path.Path());
	}
	if (entry.Exists()) {
		entry.GetRef(ref);
		return true;
	}
	return false;
}


void PDFWindow::Launch(const char* file)
{
	entry_ref r;
	if (GetEntryRef(file, &r)) {
		be_roster->Launch(&r);
	}
}

void PDFWindow::OpenInWindow(const char* file)
{
	entry_ref r;
	if (GetEntryRef(file, &r)) {
		BMessage msg(B_REFS_RECEIVED);
		msg.AddRef("refs", &r);
		be_app->PostMessage(&msg);
	}
}


// #pragma mark - Left Panel


void PDFWindow::ActivateOutlines()
{
	// fMainView->WaitForPage();
	if (fLayerView->CardLayout()->VisibleIndex() == BOOKMARKS_PANEL && fShowLeftPanel) {
		fMainView->WaitForPage();
		fOutlinesView->Activate();
	}
}


void PDFWindow::ShowLeftPanel(int panel)
{
	if (!fShowLeftPanel) {
		ToggleLeftPanel();
	}
	if (fLayerView->CardLayout()->VisibleIndex() != panel) {
		gApp->GetSettings()->SetLeftPanel(panel);
		fLayerView->CardLayout()->SetVisibleItem(panel);
	}
	// BePDF #115: Activate must fire whenever BOOKMARKS_PANEL becomes
	// (or already is) the visible panel — not only when the index
	// transitions. At startup the default visible index is 0, which
	// equals BOOKMARKS_PANEL, so the old `!=` guard skipped Activate
	// and the bookmark list stayed empty even though the panel was
	// shown. ActivateOutlines() is idempotent (gated on fNeedsUpdate).
	if (panel == BOOKMARKS_PANEL) {
		ActivateOutlines();
	}
	UpdateInputEnabler();
}


void PDFWindow::HideLeftPanel()
{
	if (fShowLeftPanel) {
		ToggleLeftPanel();
	}
}


void PDFWindow::ToggleLeftPanel()
{
	fShowLeftPanel = !fShowLeftPanel;
	fSplitView->SetItemCollapsed(0, !fShowLeftPanel);
	gApp->GetSettings()->SetShowLeftPanel(fShowLeftPanel);
	if (fShowLeftPanel) {
		ActivateOutlines();
		fSplitView->SetFlags(B_NAVIGABLE | fSplitView->Flags());
	} else {
		fSplitView->SetFlags((~B_NAVIGABLE) & fSplitView->Flags());
	}
	UpdateInputEnabler();
	fMainView->Resize();
}


void PDFWindow::OnFullScreen()
{
	bool quasiFullScreenMode = gApp->GetSettings()->GetQuasiFullscreenMode();
	fFullScreen = !fFullScreen;
	BRect frame;
	if (fFullScreen) {
		fWindowFrame = Frame();
		frame = gScreen->Frame();
		if (quasiFullScreenMode) {
			frame.OffsetBy(0, -fMenuBar->Bounds().Height());
			frame.bottom += fMenuBar->Bounds().Height();
		} else {
			HideLeftPanel();
			BRect bounds = fMainView->Parent()->ConvertToScreen(fMainView->Frame());
			frame.bottom += fWindowFrame.IntegerHeight() - bounds.IntegerHeight();
			frame.right += fWindowFrame.IntegerWidth() - bounds.IntegerWidth();
			frame.OffsetBy(-bounds.left + fWindowFrame.left, -bounds.top + fWindowFrame.top);
		}
		fFullScreenItem->SetMarked(true);
		SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
		SetFlags(Flags() | B_NOT_RESIZABLE | B_NOT_MOVABLE);
		Activate(true);
	} else {
		SetFeel(B_NORMAL_WINDOW_FEEL);
		SetWorkspaces(B_CURRENT_WORKSPACE);
		SetFlags(Flags() & ~(B_NOT_RESIZABLE | B_NOT_MOVABLE));
		frame = fWindowFrame;
		fFullScreenItem->SetMarked(false);
	}
	MoveTo(frame.left, frame.top);
	ResizeTo(frame.Width(), frame.Height());

	UpdateInputEnabler();
}

//~ this is very restrictive: it assumes that the window is only set in one workspace
void PDFWindow::WorkspaceActivated(int32 workspace, bool active)
{
#ifdef MORE_DEBUG
	Trace(LOG_DEBUG,
	    "%s %d %s %d %d\n",
	    fFullScreen ? "fullscreen" : "window",
	    workspace,
	    active ? "active" : "not active",
	    fCurrentWorkspace,
	    Workspaces());
#endif
	if (fFullScreen) {
		if (fCurrentWorkspace == 1 << workspace) {
			SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
		} else {
			SetFeel(B_NORMAL_WINDOW_FEEL);
			SetWorkspaces(fCurrentWorkspace);
		}
	} else if (active) {
		fCurrentWorkspace = 1 << workspace;
	}
}

// #pragma mark - User-defined bookmarks

void PDFWindow::AddBookmark()
{
	char buffer[256];
	sprintf(buffer, B_TRANSLATE("Page %d"), fMainView->Page());
	new BookmarkWindow(fMainView->Page(), buffer, BRect(30, 30, 300, 200), this);
}

void PDFWindow::DeleteBookmark()
{
	fOutlinesView->RemoveUserBookmark(fMainView->Page());
	UpdateInputEnabler();
}

void PDFWindow::EditBookmark()
{
	const char* label = fOutlinesView->GetUserBMLabel(fMainView->Page());
	if (label) {
		new BookmarkWindow(fMainView->Page(), label, BRect(30, 30, 300, 200), this);
	} else {
		// should not reach here
	}
}

// #pragma mark - Annotations

static const int32 kAnnotDescEOL = -1;
static const int32 kAnnotDescSeparator = -2;

static AnnotDesc annotDescs[] = {{PDFWindow::ADD_COMMENT_TEXT_ANNOT_CMD, B_TRANSLATE("Add comment text annotation"), "ANNOT_COMMENT"},
    {PDFWindow::ADD_HELP_TEXT_ANNOT_CMD, B_TRANSLATE("Add help text annotation"), "ANNOT_HELP"},
    {PDFWindow::ADD_INSERT_TEXT_ANNOT_CMD, B_TRANSLATE("Add insert text annotation"), "ANNOT_INSERT"},
    {PDFWindow::ADD_KEY_TEXT_ANNOT_CMD, B_TRANSLATE("Add key text annotation"), "ANNOT_KEY"},
    {PDFWindow::ADD_NEW_PARAGRAPH_TEXT_ANNOT_CMD, B_TRANSLATE("Add new paragraph text annotation"), "ANNOT_NEW_PARAGRAPH"},
    {PDFWindow::ADD_NOTE_TEXT_ANNOT_CMD, B_TRANSLATE("Add note text annotation"), "ANNOT_NOTE"},
    {PDFWindow::ADD_PARAGRAPH_TEXT_ANNOT_CMD, B_TRANSLATE("Add paragraph text annotation"), "ANNOT_PARAGRAPH"},
    {PDFWindow::ADD_LINK_ANNOT_CMD, B_TRANSLATE("Add link annotation"), "ANNOT_LINK"},
    {kAnnotDescSeparator, NULL, NULL},
    {PDFWindow::ADD_FREETEXT_ANNOT_CMD, B_TRANSLATE("Add free text annotation"), "ANNOT_FREETEXT"},
    {PDFWindow::ADD_LINE_ANNOT_CMD, B_TRANSLATE("Add line annotation"), "ANNOT_LINE"},
    {PDFWindow::ADD_SQUARE_ANNOT_CMD, B_TRANSLATE("Add square annotation"), "ANNOT_SQUARE"},
    {PDFWindow::ADD_CIRCLE_ANNOT_CMD, B_TRANSLATE("Add circle annotation"), "ANNOT_CIRCLE"},
    {PDFWindow::ADD_HIGHLIGHT_ANNOT_CMD, B_TRANSLATE("Add highlight annotation"), "ANNOT_HIGHLIGHT"},
    {PDFWindow::ADD_UNDERLINE_ANNOT_CMD, B_TRANSLATE("Add underline annotation"), "ANNOT_UNDERLINE"},
    {PDFWindow::ADD_SQUIGGLY_ANNOT_CMD, B_TRANSLATE("Add squiggly annotation"), "ANNOT_SQUIGGLY"},
    {PDFWindow::ADD_STRIKEOUT_ANNOT_CMD, B_TRANSLATE("Add strikeout annotation"), "ANNOT_STRIKEOUT"},
    {PDFWindow::ADD_STAMP_ANNOT_CMD, B_TRANSLATE("Add stamp annotation"), "ANNOT_STAMP"},
    {PDFWindow::ADD_INK_ANNOT_CMD, B_TRANSLATE("Add ink annotation"), "ANNOT_INK"},
    {PDFWindow::ADD_POPUP_ANNOT_CMD, B_TRANSLATE("Add popup annotation"), "ANNOT_POPUP"},
    {PDFWindow::ADD_FILEATTACHMENT_ANNOT_CMD, B_TRANSLATE("Add file attachment annotation"), "ANNOT_FILEATTACHMENT"},
    {PDFWindow::ADD_SOUND_ANNOT_CMD, B_TRANSLATE("Add sound annotation"), "ANNOT_SOUND"},
    {PDFWindow::ADD_MOVIE_ANNOT_CMD, B_TRANSLATE("Add movie annotation"), "ANNOT_MOVIE"},
    {PDFWindow::ADD_WIDGET_ANNOT_CMD, B_TRANSLATE("Add widget annotation"), "ANNOT_WIDGET"},
    {PDFWindow::ADD_PRINTERMARK_ANNOT_CMD, B_TRANSLATE("Add printer mark annotation"), "ANNOT_PRINTERMARK"},
    {PDFWindow::ADD_TRAPNET_ANNOT_CMD, B_TRANSLATE("Add trapnet annotation"), "ANNOT_TRAPNET"},
    {kAnnotDescEOL, NULL, NULL}};

BView* PDFWindow::BuildAnnotToolBar(const char* name, AnnotDesc* desc)
{
	fAnnotationBar = new BToolBar(B_VERTICAL);
	fAnnotationBar->SetName(name);

	fAnnotationBar->AddAction(DONE_EDIT_ANNOT_CMD, this, LoadVectorIcon("DONE_ANNOT"), B_TRANSLATE("Leave annotation editing mode"));
	fAnnotationBar->AddAction(SAVE_FILE_AS_CMD, this, LoadVectorIcon("SAVE_FILE_AS"), B_TRANSLATE("Save file as"));

	fAnnotationBar->AddSeparator();

	// add buttons for supported annotations
	for (desc = annotDescs; desc->fCmd != kAnnotDescEOL; desc++) {
		if (desc->fCmd == kAnnotDescSeparator) {
			fAnnotationBar->AddSeparator();
			continue;
		}

		Annotation* annot = GetAnnotTemplate(desc->fCmd);
		if (annot == NULL)
			continue;

		fAnnotationBar->AddAction(desc->fCmd, this, LoadVectorIcon(desc->fButtonPrefix), desc->fToolTip, NULL, true);
	}
	fAnnotationBar->AddGlue();

	/*BScrollView* sc = new BScrollView("AnnotToolbarScroll", fAnnotationBar,
		0, false, true, B_PLAIN_BORDER);
	sc->SetExplicitMinSize(BSize(0, 0));
	BScrollBar* sb = sc->ScrollBar(B_VERTICAL);
	float range;
	sb->GetRange(NULL, &range);
	sb->SetRange(0, range * 0.35);
	sb->SetSteps(5, 15);
	sb->SetProportion(0.5);*/
	BView* CV = new BView("CV", 0);
	BLayoutBuilder::Group<>(CV, B_HORIZONTAL).AddGlue(0).Add(fAnnotationBar).AddGlue(0).End();
	return CV;
}

bool PDFWindow::TryEditAnnot()
{
	if (fMainView->GetPDFDoc()->isEncrypted()) {
		BAlert* alert = new BAlert(
		    B_TRANSLATE("Warning"), B_TRANSLATE("Editing of annotations in an encrypted PDF file isn't supported yet!"), B_TRANSLATE("OK"));
		alert->Go();
		return false;
	} else {
		EditAnnotation(true);
		return true;
	}
}

void PDFWindow::EditAnnotation(bool edit)
{
	if (edit == fMainView->EditingAnnot()) {
		return;
	}
	if (edit) {
		fMainView->BeginEditAnnot();
	} else {
		ReleaseAnnotationButton();
		fMainView->EndEditAnnot();
	}
	fMainView->Invalidate();
	UpdateInputEnabler();
}

void PDFWindow::InitAnnotTemplates()
{
	for (int i = 0; i < NUM_ANNOTS; i++)
		fAnnotTemplates[i] = NULL;

	PDFRectangle rect;
	// rect.x1 == -1 means that when the annotation is added to the the page
	// resize mode should be enabled otherwise the rectangle should be used
	// as default and move mode should be enabled.
	rect.x1 = -1;
	rect.x2 = 40;
	rect.y1 = 0;
	rect.y2 = 40;

	for (int i = 0; i < TextAnnot::no_of_types - 1; i++) {
		BBitmap* bitmap = gApp->GetTextAnnotImage(i);
		PDFRectangle rect;
		BRect bounds(bitmap->Bounds());
		rect.x1 = rect.y1 = 0;
		rect.x2 = bounds.right;
		rect.y2 = bounds.bottom;
		SetAnnotTemplate(ADD_COMMENT_TEXT_ANNOT_CMD + i, new TextAnnot(rect, (TextAnnot::text_annot_type)i));
	}

	PDFPoint line[2];
	line[0] = PDFPoint(rect.x1, rect.y1);
	line[1] = PDFPoint(rect.x2, rect.y1);

	PDFFont* font = BePDFAcroForm::GetStandardFonts()->FindByName("Helvetica");
	ASSERT(font != NULL);
	SetAnnotTemplate(ADD_FREETEXT_ANNOT_CMD, new FreeTextAnnot(rect, font));

	SetAnnotTemplate(ADD_LINE_ANNOT_CMD, new LineAnnot(rect, line));

	SetAnnotTemplate(ADD_SQUARE_ANNOT_CMD, new SquareAnnot(rect));
	SetAnnotTemplate(ADD_CIRCLE_ANNOT_CMD, new CircleAnnot(rect));
	SetAnnotTemplate(ADD_HIGHLIGHT_ANNOT_CMD, new HighlightAnnot(rect));
	SetAnnotTemplate(ADD_UNDERLINE_ANNOT_CMD, new UnderlineAnnot(rect));
	SetAnnotTemplate(ADD_SQUIGGLY_ANNOT_CMD, new SquigglyAnnot(rect));
	SetAnnotTemplate(ADD_STRIKEOUT_ANNOT_CMD, new StrikeOutAnnot(rect));
}

void PDFWindow::DeleteAnnotTemplates()
{
	for (int i = 0; i < NUM_ANNOTS; i++) {
		delete fAnnotTemplates[i];
		fAnnotTemplates[i] = NULL;
	}
}

void PDFWindow::SetAnnotTemplate(int cmd, Annotation* a)
{
	ASSERT(FIRST_ANNOT_CMD <= cmd && cmd <= LAST_ANNOT_CMD);
	ASSERT(fAnnotTemplates[cmd - FIRST_ANNOT_CMD] == NULL);
	if (CanWrite(a)) {
		fAnnotTemplates[cmd - FIRST_ANNOT_CMD] = a;
		// add popup annotation to annotation if it's not a FreeTextAnnot
		if (dynamic_cast<FreeTextAnnot*>(a) == NULL) {
			PDFRectangle rect;
			rect.x1 = 0;
			rect.x2 = 300;
			rect.y1 = 0;
			rect.y2 = 200;
			PopupAnnot* popup = new PopupAnnot(rect);
			a->SetPopup(popup);
		}
	} else {
		delete a;
	}
}

Annotation* PDFWindow::GetAnnotTemplate(int cmd)
{
	ASSERT(FIRST_ANNOT_CMD <= cmd && cmd <= LAST_ANNOT_CMD);
	return fAnnotTemplates[cmd - FIRST_ANNOT_CMD];
}

void PDFWindow::InsertAnnotation(int cmd)
{
	ReleaseAnnotationButton();

	if (!fMainView->EditingAnnot() && !TryEditAnnot()) {
		return;
	}

	PressAnnotationButton();
	Annotation* templateAnnotation = GetAnnotTemplate(cmd);
	if (templateAnnotation != NULL) {
		fMainView->InsertAnnotation(templateAnnotation);
	} else {
		ReleaseAnnotationButton();
	}
}

class SaveFileThread : public SaveThread {
public:
	SaveFileThread(const char* title, XRef* xref, const char* path, PDFView* view)
	    : SaveThread(title, xref),
	      fPath(path),
	      fMainView(view)
	{}

	int32 Run()
	{
		BAlert* alert = NULL;

		AnnotWriter writer(GetXRef(), fMainView->GetPDFDoc(), fMainView->GetPageRenderer()->GetAnnotsList(), fMainView->GetBePDFAcroForm());
		if (writer.WriteTo(fPath.String())) {
			alert = new BAlert(
			    "Information", B_TRANSLATE("PDF file successfully written!"), B_TRANSLATE("OK"), 0, 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		} else {
			alert = new BAlert("Error", B_TRANSLATE("Could not write PDF file!"), B_TRANSLATE("OK"), 0, 0, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		}

		alert->Go();
		delete this;
		return 0;
	}

private:
	BString fPath;
	PDFView* fMainView;
};

void PDFWindow::SaveFile(BMessage* msg)
{
	entry_ref dir;
	BString name;
	if (msg->FindRef("directory", &dir) == B_OK && msg->FindString("name", &name) == B_OK) {
		BEntry entry(&dir);
		BPath path(&entry);
		path.Append(name.String());
		BEntry newFile(path.Path());
		if (newFile != fCurrentFile) {
			gPdfLock->Lock();
			fMainView->SyncAnnotation(false);
			gPdfLock->Unlock();

			SaveFileThread* thread =
			    new SaveFileThread(B_TRANSLATE("Saving copy of PDF file:"), fMainView->GetPDFDoc()->getXRef(), path.Path(), fMainView);

			thread->Resume();
		} else {
			BAlert* alert = NULL;
			alert = new BAlert(B_TRANSLATE("Warning"),
			    B_TRANSLATE("Can not overwrite a PDF file that's currently opened in BePDF! Please choose another file name."),
			    B_TRANSLATE("OK"));
			alert->Go();
		}
	}
}

void PDFWindow::PressAnnotationButton()
{
	BMessage* msg = CurrentMessage();
	BControl* control;
	if (msg && msg->FindPointer("source", (void**)&control) == B_OK) {
		control->SetValue(B_CONTROL_ON);
		fPressedAnnotationButton = control;
	}
}

void PDFWindow::ReleaseAnnotationButton()
{
	if (fPressedAnnotationButton) {
		fPressedAnnotationButton->SetValue(B_CONTROL_OFF);
		fPressedAnnotationButton = NULL;
	}
}
