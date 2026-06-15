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

#include <stdlib.h>
#include <ctype.h>

#include <locale/Catalog.h>
#include <Application.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Roster.h>
#include <Screen.h>
#include <StorageKit.h>
#include <Deskbar.h>
#include <Alert.h>
#include <AboutWindow.h>

#include "Trace.h"
// xpdf "config.h" and "Error.h" replaced by poppler headers; pkg-config
// adds the poppler header path.
#include "Error.h"

// xpdf-era "Init.h" + parseargs / GetGlobalArgDesc / GetPrintHelp are
// gone in the poppler migration. argv parsing and global init are now
// inlined in ArgvReceived (see TODO there). Phase G will replace
// InitXpdf with poppler's GlobalParams setup.

#include "PDFWindow.h"
#include "NimblePDFApplication.h"
#include "ResourceLoader.h"
#include "PasswordWindow.h"
#include "NimblePDF.h"
#include "TraceWindow.h"
#include "FileInfoWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NimblePDFApplication"

static const char* PAGE_NUM_MSG_KEY = "nimblepdf:page_num";

static const char* settingsFilename = "NimblePDF";

static const char* attachmentNames[] = {"GRAPH_ANNOT", "PAPER_CLIP_ANNOT", "PUSH_PIN_ANNOT", "TAG_ANNOT", "UNKNOWN_ATTACHMENT_ANNOT"};

static const char* textAnnotNames[] = {"COMMENT_ANNOT",
    "HELP_ANNOT",
    "INSERT_ANNOT",
    "KEY_ANNOT",
    "NEW_PARAGRAPH_ANNOT",
    "NOTE_ANNOT",
    "PARAGRAPH_ANNOT",
    "UNKNOWN_TEXT_ANNOT"};

// Implementation of PDFFilter
class PDFFilter : public BRefFilter {
	static const char* valid_filetypes[];

public:
	bool Filter(const entry_ref* ref, BNode* node, struct stat_beos* st, const char* filetype);
};

static PDFFilter pdfFilter;

BRefFilter* GetPdfFilter()
{
	return &pdfFilter;
}

const char* PDFFilter::valid_filetypes[] = {"application/x-vnd.Be-directory",
    "application/x-vnd.Be-symlink",
    "application/x-vnd.Be-volume",
    "application/pdf",
    "application/x-pdf",
    NULL};

bool PDFFilter::Filter(const entry_ref* ref, BNode* node, struct stat_beos* st, const char* filetype)
{
	for (int i = 0; valid_filetypes[i]; i++) {
		if (strcmp(filetype, valid_filetypes[i]) == 0)
			return true;
		// check file extension if filetype has not been set to application/pdf
		BString name(ref->name);
		name.ToUpper();
		int32 l = name.FindLast('.');
		if (l != B_ERROR) {
			if (name.FindFirst(".PDF", l) != B_ERROR)
				return true;
		}
	}
	return false;
}
///////////////////////////////////////////////////////////
int main()
{
	new NimblePDFApplication();

	be_app->Run();

	delete be_app;
	return 0;
}


///////////////////////////////////////////////////////////
void NimblePDFApplication::LoadImages(BBitmap* images[], const char* names[], int num)
{
	for (int i = 0; i < num; i++) {
		images[i] = LoadBitmap(names[i], 'BBMP');
		if (!images[i])
			Trace(LOG_WARNING, "Could not load bitmap %s", names[i]);
	}
}

///////////////////////////////////////////////////////////
void NimblePDFApplication::FreeImages(BBitmap* images[], int num)
{
	for (int i = 0; i < num; i++) {
		delete images[i];
		images[i] = NULL;
	}
}

///////////////////////////////////////////////////////////
NimblePDFApplication::NimblePDFApplication()
    : BApplication(NIMBLEPDF_APP_SIG)
{
	fSettings = new GlobalSettings();
	fOpenFilePanel = NULL;
	fSaveFilePanel = NULL;
	fSaveToDirectoryFilePanel = NULL;
	fInitialized = false;
	fGotSomething = false;
	fReadyToQuit = false;
	fWindow = NULL;

	fStdoutTracer = NULL;
	fStderrTracer = NULL;
	pointerCursor = new BCursor(B_CURSOR_ID_SYSTEM_DEFAULT);
	linkCursor = new BCursor(B_CURSOR_ID_CREATE_LINK);
	handCursor = new BCursor(B_CURSOR_ID_GRAB);
	grabCursor = new BCursor(B_CURSOR_ID_GRABBING);
	textSelectionCursor = new BCursor(B_CURSOR_ID_I_BEAM);
	zoomCursor = new BCursor(B_CURSOR_ID_ZOOM_IN);
	splitVCursor = new BCursor(B_CURSOR_ID_RESIZE_NORTH_SOUTH);
	resizeCursor = new BCursor(B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST);

	LoadImages(fAttachmentImages, attachmentNames, FileAttachmentAnnot::no_of_types);
	LoadImages(fTextAnnotImages, textAnnotNames, TextAnnot::no_of_types);

	BEntry entry;
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK) {
		fTeamID = info.team;
		entry = BEntry(&info.ref);
		entry.GetPath(&fAppPath);
		fAppPath.GetParent(&fAppPath);
	} else {
		fAppPath.SetTo(".");
	}

	fDefaultPDF = fAppPath;
	fDefaultPDF.Append("docs/Start.pdf");

	BPath path(fAppPath);
	LoadSettings();

	InitNimblePDF();
}

#include <memory>
#include <vector>

#include <GlobalParams.h>
#include <goo/GooString.h>
#include <poppler-config.h>
#include "DisplayCIDFonts.h"

static void setGlobalParameter(const char* type, const char* arg1, const char* arg2 = NULL)
{
	// poppler 25.12: GlobalParams has no parseLine(), and no fontDir /
	// displayCIDFont* config — it discovers fonts via fontconfig. The xpdf
	// config-line mechanism is gone, so this is a no-op for now.
	// TODO(poppler-migration, phase G): register bundled fonts via
	// GlobalParams::addFontFile and route CID fonts through fontconfig.
	(void)type;
	(void)arg1;
	(void)arg2;
}

/*
 * Originally copied from xpdf/GlobalParams.cc (the function was removed
 * in XPDF 4). xpdf's cidToUnicodes was a GHash<GooString*, void*>
 * iterated via startIter/getNext/killIter. Poppler's GlobalParams does
 * not expose cidToUnicodes; the equivalent walk needs a different
 * accessor.
 *
 * TODO(poppler-migration, phase G): rewrite against poppler's GlobalParams
 * CID-font enumeration API (likely getCIDToUnicode / mapCIDToUnicode
 * iteration, or fall back to walking the CIDFont resource directly).
 *
 * For now this returns an empty vector so Phase A type swaps compile.
 * Effect at runtime: CJK display-CID-font auto-discovery in
 * NimblePDFApplication::Initialize is a no-op until the rewrite lands.
 */
std::vector<GooString*>* getCIDToUnicodeNames(GlobalParams* globalParams)
{
	return new std::vector<GooString*>();
}

void NimblePDFApplication::Initialize()
{
	if (!fInitialized) {
		fInitialized = true;

		// built in fonts
		BPath fontDirectory(fAppPath);
		fontDirectory.Append("fonts");

		// built in encodings
		BPath encodingDirectory(fAppPath);
		encodingDirectory.Append("encodings");

		// TODO(poppler-migration, phase G): replace the bundled-font and
		// encoding-dir setup that InitXpdf() did with the poppler
		// equivalent. Poppler's GlobalParams reads system fontconfig
		// rather than a custom encodings/ tree; the dist/encodings tree
		// becomes superfluous on Haiku once this lands. For Phase A we
		// just instantiate GlobalParams so document opens don't crash.
		globalParams = std::make_unique<GlobalParams>();
		(void)fontDirectory;
		(void)encodingDirectory;

		// system fonts
		BPath systemFontsPath;
		if (find_directory(B_BEOS_FONTS_DIRECTORY, &systemFontsPath) == B_OK) {
			BDirectory directory(systemFontsPath.Path());
			BEntry entry;
			while (directory.GetNextEntry(&entry) == B_OK) {
				if (!entry.IsDirectory())
					continue;
				BPath fontDirectory;
				if (entry.GetPath(&fontDirectory) != B_OK)
					continue;
				setGlobalParameter("fontDir", fontDirectory.Path());
			}
		}

		// CID fonts
		BMessage msg;
		fSettings->GetDisplayCIDFonts(msg);
		DisplayCIDFonts displayNames(msg);

		// record new names
		bool foundNewName = false;
		std::vector<GooString*>* list = getCIDToUnicodeNames(globalParams.get());
		for (size_t i = 0; i < list->size(); i++) {
			GooString* name = list->at(i);
			if (displayNames.Contains(name->c_str())) {
				continue;
			}
			// record name
			displayNames.Set(name->c_str());
			foundNewName = true;
		}

		// store in settings
		if (foundNewName) {
			msg.MakeEmpty();
			displayNames.Archive(msg);
			fSettings->SetDisplayCIDFonts(msg);
		}

		// set CID fonts
		for (int i = 0; i < list->size(); i++) {
			GooString* name = list->at(i);
			BString file;
			DisplayCIDFonts::Type type;

			displayNames.Get(name->c_str(), file, type);
			if (type == DisplayCIDFonts::kUnknownType || file.Length() == 0) {
				continue;
			}

			if (type == DisplayCIDFonts::kTrueType) {
				setGlobalParameter("displayCIDFontTT", name->c_str(), file.String());
			} else {
				setGlobalParameter("displayCIDFontT1", name->c_str(), file.String());
			}
		}

		for (GooString* s : *list)
			delete s;
		delete list;
	}
}

///////////////////////////////////////////////////////////
NimblePDFApplication::~NimblePDFApplication()
{
	SaveSettings();

	delete fSettings;
	fSettings = NULL;

	delete linkCursor;
	linkCursor = NULL;
	delete handCursor;
	handCursor = NULL;
	delete grabCursor;
	grabCursor = NULL;
	delete textSelectionCursor;
	textSelectionCursor = NULL;
	delete zoomCursor;
	zoomCursor = NULL;
	delete splitVCursor;
	splitVCursor = NULL;
	delete resizeCursor;
	resizeCursor = NULL;

	FreeImages(fAttachmentImages, FileAttachmentAnnot::no_of_types);
	FreeImages(fTextAnnotImages, TextAnnot::no_of_types);

	ExitNimblePDF();
}


///////////////////////////////////////////////////////////
void NimblePDFApplication::ReadyToRun()
{
	fStdoutTracer = new OutputTracer(1, "stdout", GetSettings());
	fStderrTracer = new OutputTracer(2, "stderr", GetSettings());

	Initialize();
	if (!fGotSomething) {
		// No document was passed on launch: come up with a blank window so the
		// user can open a file via File > Open... NimblePDF, unlike BePDF, does
		// not auto-open a bundled welcome document.
		BRect rect(fSettings->GetWindowRect());
		fWindow = new PDFWindow(NULL, rect, NULL, NULL, NULL);
		fWindow->Show();
	}
}

///////////////////////////////////////////////////////////
void NimblePDFApplication::AboutRequested()
{
	BString version;
	GetVersion(version);

	// The window picks up the application icon (BEOS:ICON) from our resources
	// automatically, so we only need to supply the text.
	BAboutWindow* about = new BAboutWindow("NimblePDF", NIMBLEPDF_APP_SIG);
	about->SetVersion(version.String());
	about->AddDescription(
	    B_TRANSLATE("A fast, full-featured PDF reader for Haiku."));

	// Kevin's copyright is primary; the original BePDF copyright holders are
	// preserved as required by the GNU GPL (NimblePDF is a fork of BePDF).
	const char* bePdfCopyrights[] = {
	    "1997 Benoit Triquet",
	    "1998-2000 Hubert Figuiere",
	    "2000-2011 Michael Pfeiffer",
	    "2013-2017 waddlesplash",
	    NULL};
	about->AddCopyright(2026, "Kevin Adams", bePdfCopyrights);

	const char* origin[] = {
	    B_TRANSLATE("NimblePDF is a fork of BePDF, the long-running Haiku PDF "
	                "reader, with the original xpdf engine replaced by poppler."),
	    NULL};
	about->AddText(B_TRANSLATE("Based on BePDF"), origin);

	BString poppler;
	poppler.SetToFormat(
	    B_TRANSLATE_COMMENT("Rendering by poppler %s.", "poppler version"), POPPLER_VERSION);
	const char* engine[] = {poppler.String(), NULL};
	about->AddText(B_TRANSLATE("Rendering engine"), engine);

	const char* license[] = {
	    B_TRANSLATE("NimblePDF is free software under the GNU General Public "
	                "License, version 2 or (at your option) any later version."),
	    NULL};
	about->AddText(B_TRANSLATE("License"), license);

	// Clickable in the About window's text view.
	const char* source[] = {"https://github.com/KevinAdams05/NimblePDF", NULL};
	about->AddText(B_TRANSLATE("Source code"), source);

	about->Show();
}

/*
	open a file panel and ask for a PDF file
	the file panel will tell by itself if openning have been cancelled
	or not.
*/
void NimblePDFApplication::OpenFilePanel()
{
	if (fOpenFilePanel == NULL) {
		fOpenFilePanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, B_FILE_NODE, true, NULL, NULL);
		fOpenFilePanel->SetRefFilter(&pdfFilter);
	}
	fOpenFilePanel->SetPanelDirectory(fSettings->GetPanelDirectory());
	fReadyToQuit = true;
	fOpenFilePanel->Show();
}

/*
	open a file panel and ask for a PDF file
	the file panel will tell by itself if openning have been cancelled
	or not.
*/
void NimblePDFApplication::OpenSaveFilePanel(BHandler* handler, bool fileMode, BRefFilter* filter, BMessage* msg, const char* name)
{
	BFilePanel* panel = NULL;

	// lazy construct file panel
	if (fileMode) {
		// file panel for selection of file
		if (fSaveFilePanel == NULL) {
			fSaveFilePanel = new BFilePanel(B_SAVE_PANEL, NULL, NULL, B_FILE_NODE, false, NULL, NULL, true);
		}

		// hide other file panel
		if (fSaveToDirectoryFilePanel != NULL && fSaveToDirectoryFilePanel->IsShowing()) {
			fSaveToDirectoryFilePanel->Hide();
		}

		panel = fSaveFilePanel;
	} else {
		// file panel for selection of directory
		if (fSaveToDirectoryFilePanel == NULL) {
			fSaveToDirectoryFilePanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, B_DIRECTORY_NODE, false, NULL, NULL, true);
		}

		// hide other file panel
		if (fSaveFilePanel != NULL && fSaveFilePanel->IsShowing()) {
			fSaveFilePanel->Hide();
		}

		panel = fSaveToDirectoryFilePanel;
	}

	// (re)-set to directory of currently opened PDF file
	// TODO decide if directory should be independent from PDF file
	panel->SetPanelDirectory(fSettings->GetPanelDirectory());

	if (name != NULL) {
		panel->SetSaveText(name);
	} else if (fileMode) {
		panel->SetSaveText("");
	}

	// set/reset filter
	panel->SetRefFilter(filter);

	// add kind to message
	BMessage message(B_SAVE_REQUESTED);
	if (msg == NULL) {
		msg = &message;
	}
	panel->SetMessage(msg);

	// set target
	BMessenger msgr(handler);
	panel->SetTarget(msgr);

	panel->Refresh();

	panel->Show();
}

void NimblePDFApplication::OpenSaveFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg, const char* name)
{
	OpenSaveFilePanel(handler, true, filter, msg, name);
}

void NimblePDFApplication::OpenSaveToDirectoryFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg, const char* name)
{
	OpenSaveFilePanel(handler, false, filter, msg, name);
}


/*
  NOTIFY_QUIT_MSG:
  Or to quit all NimblePDF applications.
*/
void NimblePDFApplication::Notify(uint32 cmd)
{
	BList list;
	be_roster->GetAppList(NIMBLEPDF_APP_SIG, &list);
	const int n = list.CountItems() - 1;
	BMessage msg(cmd);
	// notify all but this team
	for (int i = n; i >= 0; i--) {
		team_id who = (team_id)(addr_t)list.ItemAt(i);
		if (who == fTeamID)
			continue; // skip own team
		status_t status;
		BMessenger app(NIMBLEPDF_APP_SIG, who, &status);
		if (status == B_OK) {
			app.SendMessage(&msg, (BHandler*)NULL, 0);
		}
	}
	// notify ourself
	PostMessage(&msg, (BHandler*)NULL, 0);
}

bool NimblePDFApplication::QuitRequested()
{
	delete fStdoutTracer;
	fStdoutTracer = NULL;
	delete fStderrTracer;
	fStderrTracer = NULL;

	bool shortcut;
	if (CurrentMessage()->FindBool("shortcut", &shortcut) == B_OK && shortcut) {
		Notify(NOTIFY_QUIT_MSG);
	}
	return BApplication::QuitRequested();
}

///////////////////////////////////////////////////////////
/*
	Opens everything.
*/
void NimblePDFApplication::RefsReceived(BMessage* msg)
{
	uint32 type;
	int32 count;
	fReadyToQuit = false;
	status_t result;

	msg->GetInfo("refs", &type, &count);

	if (type != B_REF_TYPE) {
		BAlert* error = new BAlert(B_TRANSLATE("Error"),
		    B_TRANSLATE("Invalid file reference received!"),
		    B_TRANSLATE("Close"),
		    NULL,
		    NULL,
		    B_WIDTH_AS_USUAL,
		    B_WARNING_ALERT);
		error->Go();

		return;
	}

	BString ownerPassword, userPassword;
	const char* owner = NULL;
	const char* user = NULL;
	int32 pageNum = 0;
	entry_ref ref;

	if (msg->FindString("ownerPassword", &ownerPassword) == B_OK) {
		owner = ownerPassword.String();
	}
	if (msg->FindString("userPassword", &userPassword) == B_OK) {
		user = userPassword.String();
	}
	result = msg->FindInt32(PAGE_NUM_MSG_KEY, &pageNum);
	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) {
			BAlert* error = new BAlert(B_TRANSLATE("Error"),
			    B_TRANSLATE("Error getting page number!"),
			    B_TRANSLATE("Close"),
			    NULL,
			    NULL,
			    B_WIDTH_AS_USUAL,
			    B_WARNING_ALERT);
			error->Go();
		}
	}

	Initialize();

	for (int32 i = --count; i >= 0; i--) {
		if (msg->FindRef("refs", i, &ref) == B_OK) {
			/*
				Open the document...
				WARNING: The application thread is used to open a file!
			*/
			PDFWindow* win;
			BRect rect(fSettings->GetWindowRect());
			bool ok;
			bool encrypted = false;

			if (fWindow == NULL) {
				win = new PDFWindow(&ref, rect, owner, user, &encrypted);
				ok = win->IsOk();
			} else {
				win = fWindow;
				win->Lock();
				ok = fWindow->LoadFile(&ref, owner, user, &encrypted);
				win->Unlock();
			}

			if (!ok) {
				if (!encrypted) {
					BAlert* error = new BAlert(B_TRANSLATE("Error"),
					    B_TRANSLATE("NimblePDF: Error opening file!"),
					    B_TRANSLATE("Close"),
					    NULL,
					    NULL,
					    B_WIDTH_AS_USUAL,
					    B_STOP_ALERT);
					error->Go();

					if (fWindow == NULL) { // fixme: always true even if a PDF window is already open!
						OpenFilePanel();
					}
				} else {
					new PasswordWindow(&ref, rect, this);
				}
				if (fWindow == NULL)
					delete win;

			} else {
				if (fWindow == NULL) {
					fWindow = win;
					win->Show();
				}
				// jump to page if provided -- only on a successful open, and
				// post to the window's looper rather than calling its handler
				// from this (the application) thread.
				if (pageNum != 0 && fWindow != NULL) {
					BMessage goToPageMsg(PDFWindow::GOTO_PAGE_CMD);
					goToPageMsg.AddInt32("page", pageNum);
					fWindow->PostMessage(&goToPageMsg);
				}
			}
			// stop after first document
			fGotSomething = true;
			break;
		}
	}
}


///////////////////////////////////////////////////////////
void NimblePDFApplication::MessageReceived(BMessage* msg)
{
	if (msg == NULL) {
		Trace(LOG_DEBUG, "xpdf: message NULL received\n");
		return;
	}

	switch (msg->what) {
	case NOTIFY_QUIT_MSG:
		if (fWindow) {
			BWindow* w = fWindow;
			w->Lock();
			w->PostMessage(B_QUIT_REQUESTED);
			w->Unlock();
		}
		break;
	case NOTIFY_CLOSE_MSG:
		if (fWindow) {
			fWindow->Lock();
			fWindow->UpdateWindowsMenu();
			fWindow->Unlock();
		}
		break;
	case B_CANCEL:
		if (!fWindow && fReadyToQuit) {
			PostMessage(B_QUIT_REQUESTED);
		}
		break;
	default:
		BApplication::MessageReceived(msg);
	}
}


///////////////////////////////////////////////////////////
void NimblePDFApplication::ArgvReceived(int32 argc, char** argv)
{
	// TODO(poppler-migration, phase G): xpdf provided parseArgs() with
	// ArgDesc tables for flag-style command-line parsing (e.g. -h, -v).
	// Reimplement that against a Haiku-friendly option parser or just
	// drop the flag handling — the GUI launch path doesn't need it.
	// For Phase A we accept argv[1] as the PDF path and argv[2] (if
	// given) as the page number; no flags.
	int pg = 1;
	entry_ref fileToOpen;

	if (!(argc == 2 || argc == 3)) {
		Trace(LOG_ERR, "usage: %s <pdf-path> [<page>]", argv[0]);
		be_app->PostMessage(B_QUIT_REQUESTED);
		return;
	}
	if (argc == 3)
		pg = atoi(argv[2]);

	BMessage msg(B_REFS_RECEIVED);
	msg.AddInt32(PAGE_NUM_MSG_KEY, pg);
	if (get_ref_for_path(argv[1], &fileToOpen) != B_OK) {
		Trace(LOG_ERR, "NimblePDF: cannot resolve path: %s", argv[1]);
		be_app->PostMessage(B_QUIT_REQUESTED);
		return;
	}
	msg.AddRef("refs", &fileToOpen);
	PostMessage(&msg);
	fGotSomething = true;
}


///////////////////////////////////////////////////////////
void NimblePDFApplication::LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK && path.Append(settingsFilename) == B_OK) {
		fSettings->Load(path.Path());
	}
}

///////////////////////////////////////////////////////////
void NimblePDFApplication::SaveSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK && path.Append(settingsFilename) == B_OK) {
		fSettings->Save(path.Path());
	}
}

static struct {
	const char* name;
	const char* public_name;
	const char* pdf_name;
	int32 type_code;
} gAttrInfo[] = {{"META:subject", "Subject", "Subject", B_STRING_TYPE},
    {"META:title", "Title", "Title", B_STRING_TYPE},
    {"META:creator", "Creator", "Creator", B_STRING_TYPE},
    {"META:author", "Author", "Author", B_STRING_TYPE},
    {"META:keyw", "Keywords", "Keywords", B_STRING_TYPE},
    {"PDF:producer", "Producer", "Producer", B_STRING_TYPE},
    {"PDF:created", "Created", "CreationDate", B_TIME_TYPE},
    {"PDF:modified", "Modified", "ModDate", B_TIME_TYPE},
    {"META:pages", "Pages", NULL, B_INT32_TYPE},
    {"PDF:version", "Version", NULL, B_DOUBLE_TYPE},
    {"PDF:linearized", "Linearized", NULL, B_BOOL_TYPE},
    {NULL, NULL, NULL, 0}};

///////////////////////////////////////////////////////////
void NimblePDFApplication::UpdateAttr(BNode& node, const char* name, type_code type, off_t offset, void* buffer, size_t length)
{
	char dummy[10];
	if (B_ENTRY_NOT_FOUND == node.ReadAttr(name, type, offset, (char*)dummy, sizeof(dummy))) {
		node.WriteAttr(name, type, offset, buffer, length);
	}
}


///////////////////////////////////////////////////////////
void NimblePDFApplication::UpdateFileAttributes(PDFDoc* doc, entry_ref* ref)
{
	BNode node(ref);
	if (node.InitCheck() != B_OK)
		return;

	const bool force_overwrite = (modifiers() & B_COMMAND_KEY) == B_COMMAND_KEY;

	if (force_overwrite) {
		for (int i = 0; gAttrInfo[i].name; i++) {
			node.RemoveAttr(gAttrInfo[i].name);
		}
	}

	int32 pages = (int32)doc->getNumPages();
	UpdateAttr(node, "META:pages", B_INT32_TYPE, 0, &pages, sizeof(int32));
	bool b = doc->isLinearized();
	UpdateAttr(node, "PDF:linearized", B_BOOL_TYPE, 0, &b, sizeof(b));
	double d = doc->getPDFMajorVersion() + doc->getPDFMinorVersion() / 10.0;
	UpdateAttr(node, "PDF:version", B_DOUBLE_TYPE, 0, &d, sizeof(d));

	Object obj = doc->getDocInfo();
	if (obj.isDict()) {
		Dict* dict = obj.getDict();
		for (int i = 0; gAttrInfo[i].name; i++) {
			time_t time;
			if (gAttrInfo[i].pdf_name == NULL)
				continue;
			BString* s = FileInfoWindow::GetProperty(dict, gAttrInfo[i].pdf_name, &time);
			if (s) {
				if (gAttrInfo[i].type_code == B_TIME_TYPE) {
					if (time != 0) {
						UpdateAttr(node, gAttrInfo[i].name, B_TIME_TYPE, 0, &time, sizeof(time));
					}
				} else {
					UpdateAttr(node, gAttrInfo[i].name, B_STRING_TYPE, 0, (void*)s->String(), s->Length() + 1);
				}
				delete s;
			}
		}
	}
}


const char* NimblePDFApplication::GetVersion(BString& version)
{
	version = "?.?.?";
	if (be_app == NULL) {
		return version.String();
	}

	app_info info;
	if (be_app->GetAppInfo(&info) != B_OK) {
		return version.String();
	}

	BFile file(&info.ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		return version.String();
	}

	BAppFileInfo appFileInfo(&file);
	version_info appVersion;
	if (appFileInfo.GetVersionInfo(&appVersion, B_APP_VERSION_KIND) != B_OK) {
		return version.String();
	}

	BString variety = B_TRANSLATE("Unknown");
	switch (appVersion.variety) {
	case 0:
		variety = B_TRANSLATE("Development");
		break;
	case 1:
		variety = B_TRANSLATE("Alpha");
		break;
	case 2:
		variety = B_TRANSLATE("Beta");
		break;
	case 3:
		variety = B_TRANSLATE("Gamma");
		break;
	case 4:
		variety = B_TRANSLATE("Golden Master");
		break;
	case 5:
		if (appVersion.internal == 0) {
			// hide variety
			variety = "";
		} else {
			variety = B_TRANSLATE("Final");
		}
		break;
	};
	version = "";
	version << appVersion.major << "." << appVersion.middle << "." << appVersion.minor << " " << variety;
	if (appVersion.internal != 0) {
		version << " " << appVersion.internal;
	}
	return version.String();
}
