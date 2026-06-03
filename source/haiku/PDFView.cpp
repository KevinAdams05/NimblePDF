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

#include <stdio.h>
#include <math.h>
#include <memory>
#include <optional>
// BeOS
#include <locale/Catalog.h>

#include <Application.h>
#include <Clipboard.h>
#include <Looper.h>
#include <MessageQueue.h>
#include <Roster.h>

#include <ScrollBar.h>
#include <PrintJob.h>
#include <Alert.h>
#include <StringView.h>
#include <PopUpMenu.h>
#include <MenuItem.h>

#include <Path.h>
#include <Entry.h>
#include <Directory.h>
#include <File.h>
#include <NodeInfo.h>
#include <BitmapStream.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>
#include <String.h>
#include <Debug.h>

// xpdf
#include <TextOutputDev.h>
#include <Gfx.h>
// BePDF
#include "Logging.h"
#include "AnnotationWindow.h"
#include "AnnotWriter.h"
#include "BePDF.h"
#include "BepdfApplication.h"
#include "CachedPage.h"
#include "FileInfoWindow.h"
#include "FindTextWindow.h"
#include "PageRenderer.h"
#include "PDFWindow.h"
#include "PDFView.h"
#include "PrintingProgressWindow.h"
#include "ResourceLoader.h"
#include "SaveThread.h"
#include "StatusWindow.h"
#include "TextConversion.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFView"

// zoom factor is 1.2 (similar to DVI magsteps)
static const int kZoomDPI[MAX_ZOOM - MIN_ZOOM + 1] = {18, 24, 36, 48, 54, 72, 90, 108, 127, 144, 216};

#define OPEN_FILE_MSG 'open'
#define COPY_LINK_MSG 'cplk'
#define DELETE_ANNOT_MSG 'dele'
#define PROPERTIES_ANNOT_MSG 'prp'
#define EDIT_ANNOT_MSG 'edit'
#define SAVE_FILE_ATTACHMENT_ANNOT_MSG 'save'


// Poppler 23.12 dropped these path helpers from goo/gfile.h.
// Lightweight POSIX-only replacements local to this file.
static bool isAbsolutePath(const char* path)
{
	return path != NULL && path[0] == '/';
}

static GooString* grabPath(const char* path)
{
	if (path == NULL)
		return new GooString("");
	const char* lastSlash = strrchr(path, '/');
	if (lastSlash == NULL)
		return new GooString("");
	return new GooString(path, lastSlash - path);
}


///////////////////////////////////////////////////////////////////////////
PDFView::PDFView(entry_ref* ref,
    FileAttributes* fileAttributes,
    const char* name,
    uint32 flags,
    const char* ownerPassword,
    const char* userPassword,
    bool* encrypted)
    : BView(name, flags)
{
	GlobalSettings* settings = gApp->GetSettings();
	SetViewColor(B_TRANSPARENT_COLOR);
	// init member variables
	fDoc = NULL;
	fBePDFAcroForm = NULL;
	fOk = false;
	fZoom = settings->GetZoom();
	fBitmap = NULL;
	fPage = new CachedPage();
	fCurrentPage = 0;
	fRotation = settings->GetRotation(); // 0.0f;
	fOwnerPassword = fUserPassword = NULL;
	SetPassword(ownerPassword, userPassword);

	fColorSpace = B_RGB32;

	fInvertVerticalScrolling = settings->GetInvertVerticalScrolling();

	fTitle = NULL;
	fLeft = fTop = 0;
	fWidth = 100;
	fHeight = 100;
	fLinkAction = NULL;
	fAnnotation = NULL;
	fAnnotInEditor = NULL;
	fNavigationState = kNotInHistory;

	fViewCursor = NULL;
	fMouseAction = NO_ACTION;
	fMousePosition.Set(0, 0);
	fDragStarted = false;
	fEditAnnot = false;
	fInsertAnnot = NULL;

	fMouseWheelDY = 0;

	fRendererID = -1;
	fRendering = false;

	fSelected = NOT_SELECTED;
	fFilledSelection = settings->GetFilledSelection();

	fPrintSettings = NULL;

#if JAPANESE_SUPPORT
	SetJapaneseFont(settings->GetJapaneseFontFamily(), settings->GetJapaneseFontStyle());
#endif

#if CHINESE_CNS_SUPPORT
	SetChineseTFont(settings->GetChineseTFontFamily(), settings->GetChineseTFontStyle());
#endif

#if CHINESE_GB_SUPPORT
	SetChineseSFont(settings->GetChineseSFontFamily(), settings->GetChineseSFontStyle());
#endif

#if KOREAN_SUPPORT
	SetKoreanFont(settings->GetKoreanFontFamily(), settings->GetKoreanFontStyle());
#endif

	fPageRenderer.SetPassword(fOwnerPassword, fUserPassword);

	if (LoadFile(ref, fileAttributes, ownerPassword, userPassword, true, encrypted)) {
		SetViewCursor(gApp->handCursor, true);
		fOk = true;
	}
}

PDFWindow* PDFView::GetPDFWindow()
{
	return dynamic_cast<PDFWindow*>(Window());
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetPassword(const char* ownerPassword, const char* userPassword)
{
	delete fOwnerPassword;
	fOwnerPassword = ownerPassword ? new BString(ownerPassword) : NULL;
	delete fUserPassword;
	fUserPassword = userPassword ? new BString(userPassword) : NULL;
}

///////////////////////////////////////////////////////////////////////////
GooString* PDFView::ConvertPassword(const char* password)
{
	GooString* pwd = NULL;
	if (password != NULL) {
		BString* s = ToAscii(password);
		if (s) {
			pwd = new GooString(s->String());
			delete s;
		}
	}
	return pwd;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::EndDoc()
{
	fSelected = NOT_SELECTED;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::UpdatePanelDirectory(BPath* path)
{
	BPath directory;
	if (strcmp(path->Path(), gApp->DefaultPDF()->Path()) != 0 && path->GetParent(&directory) == B_OK) {
		// don't set path to default pdf file
		gApp->GetSettings()->SetPanelDirectory(directory.Path());
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::MakeTitleString(BPath* path)
{
	delete fTitle;
	fTitle = new BString("BePDF: ");

	Object obj = fDoc->getDocInfo();
	if (obj.isDict()) {
		Dict* dict = obj.getDict();
		BString* s = FileInfoWindow::GetProperty(dict, FileInfoWindow::titleKey);
		if (s) {
			*fTitle << *s << " (" << path->Leaf() << ")";
			delete s;
		} else
			*fTitle << path->Leaf();
	} else
		*fTitle << path->Leaf();
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::OpenFile(entry_ref* ref, const char* ownerPassword, const char* userPassword, bool* encrypted)
{
	BEntry entry(ref, true);
	if (!entry.Exists()) {
		return false;
	}
	BPath path;
	entry.GetPath(&path);

	auto fileName = std::make_unique<GooString>(path.Path());
	GooString* ownerStr = ConvertPassword(ownerPassword);
	GooString* userStr = ConvertPassword(userPassword);
	std::optional<GooString> owner;
	std::optional<GooString> user;
	if (ownerStr != NULL) {
		// GooString's copy ctor is deleted in poppler 23.12, so construct
		// the optional's value in place from the C-string contents.
		owner.emplace(ownerStr->c_str(), ownerStr->size());
		delete ownerStr;
	}
	if (userStr != NULL) {
		user.emplace(userStr->c_str(), userStr->size());
		delete userStr;
	}

	PDFDoc* newDoc = new PDFDoc(std::move(fileName), owner, user, nullptr);

	UpdatePanelDirectory(&path);

	bool ok = newDoc->isOk();
	*encrypted = newDoc->isEncrypted();

	if (ok) {
		delete fDoc;
		fDoc = newDoc;
		MakeTitleString(&path);
	} else {
		delete newDoc;
	}
	return ok;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::LoadFileSettings(entry_ref* ref, FileAttributes* fileAttributes, float& left, float& top)
{
	GlobalSettings* s = gApp->GetSettings();
	if (fileAttributes->Read(ref, s) && s->GetRestorePageNumber()) {
		fCurrentPage = fileAttributes->GetPage();
		if (fCurrentPage > fDoc->getNumPages()) {
			fCurrentPage = fDoc->getNumPages();
		}
		fZoom = s->GetZoom();
		fRotation = s->GetRotation();
		fileAttributes->GetLeftTop(left, top);
	} else {
		left = top = 0;
		fCurrentPage = 1;
		fileAttributes->SetPage(fCurrentPage);
		fileAttributes->SetLeftTop(left, top);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::RestoreWindowFrame(BWindow* w)
{
	GlobalSettings* s = gApp->GetSettings();
	if (s->GetRestoreWindowFrame()) {
		// restore window position and size
		w->MoveTo(s->GetWindowPosition());
		float width, height;
		s->GetWindowSize(width, height);
		w->ResizeTo(width, height);
	}
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::LoadFile(
    entry_ref* ref, FileAttributes* fileAttributes, const char* ownerPassword, const char* userPassword, bool init, bool* encrypted)
{
	BString s(B_TRANSLATE("BePDF reading file: "));
	s += ref->name;
	ShowLoadProgressStatusWindow statusWindow(s.String());
	EndDoc();

	SetPassword(ownerPassword, userPassword);
	fPageRenderer.SetPassword(fOwnerPassword, fUserPassword);
	WaitForPage(true);

	// We use the application thread to load a PDF file.
	// To keep the window responsive while loading, we unlock the window lock
	// and have to ensure that the window thread does not access data
	// that is being loaded (Draw() just fills the entire view with a background color).
	fLoading = true;
	bool isLocked = Window()->IsLocked();
	if (isLocked) {
		Invalidate();
		Window()->Unlock();
	}
	bool opened = OpenFile(ref, ownerPassword, userPassword, encrypted);
	if (isLocked)
		Window()->Lock();
	fLoading = false;
	fPageRenderer.StartDoc(fColorSpace);
	if (!opened) {
		// show previous document
		if (Window()->Lock()) {
			Invalidate();
			Window()->Unlock();
		}
		return false;
	}
	delete fBePDFAcroForm;
	fBePDFAcroForm = new BePDFAcroForm(fDoc->getXRef(), fDoc->getCatalog()->getAcroForm());
	fPageRenderer.SetDoc(fDoc, fBePDFAcroForm);
	BepdfApplication::UpdateFileAttributes(fDoc, ref);

	float left, top;
	LoadFileSettings(ref, fileAttributes, left, top);

	RecordHistory(*ref, ownerPassword, userPassword);

	PDFWindow* w = GetPDFWindow();
	if (w && !init && w->Lock()) {
		RestoreWindowFrame(w);
		w->NewDoc(fDoc);
		w->SetTitle(fTitle->String());
		Redraw();
		ScrollTo(left, top);
		w->Unlock();
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////
PDFView::~PDFView()
{
	delete fDoc;
	delete fTitle;
	delete fPage;
	delete fOwnerPassword;
	delete fUserPassword;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::MessageReceived(BMessage* msg)
{
	BString string;
	switch (msg->what) {
	case B_SIMPLE_DATA: {
		entry_ref ref;
		if (msg->FindRef("refs", 0, &ref) == B_OK) {
			be_app->RefsReceived(msg);
			return;
		}
	} break;
	case B_COPY_TARGET:
		SendDataMessage(msg);
		break;
	case B_MOUSE_WHEEL_CHANGED:
		OnMouseWheelChanged(msg);
		break;
	case COPY_LINK_MSG:
		if (msg->FindString("link", &string) == B_OK) {
			CopyText(&string);
		}
		break;
	case OPEN_FILE_MSG:
		if (msg->FindString("file", &string) == B_OK) {
			PDFWindow::Launch(string.String());
		}
		break;
	case DELETE_ANNOT_MSG: {
		void* p;
		if (msg->FindPointer("annot", &p) == B_OK && p == fAnnotation) {
			if (p == fAnnotInEditor)
				fAnnotInEditor = NULL;
			fAnnotation->SetDeleted(true);
			Invalidate(CvtUserToDev(fAnnotation->GetRect()));
			ClearAnnotationWindow();
		}
		fAnnotation = NULL;
	} break;
	case EDIT_ANNOT_MSG: {
		PDFWindow* win = GetPDFWindow();
		if (win) {
			win->EditAnnotation(!fEditAnnot);
		}
	} break;
	case SAVE_FILE_ATTACHMENT_ANNOT_MSG: {
		PDFWindow* win = GetPDFWindow();
		if (win == NULL) {
			break;
		}

		FileAttachmentAnnot* fileAttachment = dynamic_cast<FileAttachmentAnnot*>(fAnnotation);
		if (fileAttachment == NULL) {
			break;
		}

		BMessage msg(B_SAVE_REQUESTED);
		msg.AddPointer("fileAttachment", fileAttachment);
		gApp->OpenSaveFilePanel(this, NULL, &msg, fileAttachment->GetFileName());
	} break;
	case PROPERTIES_ANNOT_MSG:
		ShowAnnotWindow(true);
		break;
	case B_SAVE_REQUESTED:
		SaveFileAttachment(msg);
		break;
	default:
		BView::MessageReceived(msg);
	}
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::InPage(BPoint p)
{
	return p.x >= 0.0 && p.x < fWidth && p.y >= 0.0 && p.y < fHeight;
}

///////////////////////////////////////////////////////////////////////////
BPoint PDFView::LimitToPage(BPoint p)
{
	if (p.x < 0)
		p.x = 0.0;
	else if (p.x > fWidth)
		p.x = fWidth;

	if (p.y < 0)
		p.y = 0.0;
	else if (p.y > fHeight)
		p.y = fHeight;
	return p;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::OnMouseWheelChanged(BMessage* msg)
{
	float dy, dx;
	if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK && dy != 0.0) {
		bool down = dy > 0;
		// intelliMouse driver uses command key to simulate wheel_detla_x!
		if ((modifiers() & (B_COMMAND_KEY | B_OPTION_KEY))) {
			Zoom(!down); // zoom in / out
		} else if ((modifiers() & (B_SHIFT_KEY | B_CONTROL_KEY))) {
			// next/previous page
			MoveToPage(fCurrentPage + (down ? 1 : -1));
		} else {
			ScrollVertical(down, 0.20);
		}
	}
	if (msg->FindFloat("be:wheel_delta_x", &dx) == B_OK && dx != 0.0) {
		bool right = dx > 0;
		ScrollHorizontal(right, 0.20);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::DrawAnnotations(BRect updateRect)
{
	if (!fRendering && fEditAnnot) {
		SetOrigin(fLeft, fTop);
		fPageRenderer.DrawAnnotations(this, fEditAnnot);
		SetOrigin(0, 0);
	}
}


///////////////////////////////////////////////////////////////////////////
void PDFView::DrawPage(BRect updateRect)
{
	if (fBitmap == NULL) {
#ifdef DEBUG
		Trace(LOG_WARNING, "PDFView::Draw() called with NULL bitmap");
#endif
	} else {
		DrawBitmap(fBitmap, BRect(0, 0, fWidth, fHeight), BRect(fLeft, fTop, fLeft + fWidth, fTop + fHeight));
		DrawAnnotations(updateRect);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::DrawBackground(BRect updateRect)
{
	BRect rect(Bounds());
	float right = fLeft + fWidth, bottom = fTop + fHeight;
	SetLowColor(128, 128, 128, 0);
	if (rect.left < fLeft) {
		FillRect(BRect(rect.left, rect.top, fLeft - 2, rect.bottom), B_SOLID_LOW);
	}
	if (rect.top < fTop) {
		FillRect(BRect(rect.left, rect.top, rect.right, fTop - 2), B_SOLID_LOW);
	}
	if (right < rect.right) {
		FillRect(BRect(right + 2, rect.top, rect.right, rect.bottom), B_SOLID_LOW);
	}
	if (bottom < rect.bottom) {
		FillRect(BRect(rect.left, bottom + 2, rect.right, rect.bottom), B_SOLID_LOW);
	}

	SetLowColor(0, 0, 0, 0);
	StrokeRect(BRect(fLeft - 1, fTop - 1, right + 1, bottom + 1), B_SOLID_LOW);
}

///////////////////////////////////////////////////////////////////////////
void PDFView::DrawSelection(BRect updateRect)
{
	BRect selection(fSelection);
	selection.OffsetBy(fLeft, fTop);

	rgb_color fill_color = {0, 0, 255, 64}; // transparent blue
	SetHighColor(fill_color);               // fill color for selection
	SetPenSize(1.0);

	switch (fSelected) {
	case DO_SELECTION:
		StrokeRect(selection);
		break;
	case SELECTED:
		SetDrawingMode(B_OP_ALPHA);
		if (fFilledSelection) {
			FillRect(selection);
		} else {
			StrokeRect(selection);
		}
		SetDrawingMode(B_OP_COPY);
		break;
	default:
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::Draw(BRect updateRect)
{
	if (fLoading) {
		SetLowColor(128, 128, 128, 0);
		FillRect(updateRect, B_SOLID_LOW);
	} else {
		DrawBackground(updateRect);
		DrawPage(updateRect);
		BRect rect(Bounds());
		if (GetPDFWindow()) {
			GetPDFWindow()->GetFileAttributes()->SetLeftTop(rect.left, rect.top);
		}
		DrawSelection(updateRect);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::ScrollTo(BPoint point)
{
	BView::ScrollTo(point);
	BPoint mouse;
	uint32 buttons;
	GetMouse(&mouse, &buttons);
	DisplayLink(mouse);
}

void PDFView::ScrollTo(float x, float y)
{
	BRect bounds(Bounds());
	float xMax = fWidth - bounds.Width();
	float yMax = fHeight - bounds.Height();

	if ((x < 0) || (fLeft > 0))
		x = 0;
	else if ((xMax > 0) && (x > xMax))
		x = xMax;

	if ((y < 0) || (fTop > 0))
		y = 0;
	else if ((yMax > 0) && (y > yMax))
		y = yMax;

	BView::ScrollTo(x, y);
}

///////////////////////////////////////////////////////////////////////////
void PDFView::FrameResized(float width, float height)
{
	Resize();
}


///////////////////////////////////////////////////////////////////////////
void PDFView::AttachedToWindow()
{
	Window()->SetTitle(fTitle->String());
	SetViewCursor(gApp->handCursor);
	fPageRenderer.SetListener(Window(), this);
}

///////////////////////////////////////////////////////////////////////////
void PDFView::ScrollVertical(bool down, float by)
{
	BRect rect(Bounds());
	float scrollBy = (by > 0) ? rect.Height() * by : -by;
	if (down) {
		if (rect.bottom < fHeight - 1) {
			ScrollBy(0, scrollBy);
		} else {
			if (fCurrentPage != fDoc->getNumPages()) { // bottom of last page not reached
				MoveToPage(fCurrentPage + 1, true);
			}
		}
	} else { // up
		if (rect.top != 0) {
			ScrollBy(0, -scrollBy);
		} else {
			if (fCurrentPage != 1) { // top of first page not reached
				MoveToPage(fCurrentPage - 1, false);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::ScrollHorizontal(bool right, float by)
{
	BRect rect(Bounds());
	float scrollBy = (by > 0) ? rect.Width() * by : -by;
	if (right) {
		if (rect.right < fWidth - 1) {
			ScrollBy(scrollBy, 0);
		}
	} else {
		if (rect.left != 0) {
			ScrollBy(-scrollBy, 0);
		}
	}
}
///////////////////////////////////////////////////////////////////////////
void PDFView::KeyDown(const char* bytes, int32 numBytes)
{
	switch (*bytes) {
	case B_PAGE_UP:
		MoveToPage(fCurrentPage - 1);
		break;
	case B_SPACE:
	case B_ENTER:
	case B_BACKSPACE:
		ScrollVertical(*bytes != B_BACKSPACE, 0.95);
		break;
	case B_DOWN_ARROW:
	case B_UP_ARROW:
		ScrollVertical(*bytes == B_DOWN_ARROW, -20);
		break;
	case B_LEFT_ARROW:
	case B_RIGHT_ARROW:
		ScrollHorizontal(*bytes == B_RIGHT_ARROW, -20);
		break;
	case B_PAGE_DOWN:
		MoveToPage(fCurrentPage + 1);
		break;
	case B_HOME:
		MoveToPage(1);
		break;
	case B_END:
		MoveToPage(GetNumPages());
		break;
	default:
		BView::KeyDown(bytes, numBytes);
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetAction(mouse_action action)
{
	fMouseAction = action;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetViewCursor(BCursor* cursor, bool sync)
{
	if (Window()->Lock()) {
		fViewCursor = cursor;
		BView::SetViewCursor(cursor, sync);
		Window()->Unlock();
	}
}

///////////////////////////////////////////////////////////////////////////
BPoint PDFView::CorrectMousePos(const BPoint point)
{
	BPoint p(point);
	p.x -= fLeft;
	p.y -= fTop;
	return p;
}

///////////////////////////////////////////////////////////////////////////
PDFPoint PDFView::CvtDevToUser(BPoint dev)
{
	double x, y;
	fPage->CvtDevToUser((int)dev.x, (int)dev.y, &x, &y);
	return PDFPoint(x, y);
}

///////////////////////////////////////////////////////////////////////////
BPoint PDFView::CvtUserToDev(PDFPoint user)
{
	int x, y;
	fPage->CvtUserToDev(user.x, user.y, &x, &y);
	return BPoint(x, y);
}

///////////////////////////////////////////////////////////////////////////
BRect PDFView::CvtUserToDev(PDFRectangle* user)
{
	// Note: Keep in sync with AnnotationRenderer::ToRect()
	BRect r;
	int x, y;
	fPage->CvtUserToDev(user->x1, user->y1, &x, &y);
	r.top = r.bottom = y;
	r.right = r.left = x;
	fPage->CvtUserToDev(user->x2, user->y2, &x, &y);
	if (y < r.top)
		r.top = y;
	else
		r.bottom = y;
	if (x < r.left)
		r.left = x;
	else
		r.right = x;
	r.top = floor(r.top);
	r.left = floor(r.left);
	r.bottom = ceil(r.bottom + 1.0);
	r.right = ceil(r.right + 1.0);
	// The next line exists not in AnnotationRenderer::ToRect():
	r.OffsetBy(fLeft, fTop);
	return r;
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::OnAnnotResizeRect(BPoint point, bool& vertOnly)
{
	BRect r = CvtUserToDev(fAnnotation->GetRect());
	r.left = r.right - 5;
	if (r.Contains(point)) {
		r.top = r.bottom - 5;
		vertOnly = !r.Contains(point);
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::OnAnnotation(BPoint point)
{
	if (fRendering)
		return false;

	BPoint p = CorrectMousePos(point);
	PDFPoint u = CvtDevToUser(p);
	Annotation* annot = fPage->GetAnnotations()->OverAnnotation(u.x, u.y, fEditAnnot);
	// XXX maybe allow deleting the annotation even if we can't write one
	if (!CanWrite(annot))
		annot = NULL;
	fAnnotation = annot;
	if (fAnnotation) {
		bool vertOnly;
		if (OnAnnotResizeRect(point, vertOnly)) {
			if (vertOnly) {
				SetViewCursor(gApp->splitVCursor);
			} else {
				SetViewCursor(gApp->resizeCursor);
			}
		} else {
			SetViewCursor(gApp->pointerCursor);
		}
	} else {
		SetViewCursor(gApp->handCursor);
	}
	return fAnnotation != NULL;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::CurrentDate(BString& date)
{
	GooString s;
	AnnotUtils::CurrentDate(&s);
	date = s.c_str();
}

///////////////////////////////////////////////////////////////////////////
void PDFView::InsertAnnotation(BPoint where, bool* hasFixedSize)
{
	BString date;
	CurrentDate(date);

	if (GetPDFWindow()) {
		GetPDFWindow()->ReleaseAnnotationButton();
	}

	fAnnotation = fInsertAnnot->Clone();
	fPage->GetAnnotations()->Append(fAnnotation);

	if (fAnnotation->GetRect()->x1 == -1) {
		fAnnotation->GetRect()->x1 = 0;
	} else {
		*hasFixedSize = true;
	}

	fAnnotation->MoveTo(CvtDevToUser(CorrectMousePos(where)));
	GooString* t = Utf8ToUcs2(gApp->GetSettings()->GetAuthor());
	fAnnotation->SetTitle(t);
	delete t;
	fAnnotation->SetDate(date.String());

	PopupAnnot* popup = fAnnotation->GetPopup();
	if (popup) {
		popup->MoveTo(CvtDevToUser(CorrectMousePos(where)));
		// inherited from parent annotation:
		//popup->SetTitle(gApp->GetSettings()->GetAuthor());
		//popup->SetDate(date.String());
	}

	if (!*hasFixedSize) {
		SetViewCursor(gApp->resizeCursor);
	}
	SyncAnnotation(false);
	Invalidate(CvtUserToDev(fAnnotation->GetRect()));
	fInsertAnnot = NULL;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::AnnotMoveOrResize(BPoint point, bool annotInserted, bool fixedSize)
{
	SetAction(MOVE_ANNOT_ACTION);
	SyncAnnotation(false);
	fAnnotStartRect = *fAnnotation->GetRect();
	fDragStarted = true;
	SetMouseEventMask(B_POINTER_EVENTS);
	fResizeVertOnly = false;
	if (fixedSize || (!annotInserted && !OnAnnotResizeRect(point, fResizeVertOnly))) {
		// move
		PDFPoint p = CvtDevToUser(CorrectMousePos(point)) - fAnnotation->LeftTop();
		if (fixedSize) {
			float dx = (fAnnotation->GetRect()->x2 - fAnnotation->GetRect()->x1) / 2;
			float dy = (fAnnotation->GetRect()->y1 - fAnnotation->GetRect()->y2) / 2;
			p.x = dx;
			p.y = dy;
		}
		fMousePosition.Set(p.x, p.y);
	} else {
		// resize
		if (annotInserted) {
			// move point over resize rectangle
			BRect rect = CvtUserToDev(fAnnotation->GetRect());
			point.Set(rect.right, rect.bottom);
		}
		SetAction(RESIZE_ANNOT_ACTION);
		fMousePosition = point;
	}
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::AnnotMouseDown(BPoint point, uint32 buttons)
{
	BPoint screen = ConvertToScreen(point);
	switch (buttons) {
	case B_PRIMARY_MOUSE_BUTTON:
		if (!fEditAnnot)
			return false;
		// move, resize or insert annotation
		{
			bool annotInserted = false;
			bool fixedSize = false;
			if (fInsertAnnot) {
				InsertAnnotation(point, &fixedSize);
				annotInserted = true;
			}
			ShowAnnotWindow(true, true);
			if (annotInserted || OnAnnotation(point)) {
				AnnotMoveOrResize(point, annotInserted, fixedSize);
				return true;
			} else {
				SetAction(NO_ACTION);
				return false;
			}
		}
		break;
	case B_SECONDARY_MOUSE_BUTTON:
		// context menu, copy is not implemented in annotation editing mode
		if (fAnnotation) {
			ShowAnnotPopUpMenu(screen);
			return true;
		}
		// don't allow copy in annotation editing mode
		if (fEditAnnot) {
			return true;
		}
		break;
	default:;
	}
	return fInsertAnnot != NULL;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::MoveAnnotation(BPoint point)
{
	PDFPoint p = CvtDevToUser(CorrectMousePos(point)) - PDFPoint(fMousePosition.x, fMousePosition.y);
	if (!(fAnnotation->LeftTop() == p)) {
		BRect oldRect = CvtUserToDev(fAnnotation->GetRect());
		fAnnotation->MoveTo(PDFPoint(p.x, p.y));
		fAnnotation->SetChanged(true);
		BRect newRect = CvtUserToDev(fAnnotation->GetRect());
		BRect invRect = oldRect | newRect;
		Invalidate(invRect);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::ResizeAnnotation(BPoint point)
{
	fInsertAnnot = NULL;
	if (fResizeVertOnly)
		point.y = fMousePosition.y;
	PDFPoint delta = CvtDevToUser(fMousePosition - point);
	PDFPoint origin = CvtDevToUser(BPoint(0.0, 0.0));
	delta = origin - delta;
	PDFPoint end(delta.x + fAnnotStartRect.x2, delta.y + fAnnotStartRect.y1);
	BRect oldRect = CvtUserToDev(fAnnotation->GetRect());
	PDFPoint size(end.x - fAnnotStartRect.x1, fAnnotStartRect.y2 - end.y);
	size.x = max_c(size.x, 8);
	size.y = max_c(size.y, 8);
	fAnnotation->ResizeTo(size.x, size.y);
	fAnnotation->SetChanged(true);
	BRect newRect = CvtUserToDev(fAnnotation->GetRect());
	BRect invRect = oldRect | newRect;
	if (oldRect != newRect)
		Invalidate(invRect);
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::AnnotMouseMoved(BPoint point, uint32 transit, const BMessage* msg)
{
	if (fEditAnnot && fMouseAction == NO_ACTION) {
		if (fInsertAnnot == NULL)
			OnAnnotation(point);
		return true;
	} else if (fInsertAnnot != NULL) {
		return true;
	} else {
		return false;
	}
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::AnnotMouseUp(BPoint point)
{
	bool consumed;
	consumed = AnnotMouseMoved(point, 0, NULL);
	if (consumed) {
		SetAction(NO_ACTION);
	}
	return consumed;
}

///////////////////////////////////////////////////////////////////////////
uint32 PDFView::GetButtons()
{
	BPoint point;
	uint32 buttons;
	GetMouse(&point, &buttons, false);
	if (buttons == B_PRIMARY_MOUSE_BUTTON) {
		if ((modifiers() & B_CONTROL_KEY)) {
			buttons = B_SECONDARY_MOUSE_BUTTON; // simulate secondary button
		} else if ((modifiers() & B_SHIFT_KEY)) {
			buttons = B_TERTIARY_MOUSE_BUTTON; // simulate tertiary button
		}
	}
	return buttons;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::MouseDown(BPoint point)
{
	LinkAction* action;
	BPoint screen;

	MakeFocus(true);
	uint32 buttons = GetButtons();
	screen = ConvertToScreen(point);
	if (AnnotMouseDown(point, buttons)) {
		return;
	}
	switch (buttons) {
	case B_PRIMARY_MOUSE_BUTTON:
		if ((fSelected == SELECTED) && fSelection.Contains(CorrectMousePos(point))) {
			SendDragMessage(B_MIME_DATA); // start text drag and drop
			break;
		}
		// follow link or move view
		SetAction(MOVE_ACTION);
		if (!HandleLink(point)) {
			fDragStarted = true;
			SetMouseEventMask(B_POINTER_EVENTS);
			SetViewCursor(gApp->grabCursor);
			fMousePosition = ConvertToScreen(point);
		} else {
			SetAction(NO_ACTION);
		}
		break;
	case B_SECONDARY_MOUSE_BUTTON:
		if ((fSelected == SELECTED) && fSelection.Contains(CorrectMousePos(point))) {
			SendDragMessage(B_SIMPLE_DATA); // start negotiated drag and drop
			return;
		}
		if (fSelected == NOT_SELECTED) {
			if ((action = OnLink(point)) != NULL) {
				ShowPopUpMenu(screen, action);
				return;
			}
		}
		// text selection: fall through
	case B_TERTIARY_MOUSE_BUTTON: // zoom to selection
		if (fSelected != NOT_SELECTED) {
			fSelection.OffsetBy(fLeft, fTop);
			Invalidate(fSelection);
			fSelected = NOT_SELECTED;
		}

		// is copying allowed?
		if (buttons == B_SECONDARY_MOUSE_BUTTON && !fDoc->okToCopy()) {
			return;
		}

		SetAction(buttons == B_TERTIARY_MOUSE_BUTTON ? ZOOM_ACTION : SELECT_ACTION);

		fSelected = DO_SELECTION;
		fMousePosition = screen;
		point = CorrectMousePos(point);

		if (buttons == B_SECONDARY_MOUSE_BUTTON) {
			SetViewCursor(gApp->textSelectionCursor);
			point = LimitToPage(point);
		} else {
			SetViewCursor(gApp->zoomCursor);
		}
		fSelectionStart = point;
		fSelection.SetLeftTop(point);
		fSelection.SetRightBottom(point);
		SetMouseEventMask(B_POINTER_EVENTS);

		break;
	}
}


void PDFView::ScrollIfOutside(BPoint point)
{
	float x, y, r_min, r_max;
	BRect bounds(Bounds());

	BScrollBar* scroll = ScrollBar(B_VERTICAL);

	scroll->GetRange(&r_min, &r_max);

	if (point.x < bounds.left) { // scroll left
		x = point.x;
	} else if (point.x > bounds.right) { // scroll right
		x = point.x - bounds.Width();
	} else {
		x = bounds.left;
	}
	x = min_c(r_max, max_c(x, r_min));

	scroll = ScrollBar(B_VERTICAL);
	scroll->GetRange(&r_min, &r_max);
	if (point.y < bounds.top) { // scroll up
		y = point.y;
	} else if (point.y > bounds.bottom) { // scroll down
		y = point.y - bounds.Height();
	} else {
		y = bounds.top;
	}
	y = min_c(r_max, max_c(y, r_min));
	if ((x != bounds.left) || (y != bounds.top)) {
		ScrollTo(x, y);
	}
}


void PDFView::ResizeSelection(BPoint point)
{
	point = CorrectMousePos(point);
	BRect rect(fSelection);
	if (fMouseAction == SELECT_ACTION)
		point = LimitToPage(point);
	if (point.x < fSelectionStart.x) {
		fSelection.left = point.x;
		fSelection.right = fSelectionStart.x;
	} else {
		fSelection.left = fSelectionStart.x;
		fSelection.right = point.x;
	}

	if (point.y < fSelectionStart.y) {
		fSelection.top = point.y;
		fSelection.bottom = fSelectionStart.y;
	} else {
		fSelection.top = fSelectionStart.y;
		fSelection.bottom = point.y;
	}

	if (rect != fSelection) {
		rect = rect | fSelection;
		rect.OffsetBy(fLeft, fTop);
		Invalidate(rect);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SkipMouseMoveMsgs()
{
	BMessage* mouseMovedMsg;
	while ((mouseMovedMsg = Looper()->MessageQueue()->FindMessage(B_MOUSE_MOVED, 0))) {
		Looper()->MessageQueue()->RemoveMessage(mouseMovedMsg);
		delete mouseMovedMsg;
	}
}

void PDFView::InitViewCursor(uint32 transit)
{
	// FIXME: Where is the best place to set the initial Cursor of a view?
	if ((transit == B_ENTERED_VIEW) && (fViewCursor != NULL)) {
		if (Window()->Lock()) {
			BView::SetViewCursor(fViewCursor);
			Window()->Unlock();
			fViewCursor = NULL;
		}
	}
}

void PDFView::MouseMoved(BPoint point, uint32 transit, const BMessage* msg)
{
#define UPDATE_INTERVAL 4
	int updateCounter = UPDATE_INTERVAL;

	InitViewCursor(transit);

	if (AnnotMouseMoved(point, transit, msg)) {
		return;
	}

	switch (fMouseAction) {
	case NO_ACTION:
		if (!fDragStarted)
			DisplayLink(point);
		break;
	case MOVE_ACTION: // move view
	{
		SkipMouseMoveMsgs();

		BPoint mousePosition = point;
		uint32 buttons = GetButtons();
		BPoint offset;
		float x, y, r_min, r_max;
		BScrollBar* scroll;

		point = ConvertToScreen(mousePosition);
		offset = point - fMousePosition;
		if (fInvertVerticalScrolling) {
			offset.y = fMousePosition.y - point.y;
		}
		fMousePosition = point;

		scroll = ScrollBar(B_HORIZONTAL);
		scroll->GetRange(&r_min, &r_max);
		x = min_c(r_max, max_c(scroll->Value() - offset.x, r_min));

		scroll = ScrollBar(B_VERTICAL);
		scroll->GetRange(&r_min, &r_max);
		y = min_c(r_max, max_c(scroll->Value() - offset.y, r_min));

		ScrollTo(x, y);

		if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0) {
			MouseUp(mousePosition);
		}
		break;
	}
	case SELECT_ACTION: // text selection
	case ZOOM_ACTION:   // zoom to selection
	case MOVE_ANNOT_ACTION:
	case RESIZE_ANNOT_ACTION:
		while (true) {
			SkipMouseMoveMsgs();

			uint32 buttons;

			ScrollIfOutside(point);

			switch (fMouseAction) {
			case SELECT_ACTION:
			case ZOOM_ACTION:
				ResizeSelection(point);
				break;
			case MOVE_ANNOT_ACTION:
				MoveAnnotation(point);
				break;
			case RESIZE_ANNOT_ACTION:
				ResizeAnnotation(point);
				break;
			default:;
			}

			GetMouse(&point, &buttons, false);
			if (buttons == 0) {
				MouseUp(point);
				return;
			}
			if (updateCounter == UPDATE_INTERVAL) {
				Window()->UpdateIfNeeded();
				updateCounter = 0;
			} else {
				updateCounter++;
			}
			snooze(10000);
		}
		break;
	default:;
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::MouseUp(BPoint point)
{
	if (AnnotMouseUp(point)) {
		fDragStarted = false;
	} else {
		if (fMouseAction == SELECT_ACTION) { // copy selection
			if (fSelection.Width() * fSelection.Height() * fSelection.Height() > 200) {
				fSelected = SELECTED;
				Invalidate(fSelection.OffsetByCopy(fLeft, fTop));
				CopySelection();
			} else {
				fSelected = NOT_SELECTED;
				Invalidate(fSelection.OffsetByCopy(fLeft, fTop));
			}
		} else if (fMouseAction == ZOOM_ACTION) { // zoom to selection
			fSelected = NOT_SELECTED;
			Invalidate(fSelection.OffsetByCopy(fLeft, fTop));

			if (fSelection.Width() * fSelection.Height() > 200) {
				float a = fSelection.Width() + 1, b = fSelection.Height() + 1;
				BRect bounds(Bounds());
				float n = bounds.Width() + 1, m = bounds.Height() + 1;

				int32 zoomDPI = GetZoomDPI(), newZoomDPI;
				if (a / b > n / m) {
					newZoomDPI = (int32)(zoomDPI * n / a);
				} else {
					newZoomDPI = (int32)(zoomDPI * m / b);
				}

				if (newZoomDPI > ZOOM_DPI_MAX)
					newZoomDPI = ZOOM_DPI_MAX;
				float x = fSelection.left * newZoomDPI / zoomDPI, y = fSelection.top * newZoomDPI / zoomDPI;

				int i;
				for (i = MIN_ZOOM; i <= MAX_ZOOM; i++) {
					if (newZoomDPI == kZoomDPI[i]) {
						newZoomDPI = i;
						break;
					}
				}

				if (i > MAX_ZOOM)
					newZoomDPI = -newZoomDPI;

				if (fZoom != newZoomDPI) {
					PDFWindow* w = GetPDFWindow();
					if (w)
						w->SetZoom(newZoomDPI);
					SetZoom(newZoomDPI);
				}

				ScrollTo(x, y);
			}
		}

		SelectionChanged();

		if ((fMouseAction != NO_ACTION) || fDragStarted) {
			fDragStarted = false;
			DisplayLink(point);
			fMouseAction = NO_ACTION;
		}
	}
}

///////////////////////////////////////////////////////////////////////////
LinkAction* PDFView::OnLink(BPoint point)
{
	double x, y;
	if (fRendering || (fDoc == NULL) || (fDoc->getNumPages() == 0))
		return NULL;

	point = CorrectMousePos(point);

	// PDFLock lock;
	fPage->CvtDevToUser(point.x, point.y, &x, &y);
	return fPage->FindLink(x, y);
}

bool PDFView::IsLinkToPDF(LinkAction* action, BString* path)
{
	const char* s;
	GooString* fileName;

	if (action->getKind() == actionGoToR) {
		// Borrowed const pointers from the action; truthy-check only.
		// getDest()/getNamedDest()/getFileName() now all return const ptrs.
		const LinkDest* dest = ((LinkGoToR*)action)->getDest();
		const GooString* namedDest = (dest == NULL)
			? ((LinkGoToR*)action)->getNamedDest() : NULL;
		(void)dest;
		(void)namedDest;
		s = ((LinkGoToR*)action)->getFileName()->c_str();
		if (isAbsolutePath(s))
			fileName = new GooString(s);
		else
			fileName = appendToPath(grabPath(fDoc->getFileName()->c_str()), s);
		*path = fileName->c_str();
		delete fileName;
		return true;
	} else if (action->getKind() == actionLaunch) {
		const GooString* launchName = ((LinkLaunch*)action)->getFileName();
		s = launchName->c_str();
		int len = launchName->size();
		if (len >= 4 && (!strcmp(s + len - 4, ".pdf") || !strcmp(s + len - 4, ".PDF"))) {
			if (isAbsolutePath(s))
				fileName = launchName->copy().release();  // copy() -> unique_ptr
			else
				fileName = appendToPath(grabPath(fDoc->getFileName()->c_str()), s);
			*path = fileName->c_str();
			delete fileName;
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////
bool PDFView::HandleLink(BPoint point)
{
	LinkAction* action = NULL;
	LinkActionKind kind;
	LinkDest* dest = NULL;
	const GooString* namedDest = NULL;
	GooString* fileName;
	BString pdfFile;

	action = OnLink(point);
	if (fAnnotation) {
		LinkAnnot* link = dynamic_cast<LinkAnnot*>(fAnnotation);
		if (link == NULL) {
			ShowAnnotWindow(false);
			return true;
		} else {
			action = link->GetLinkAction();
		}
	}

	if (action != NULL) {
		// PDFLock lock;

		if (IsLinkToPDF(action, &pdfFile)) {
			RecordHistory();
			if ((modifiers() & B_COMMAND_KEY)) {
				PDFWindow::Launch(pdfFile.String());
			} else {
				PDFWindow::OpenInWindow(pdfFile.String());
			}
			return true;
		}

		switch (kind = action->getKind()) {
		// GoTo / GoToR action
		case actionGoTo:
		case actionGoToR:
			if (kind == actionGoTo) {
				dest = NULL;
				namedDest = NULL;
				const LinkDest* borrowedDest = ((LinkGoTo*)action)->getDest();
				if (borrowedDest != NULL) {
					// Copy-construct via implicit copy ctor; we own this.
					dest = new LinkDest(*borrowedDest);
				} else {
					namedDest = ((LinkGoTo*)action)->getNamedDest();
				}
			}
			if (namedDest) {
				dest = fDoc->findDest(namedDest).release();
				// namedDest was a borrowed const ptr; no delete.
			}
			if (!dest) {
				if (kind == actionGoToR)
					MoveToPage(1);
			} else {
				GotoDest(dest);
				delete dest;
				return true;
			}
			break;

			// Launch action
		case actionLaunch: {
			const GooString* origName = ((LinkLaunch*)action)->getFileName();
			fileName = origName->copy().release();  // copy() -> unique_ptr
			const GooString* params = ((LinkLaunch*)action)->getParams();
			if (params != NULL) {
				fileName->append(' ');
				fileName->append(params);
			}

			fileName->append(" &");

			BString string(B_TRANSLATE("Execute the command:"));
			string += fileName->c_str();
			string += "?";
			BAlert* dialog = new BAlert(B_TRANSLATE("BePDF: Launch"), string.String(), B_TRANSLATE("OK"), B_TRANSLATE("Cancel"));
			if (dialog->Go() == 0)
				system(fileName->c_str());
			delete dialog;
			delete fileName;
			return true;
		}

		// URI action
		case actionURI:
			if (GetPDFWindow()) {
				GetPDFWindow()->LaunchHTMLBrowser(((LinkURI*)action)->getURI().c_str());
			}
			return true;

		// Named action
		case actionNamed: {
			const std::string& actionName = ((LinkNamed*)action)->getName();
			if (actionName == "NextPage") {
				MoveToPage(fCurrentPage + 1);
			} else if (actionName == "PrevPage") {
				MoveToPage(fCurrentPage - 1);
			} else if (actionName == "FirstPage") {
				MoveToPage(1);
			} else if (actionName == "LastPage") {
				MoveToPage(GetNumPages());
			} else if (actionName == "GoBack") {
				Back();
			} else if (actionName == "GoForward") {
				Forward();
			} else if (actionName == "Quit") {
				Window()->PostMessage(B_QUIT_REQUESTED);
			} else {
				// error(-1, "Unknown named action: '%s'", actionName.c_str());
			}
			break;
		}
		// TODO
		case actionMovie: // fall through
		// unknown action type
		case actionUnknown:
			fprintf(stdout, B_TRANSLATE("Unknown link action type: '%s'"), ((LinkUnknown*)action)->getAction().c_str());
			break;
		}
	}
	return false;
}


///////////////////////////////////////////////////////////////////////////
void PDFView::GotoDest(LinkDest* dest)
{
	int dx, dy;
	int pg;
	Ref pageRef;

	if (dest->isPageRef()) {
		pageRef = dest->getPageRef();
		pg = fDoc->findPage(pageRef);
	} else {
		pg = dest->getPageNum();
	}
	if (pg > 0 && pg != fCurrentPage)
		MoveToPage(pg);
	else if (pg <= 0)
		MoveToPage(1);
	switch (dest->getKind()) {
	case destXYZ:
		fPage->CvtUserToDev(dest->getLeft(), dest->getTop(), &dx, &dy);
		if (dest->getChangeLeft() || dest->getChangeTop()) {
			BRect bounds(Bounds());
			if (dest->getChangeLeft())
				bounds.left = dx;
			if (dest->getChangeTop())
				bounds.top = dy;
			ScrollTo(bounds.left, bounds.top);
		}
		//~ what is the zoom parameter?
		break;
	case destFit:
	case destFitB:
		//~ do fit
		ScrollTo(0, 0);
		break;
	case destFitH:
	case destFitBH:
		//~ do fit
		fPage->CvtUserToDev(0, dest->getTop(), &dx, &dy);
		ScrollTo(0, dy);
		break;
	case destFitV:
	case destFitBV:
		//~ do fit
		fPage->CvtUserToDev(dest->getLeft(), 0, &dx, &dy);
		ScrollTo(dx, 0);
		break;
	case destFitR:
		//~ do fit
		fPage->CvtUserToDev(dest->getLeft(), 0, &dx, &dy);
		ScrollTo(dx, dy);
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
BMenuItem* PDFView::AddAnnotItem(BMenu* menu, const char* label, uint32 what)
{
	BMessage* msg = new BMessage(what);
	msg->AddPointer("annot", fAnnotation);
	BMenuItem* item = new BMenuItem(B_TRANSLATE(label), msg);
	menu->AddItem(item);
	item->SetTarget(this);
	return item;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::ShowAnnotPopUpMenu(BPoint point)
{
	ASSERT(fAnnotation != NULL);

	BMenuItem* item;
	BPopUpMenu* menu = new BPopUpMenu("PopUpMenu");
	menu->SetAsyncAutoDestruct(true);

	AddAnnotItem(menu, fEditAnnot ? B_TRANSLATE("Leave annotation editing mode") : B_TRANSLATE("Edit"), EDIT_ANNOT_MSG);

	item = AddAnnotItem(menu, B_TRANSLATE("Delete"), DELETE_ANNOT_MSG);
	item->SetEnabled(fEditAnnot);

	if (dynamic_cast<FileAttachmentAnnot*>(fAnnotation) != NULL) {
		menu->AddSeparatorItem();
		AddAnnotItem(menu, B_TRANSLATE("Save file attachment as" B_UTF8_ELLIPSIS), SAVE_FILE_ATTACHMENT_ANNOT_MSG);
	}

	menu->AddSeparatorItem();

	AddAnnotItem(menu, B_TRANSLATE("Properties"), PROPERTIES_ANNOT_MSG);

	point -= BPoint(10, 10);
	menu->Go(point, true, false, false);
}

///////////////////////////////////////////////////////////////////////////
void PDFView::ShowPopUpMenu(BPoint point, LinkAction* action)
{
	BPopUpMenu* menu = new BPopUpMenu("PopUpMenu");
	menu->SetAsyncAutoDestruct(true);

	BMessage* msg;
	BMenuItem* i;
	BString s;

	// Open PDF file in new window
	if (IsLinkToPDF(action, &s)) {
		msg = new BMessage(OPEN_FILE_MSG);
		msg->AddString("file", s);
		i = new BMenuItem(B_TRANSLATE("Open in new window"), msg);
		i->SetTarget(this);
		menu->AddItem(i);
	}

	// Copy link location
	msg = new BMessage(COPY_LINK_MSG);
	LinkToString(action, &s);
	msg->AddString("link", s);
	i = new BMenuItem(B_TRANSLATE("Copy link"), msg);
	i->SetTarget(this);
	menu->AddItem(i);

	point -= BPoint(10, 10);
	menu->Go(point, true, false, false);
}

///////////////////////////////////////////////////////////////////////////
void PDFView::LinkToString(LinkAction* action, BString* string)
{
	const char* s = NULL;
	char* t;
	BString str;
	int pg;

	switch (action->getKind()) {
	case actionGoTo: {
		s = B_TRANSLATE("[internal link]");
		const LinkDest* borrowedDest = ((LinkGoTo*)action)->getDest();
		std::unique_ptr<LinkDest> ownedDest;
		const LinkDest* useDest = borrowedDest;
		if (useDest == NULL) {
			PDFLock lock;
			ownedDest = fDoc->findDest(((LinkGoTo*)action)->getNamedDest());
			useDest = ownedDest.get();
			if (useDest == NULL)
				break;
		}
		if (useDest->isPageRef()) {
			Ref ref = useDest->getPageRef();
			PDFLock lock;
			pg = fDoc->findPage(ref);
		} else {
			pg = useDest->getPageNum();
		}
		s = B_TRANSLATE("Go to page %d");
		t = str.LockBuffer(strlen(s) + 20);
		sprintf(t, s, pg);
		str.UnlockBuffer();
		s = str.String();
		break;
	}
	case actionGoToR:
		s = ((LinkGoToR*)action)->getFileName()->c_str();
		break;
	case actionLaunch:
		s = ((LinkLaunch*)action)->getFileName()->c_str();
		break;
	case actionURI:
		s = ((LinkURI*)action)->getURI().c_str();
		break;
	case actionNamed:
		s = ((LinkNamed*)fLinkAction)->getName().c_str();
		break;
	case actionMovie:
		// TODO
		s = B_TRANSLATE("[link type 'movie' not supported]");
		break;
	case actionUnknown:
		s = B_TRANSLATE("[unknown link]");
		break;
	}
	*string = s;
}


void PDFView::DisplayLink(BPoint point)
{
	LinkAction* action;
	BString str;
	if (fRendering || fDragStarted || (fDoc == NULL) || (fDoc->getNumPages() == 0))
		return;

	BPoint p = CorrectMousePos(point);
	// over selection?
	if (((fSelected == SELECTED) && fSelection.Contains(p)) || p.x < 0 || p.y < 0 || p.x >= fWidth || p.y >= fHeight) {
		SetViewCursor((BCursor*)B_CURSOR_SYSTEM_DEFAULT);
		return;
	}

	double x, y;
	fPage->CvtDevToUser((int)p.x, (int)p.y, &x, &y);
	Annotation* annot = fPage->GetAnnotations()->OverAnnotation(x, y, fEditAnnot);
	// over annotation?
	if (annot) {
		// new annotation?
		if (fAnnotation != annot) {
			fLinkAction = NULL;
			fAnnotation = annot;
			SetViewCursor(gApp->linkCursor);
			LinkAnnot* link = dynamic_cast<LinkAnnot*>(annot);
			if (link == NULL) {
				SetToolTip(B_TRANSLATE("Annotation"));
			} else {
				LinkToString(link->GetLinkAction(), &str);
				SetToolTip(str.String());
			}
			ShowToolTip();
		}
		return;
	} else if (fAnnotation) {
		// moved out side of annotation
		SetToolTip("");
		HideToolTip();
		SetViewCursor(gApp->handCursor);
		fAnnotation = NULL;
	}

	// over link?
	if ((action = OnLink(point)) != NULL) {
		// new link?
		if (action != fLinkAction) {
			SetViewCursor(gApp->linkCursor);
			fLinkAction = action;
			LinkToString(action, &str);
			SetToolTip(str.String());
			ShowToolTip();
		}
	} else {
		if (fLinkAction) {
			fLinkAction = NULL;
			SetToolTip("");
			HideToolTip();
		}
		SetViewCursor(gApp->handCursor);
	}
}
///////////////////////////////////////////////////////////////////////////
void PDFView::FixScrollbars()
{
	BRect frame = Bounds();
	BScrollBar* scroll;
	float x, y;
	float bigStep, smallStep;

	x = fWidth - frame.Width();
	if (x < 0.0) {
		x = 0.0;
	}
	y = fHeight - frame.Height();
	if (y < 0.0) {
		y = 0.0;
	}

	scroll = ScrollBar(B_HORIZONTAL);
	scroll->SetRange(0.0, x);
	scroll->SetProportion((fWidth - x) / fWidth);
	bigStep = frame.Width() - 2;
	smallStep = bigStep / 10.;
	scroll->SetSteps(smallStep, bigStep);

	scroll = ScrollBar(B_VERTICAL);
	scroll->SetRange(0.0, y);
	scroll->SetProportion((fHeight - y) / fHeight);
	bigStep = frame.Height() - 2;
	smallStep = bigStep / 10.;
	scroll->SetSteps(smallStep, bigStep);
}

///////////////////////////////////////////////////////////////////////////
void PDFView::Redraw(PDFDoc* fDoc)
{
	PDFWindow* parentWin = GetPDFWindow();

	fMouseWheelDY = 0;

	// abort rendering process if neccesary and wait for it to finish
	WaitForPage(true);
	if (parentWin) {
		parentWin->NewPage(fCurrentPage);
	}
	fRendering = true;

	if (fDoc == NULL)
		fDoc = this->fDoc;

	fSelected = NOT_SELECTED;
	fPageRenderer.Start(fPage, fCurrentPage, GetZoomDPI(), fRotation, &fRendererID, fEditAnnot);
	fAnnotation = NULL;
	fLinkAction = NULL;

	fBitmap = fPage->GetBitmap();
	fWidth = fPage->GetWidth();
	fHeight = fPage->GetHeight();
	CenterPage();
	FixScrollbars();

	if (parentWin) {
		parentWin->GetFileAttributes()->SetPage(fCurrentPage);
		parentWin->SetPage(fCurrentPage);
		parentWin->SetZoomSize(fWidth, fHeight);
	}

	Invalidate();
}

///////////////////////////////////////////////////////////
void PDFView::RestartDoc()
{
	WaitForPage(true);
	fPageRenderer.StartDoc(fColorSpace);
	Redraw();
}

///////////////////////////////////////////////////////////////////////////
void PDFView::PostRedraw(thread_id id, BBitmap* bitmap)
{
	// TODO
	if (id != -1) {
		fRendering = false;
		fRendererID = -1;
		Invalidate();
		BPoint mouse;
		uint32 buttons;
		GetMouse(&mouse, &buttons);
		DisplayLink(mouse);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::RedrawAborted(thread_id id, BBitmap* bitmap)
{
	if ((fRendererID == id) && (id != -1)) {
		fRendering = false;
		fRendererID = -1;
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::WaitForPage(bool abort)
{
	if (abort) {
		fPageRenderer.Abort();
	}
	fPageRenderer.Wait();
}

///////////////////////////////////////////////////////////////////////////
void PDFView::CenterPage()
{
	BRect bounds(Bounds());
	if (bounds.Width() + 1 > fWidth) { // center page horizontally
		fLeft = (bounds.Width() - fWidth) / 2;
	} else {
		fLeft = 0;
	}

	if (bounds.Height() + 1 > fHeight) { // center page vertically
		fTop = (bounds.Height() - fHeight) / 2;
	} else {
		fTop = 0;
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::Resize()
{
	CenterPage();
	FixScrollbars();
	Invalidate();
}

///////////////////////////////////////////////////////////////////////////
void PDFView::Dump()
{}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetPage(int page)
{
	fSelected = NOT_SELECTED;

	int currentPage = fCurrentPage;
	if (fCurrentPage != page) {
		if (page < 1) {
			fCurrentPage = 1;
		} else if (page > GetNumPages()) {
			fCurrentPage = GetNumPages();
		} else {
			fCurrentPage = page;
		}

		if (currentPage != fCurrentPage) {
			Redraw();
		}
	}
}


//////////////////////////////////////////////////////////////////
void PDFView::MoveToPage(int page, bool top)
{
	if (page > GetNumPages())
		page = GetNumPages();
	if (page <= 0)
		page = 1;
	bool notChanged = fCurrentPage == page;
	if (notChanged)
		return;

	SyncAnnotation(true);
	RecordHistory();

	BRect bounds(Bounds());
	ScrollTo(bounds.left, top ? 0 : fHeight);
	SetPage(page);
}

//////////////////////////////////////////////////////////////////
void PDFView::MoveToPage(int num, int gen, bool top)
{
	MoveToPage(fDoc->findPage(Ref{num, gen}), top);
}

//////////////////////////////////////////////////////////////////
void PDFView::MoveToPage(const char* string, bool top)
{
	WaitForPage(true);
	GooString s(string);
	std::unique_ptr<LinkDest> link = fDoc->getCatalog()->findDest(&s);
	if (link) {
		if (link->isPageRef()) {
			Ref r = link->getPageRef();
			MoveToPage(r.num, r.gen, top);
		} else {
			MoveToPage(link->getPageNum(), top);
		}
		// unique_ptr cleans up
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::BeginHistoryNavigation()
{
	// Note: We are going into history navigation mode and
	// have to store the current position state in the history.
	if (fNavigationState == kNotInHistory) {
		fNavigationState = kInHistory;
		BRect bounds(Bounds());
		fHistory.AddPosition(fCurrentPage, fZoom, bounds.left, bounds.top, fRotation);
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::EndHistoryNavigation()
{
	// Note: When not navigating through the history (= kNotInHistory) the
	// current position information is not stored in the history.
	// The state is stored prior to changes of the state to the history.
	// When in navigating through the history (kInHistory), the restored
	// state is the top of the history. This state has to be replaced
	// when we record the current state (= Back()).
	if (fNavigationState == kInHistory) {
		fNavigationState = kNotInHistory;
		fHistory.Back();
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::RecordHistory()
{
	EndHistoryNavigation();
	BRect bounds(Bounds());
	fHistory.AddPosition(fCurrentPage, fZoom, bounds.left, bounds.top, fRotation);
}

//////////////////////////////////////////////////////////////////
void PDFView::RecordHistory(entry_ref ref, const char* ownerPassword, const char* userPassword)
{
	// XXX: record file open events too, otherwise they are missed if page state does not change between
	// open events.
	fHistory.SetFile(ref, ownerPassword, userPassword);
}

//////////////////////////////////////////////////////////////////
void PDFView::RestoreHistory()
{
	HistoryEntry* e = fHistory.GetTop();
	if (e == NULL)
		return;

	HistoryPosition* pos = dynamic_cast<HistoryPosition*>(e);
	if (pos) {
		PDFWindow* w = GetPDFWindow();
		HistoryFile* file = pos->GetFile();
		entry_ref ref = file->GetRef();

		if (w && !w->IsCurrentFile(&ref)) {
			bool encrypted;
			w->LoadFile(&ref, file->GetOwnerPassword(), file->GetUserPassword(), &encrypted);
			return;
		}

		int page;
		int32 left, top;
		page = pos->GetPage();
		fZoom = pos->GetZoom();
		left = pos->GetLeft();
		top = pos->GetTop();
		fRotation = pos->GetRotation();

		if (w) {
			w->SetZoom(fZoom);
			w->SetRotation(fRotation);
		}
		fCurrentPage = -1;
		SetPage(page);
		ScrollTo(left, top);
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::Back()
{
	BeginHistoryNavigation();
	if (fHistory.Back()) {
		RestoreHistory();
	}
}
//////////////////////////////////////////////////////////////////
void PDFView::Forward()
{
	BeginHistoryNavigation();
	if (fHistory.Forward()) {
		RestoreHistory();
	}
}
//////////////////////////////////////////////////////////////////
void PDFView::SetZoom(int zoom)
{
	if (fZoom != zoom) {
		RecordHistory();
		fZoom = zoom;
		Redraw();
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::Zoom(bool zoomIn)
{
	int32 zoomOld = GetZoomDPI();
	int32 zoomNew;
	if (zoomIn) {
		zoomNew = (int32)(zoomOld * 1.2);
		if (zoomNew > ZOOM_DPI_MAX)
			zoomNew = ZOOM_DPI_MAX;
	} else {
		zoomNew = (int32)(zoomOld / 1.2);
		if (zoomNew < ZOOM_DPI_MIN)
			zoomNew = ZOOM_DPI_MIN;
	}
	if (zoomNew != zoomOld) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w)
			w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::FitToPageWidth()
{
	BRect r(Bounds());
	int32 zoomOld = GetZoomDPI();
	int32 zoomNew = (int32)(r.Width() * zoomOld / fWidth);
	if (zoomOld != zoomNew) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w)
			w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::FitToPage()
{
	BRect r(Bounds());
	int32 zoomOld = GetZoomDPI();
	int32 zoomNewH = (int32)(r.Width() * zoomOld / fWidth);
	int32 zoomNewV = (int32)(r.Height() * zoomOld / fHeight);
	int32 zoomNew = (zoomNewH < zoomNewV) ? zoomNewH : zoomNewV;
	if (zoomOld != zoomNew) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w)
			w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
int16 PDFView::GetZoomDPI() const
{
	if (fZoom >= MIN_ZOOM)
		return kZoomDPI[fZoom - MIN_ZOOM];
	else
		return -fZoom;
}

//////////////////////////////////////////////////////////////////
void PDFView::SetRotation(float rotation)
{
	if (fRotation != rotation) {
		RecordHistory();
		gApp->GetSettings()->SetRotation(rotation);
		fRotation = rotation;
		PDFWindow* w = GetPDFWindow();
		if (w)
			w->SetRotation(fRotation);
		Redraw();
	}
}

void PDFView::RotateClockwise()
{
	SetRotation(((int)fRotation + 90) % 360);
}

void PDFView::RotateAntiClockwise()
{
	SetRotation(((int)fRotation - 90 + 360) % 360);
}

//////////////////////////////////////////////////////////////////
void PDFView::SetSelection(int xMin, int yMin, int xMax, int yMax, bool display)
{
	BRect rect(fSelection);
	fSelection.Set(xMin, yMin, xMax, yMax);
	if (Window()->Lock()) {
		if (fSelected == NOT_SELECTED) {
			Invalidate(fSelection.OffsetByCopy(fLeft, fTop));
		} else {
			rect = fSelection | rect;
			rect.OffsetBy(fLeft, fTop);
			Invalidate(rect);
		}
		fSelected = SELECTED;

		if (display) { // make selection visible
			BRect bounds(Bounds());
			xMin -= (int)fLeft;
			xMax -= (int)fLeft;
			yMin -= (int)fTop;
			yMax -= (int)fTop;
			float x, y;

			if ((bounds.left <= xMin) && (xMax <= bounds.right))
				x = bounds.left;
			else
				x = xMin;

			if ((bounds.top <= yMin) && (yMax <= bounds.bottom))
				y = bounds.top;
			else
				y = yMin;

			ScrollTo(x, y);
		}

		Window()->Unlock();
	}
}

//////////////////////////////////////////////////////////////////
void PDFView::GetSelection(int& xMin, int& yMin, int& xMax, int& yMax)
{
	if (fSelected == SELECTED) {
		xMin = (int)fSelection.left;
		yMin = (int)fSelection.top;
		xMax = (int)fSelection.right;
		yMax = (int)fSelection.bottom;
	} else {
		xMin = yMin = xMax = yMax = 0;
	}
}
//////////////////////////////////////////////////////////////////
BString* PDFView::GetSelectedText()
{
	if (!fRendering && (fSelected == SELECTED) && (fSelection.left < fSelection.right) && (fSelection.top < fSelection.bottom)) {
		GooString* s = fPage->GetText(fSelection.left, fSelection.top, fSelection.right, fSelection.bottom);
		if (s) {
			BString* str = new BString(s->c_str());
			delete s;
			return str;
		}
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////
void PDFView::CopyText(BString* str)
{
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();

		BMessage* clip = NULL;
		if ((clip = be_clipboard->Data()) != NULL) {
			// copy text to clipboard
			clip->AddData("text/plain", B_MIME_TYPE, str->String(), str->Length());
			be_clipboard->Commit();
		}
		be_clipboard->Unlock();
	}
}
//////////////////////////////////////////////////////////////////
void PDFView::CopySelection()
{
	if (!fRendering && (fSelected == SELECTED) && (fSelection.left < fSelection.right) && (fSelection.top < fSelection.bottom)) {
		if (be_clipboard->Lock()) {
			be_clipboard->Clear();

			BMessage* clip = NULL;
			if ((clip = be_clipboard->Data()) != NULL) {
				// copy bitmap to clipboard
				BMessage data;
				BRect sel(max_c(fSelection.left, 0),
				    max_c(fSelection.top, 0),
				    min_c(fSelection.right, fWidth),
				    min_c(fSelection.bottom, fHeight));
				BRect rect(0, 0, sel.Width(), sel.Height());
				BView view(rect, NULL, B_FOLLOW_NONE, B_WILL_DRAW);
				BBitmap bitmap(rect, fBitmap->ColorSpace(), true);
				if (bitmap.Lock()) {
					bitmap.AddChild(&view);
					view.DrawBitmap(fBitmap, sel, rect);
					view.Sync();
					bitmap.RemoveChild(&view);
					bitmap.Unlock();
				}
				bitmap.Archive(&data);
				clip->AddMessage("image/x-vnd.Be-bitmap", &data);
				clip->AddRect("rect", rect);

				// copy text to clipboard
				BString* str = GetSelectedText();
				if (str) {
					clip->AddData("text/plain", B_MIME_TYPE, str->String(), str->Length());
					delete str;
				}
				be_clipboard->Commit();
			}
			be_clipboard->Unlock();
		}
	}
}


///////////////////////////////////////////////////////////
void PDFView::SelectAll()
{
	if ((fSelected == NOT_SELECTED) || (fSelected == SELECTED)) {
		fSelected = SELECTED;
		fSelection.Set(0, 0, fWidth, fHeight);
		SelectionChanged();
		Invalidate();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SelectNone()
{
	if (fSelected == SELECTED) {
		fSelected = NOT_SELECTED;
		SelectionChanged();
		Invalidate();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SetFilledSelection(bool filled)
{
	fFilledSelection = filled;
	gApp->GetSettings()->SetFilledSelection(filled);

	if (fSelected == SELECTED) {
		Invalidate();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SelectionChanged()
{
	PDFWindow* w = GetPDFWindow();
	if (w) {
		w->UpdateInputEnabler();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SendDragMessage(uint32 protocol)
{
	fDragStarted = true;
	SetMouseEventMask(B_POINTER_EVENTS);
	if (protocol == B_SIMPLE_DATA) {
		BMessage drag(B_SIMPLE_DATA);
		drag.AddString("be:types", "text/plain");
		drag.AddString("be:types", B_FILE_MIME_TYPE);

		BTranslatorRoster* roster = BTranslatorRoster::Default();
		BBitmapStream stream(fBitmap);

		translator_info* outInfo;
		int32 outNumInfo;

		drag.AddString("be:filetypes", "text/plain");
		drag.AddString("be:type_descriptions", "Text");

		if ((roster->GetTranslators(&stream, NULL, &outInfo, &outNumInfo) == B_OK) && (outNumInfo >= 1)) {
			for (int32 i = 0; i < outNumInfo; i++) {
				const translation_format* fmts;
				int32 num_fmts;
				roster->GetOutputFormats(outInfo[i].translator, &fmts, &num_fmts);
				for (int32 j = 0; j < num_fmts; j++) {
					if (strcmp(fmts[j].MIME, "image/x-be-bitmap") != 0) {
						drag.AddString("be:filetypes", fmts[j].MIME);
						drag.AddString("be:type_descriptions", fmts[j].name);
					}
				}
			}
			drag.AddInt32("be:actions", B_COPY_TARGET);
			drag.AddString("be:clip_name", "Untitled clipping");
			DragMessage(&drag, fSelection.OffsetByCopy(fLeft, fTop));
		}
		BBitmap* bm;
		stream.DetachBitmap(&bm);
		if (bm != fBitmap)
			delete bm;
	} else if (protocol == B_MIME_DATA) {
		BMessage drag(B_MIME_DATA);
		BString* str = GetSelectedText();
		if (str) {
			drag.AddInt32("be:actions", B_TRASH_TARGET);
			drag.AddData("text/plain", B_MIME_DATA, str->String(), str->Length());
			delete str;
			DragMessage(&drag, fSelection.OffsetByCopy(fLeft, fTop));
		}
	}
}

void PDFView::SendDataMessage(BMessage* reply)
{
	BMessage data(B_MIME_DATA);

	entry_ref dir;
	BString name, filetype;
	if (B_OK != reply->FindString("be:filetypes", &filetype)) {
		// send abort message to target application
		// reply->SendReply(&data);
		return;
	}
	bool saveToFile = (reply->FindRef("directory", &dir) == B_OK) && (reply->FindString("name", &name) == B_OK);

	if (filetype == "text/plain") {
		BString* str = GetSelectedText();
		if (str) {
			if (saveToFile) {
				// write text to file
				BDirectory d(&dir);
				BNode node(&d, name.String());
				// set mime type
				BNodeInfo info(&node);
				if (info.InitCheck() == B_OK) {
					info.SetType("text/plain");
				}
				// write data
				BFile file(&d, name.String(), B_WRITE_ONLY);
				file.Write(str->String(), str->Length());
			} else {
				// send text in message to target application
				data.AddString("text/plain", str->String());
				reply->SendReply(&data);
			}
			delete str;
		}
		return;
	}
#if MORE_DEBUG
	BString s;
	s << name << " " << filetype;
	BAlert* a = new BAlert("Info", s.String(), "OK");
	a->Go();
#endif
	//~ sending image in message to target application not implemented
	if (!saveToFile)
		return;

	// copy selection to bitmap
	BRect sel(max_c(fSelection.left, 0), max_c(fSelection.top, 0), min_c(fSelection.right, fWidth), min_c(fSelection.bottom, fHeight));
	BRect rect(0, 0, sel.Width(), sel.Height());
	BView view(rect, NULL, B_FOLLOW_NONE, B_WILL_DRAW);
	BBitmap* bitmap = new BBitmap(rect, fBitmap->ColorSpace(), true);
	if (bitmap->Lock()) {
		bitmap->AddChild(&view);
		view.DrawBitmap(fBitmap, sel, rect);
		view.Sync();
		bitmap->RemoveChild(&view);
		bitmap->Unlock();
	}

	BBitmapStream stream(bitmap); // destructor frees bitmap

	// identify type
	BTranslatorRoster* roster = BTranslatorRoster::Default();
	translator_info* outInfo;
	int32 outNumInfo;
	if ((roster->GetTranslators(&stream, NULL, &outInfo, &outNumInfo) == B_OK) && (outNumInfo >= 1)) {
		for (int32 i = 0; i < outNumInfo; i++) {
			const translation_format* fmts;
			int32 num_fmts;
			roster->GetOutputFormats(outInfo[i].translator, &fmts, &num_fmts);
			for (int32 j = 0; j < num_fmts; j++) {
				if (strcmp(fmts[j].MIME, filetype.String()) == 0) {
					// save bitmap to file
					BTranslatorRoster* roster = BTranslatorRoster::Default();
					BDirectory d(&dir);
					BNode node(&d, name.String());
					// set mime type
					BNodeInfo info(&node);
					if (info.InitCheck() == B_OK) {
						info.SetType(fmts[j].MIME);
					}
					// write data
					BFile file(&d, name.String(), B_WRITE_ONLY);
					roster->Translate(&stream, NULL, NULL, &file, fmts[j].type);
					// send finished message to target application
					// reply->SendReply(&data);
					return;
				}
			}
		}
	}
	// send finished message to target application
	// reply->SendReply(&data);
}

///////////////////////////////////////////////////////////
void PDFView::SetColorSpace(color_space colorSpace)
{
	bool refresh = fColorSpace != colorSpace;
	if (refresh) {
		fColorSpace = colorSpace;
		RestartDoc();
	}
}

///////////////////////////////////////////////////////////
void PDFView::UpdateSettings(GlobalSettings* settings)
{
	fInvertVerticalScrolling = settings->GetInvertVerticalScrolling();
}


///////////////////////////////////////////////////////////
void PDFView::BeginEditAnnot()
{
	SelectNone();
	SetAction(NO_ACTION);
	fEditAnnot = true;
	fAnnotation = NULL;
	fAnnotInEditor = NULL;
	fInsertAnnot = NULL;
	fLinkAction = NULL;
	Redraw();
}

///////////////////////////////////////////////////////////
void PDFView::EndEditAnnot()
{
	SyncAnnotation(true);
	SetAction(NO_ACTION);
	fEditAnnot = false;
	fInsertAnnot = NULL;
	fAnnotInEditor = NULL;
	fAnnotation = NULL;
	fLinkAction = NULL;
	Redraw();
}

///////////////////////////////////////////////////////////
void PDFView::InsertAnnotation(Annotation* a)
{
	fInsertAnnot = a;
	fAnnotation = NULL;
	SetViewCursor(gApp->pointerCursor);
	const bool updateOnly = dynamic_cast<FreeTextAnnot*>(a) == NULL;
	ShowAnnotWindow(true, updateOnly);
}

///////////////////////////////////////////////////////////
void PDFView::UpdateAnnotation(Annotation* a, const char* contents, const char* font, float size, const char* align)
{
	if (fAnnotInEditor == a) {
		ASSERT(a != NULL);
		GooString* c = Utf8ToUcs2(contents);
		fAnnotInEditor->SetContents(c);
		fAnnotInEditor->SetChanged();
		FreeTextAnnot* ft = dynamic_cast<FreeTextAnnot*>(fAnnotInEditor);
		if (ft && font) {
			ft->SetFont(BePDFAcroForm::GetStandardFonts()->FindByName(font));
			ft->SetFontSize(size);
			ft->SetJustification(ToFreeTextJustification(align));
		}

		BRect invRect = CvtUserToDev(fAnnotInEditor->GetRect());
		Invalidate(invRect);
		delete c;
	}
}

///////////////////////////////////////////////////////////
void PDFView::UpdateAnnotation(Annotation* a, BMessage* data)
{
	const char* contents;
	const char* font;
	const char* align;
	float size;
	if (data->FindString("contents", &contents) != B_OK) {
		return; // failure
	}
	if (data->FindString("font", &font) != B_OK) {
		font = NULL;
	}
	if (data->FindFloat("size", &size) != B_OK) {
		size = 0.0;
	}
	if (data->FindString("alignment", &align) != B_OK) {
		align = "left";
	}
	UpdateAnnotation(a, contents, font, size, align);
}


void PDFView::ShowAnnotWindow(bool editable, bool updateOnly)
{
	SyncAnnotation(false);
	if (editable) {
		fAnnotInEditor = fAnnotation;
		if (fAnnotInEditor == NULL)
			fAnnotInEditor = fInsertAnnot;
	}

	Annotation* annotation = fAnnotation;
	if (annotation == NULL) {
		annotation = fInsertAnnot;
		if (annotation == NULL)
			return;
	}
	BString *label, *date, *contents;
	char buffer[80];
	const char* d = to_date(annotation->GetDate(), buffer);
	if (annotation->GetTitle()) {
		label = TextToUtf8(annotation->GetTitle()->c_str(), annotation->GetTitle()->size());
	} else {
		label = new BString();
	}
	date = TextToUtf8(d, strlen(d));
	contents = TextToUtf8(annotation->GetContents()->c_str(), annotation->GetContents()->size());
	AnnotationWindow* w = NULL;
	PDFWindow* win = GetPDFWindow();
	if (win) {
		// window already open?
		w = win->GetAnnotationWindow();
		if (w == NULL && !updateOnly) {
			// open it if not open and requested
			w = win->ShowAnnotationWindow();
		}
		if (w) {
			const char* font = NULL;
			const char* align = NULL;
			float size = 0;
			FreeTextAnnot* ft = dynamic_cast<FreeTextAnnot*>(annotation);
			if (ft) {
				font = ft->GetFont()->GetName();
				size = ft->GetFontSize();
				align = ToString(ft->GetJustification());
			}
			w->MakeEditable(editable);
			w->Update(annotation, label->String(), date->String(), contents->String(), font, size, align);
			w->Unlock();
		}
	}
	delete contents;
	delete date;
	delete label;
}

///////////////////////////////////////////////////////////
void PDFView::ClearAnnotationWindow()
{
	PDFWindow* win = GetPDFWindow();
	if (win == NULL)
		return;
	AnnotationWindow* w = win->GetAnnotationWindow();
	if (w) {
		w->MakeEditable(false);
		w->Update(NULL, "", "", "", NULL, 0, NULL);
		w->Unlock();
	}
}

///////////////////////////////////////////////////////////
// read info from AnnotationWindow, e.g. before a new annotation is selected
void PDFView::SyncAnnotation(bool clearWindow)
{
	if (fAnnotInEditor == NULL)
		return;
	PDFWindow* win = GetPDFWindow();
	if (win == NULL)
		return;
	AnnotationWindow* w = win->GetAnnotationWindow();
	if (w) {
		BMessage msg;
		w->GetContents(fAnnotInEditor, &msg);
		UpdateAnnotation(fAnnotInEditor, &msg);
		if (clearWindow) {
			ClearAnnotationWindow();
		}
		w->Unlock();
	}
}

class SaveFileAttachmentThread : public SaveThread {
public:
	SaveFileAttachmentThread(const char* title, XRef* xref, const BMessage* message)
	    : SaveThread(title, xref),
	      fMessage(*message)
	{}

	int32 Run()
	{
		entry_ref dir;
		BString name;

		if (fMessage.FindRef("directory", &dir) != B_OK || fMessage.FindString("name", &name) != B_OK) {
			// should not happen
			return -1;
		}

		BPath path(&dir);
		path.Append(name.String());

		void* pointer;
		if (fMessage.FindPointer("fileAttachment", &pointer) != B_OK) {
			// should not happen
			return -1;
		}

		// TODO validate pointer
		FileAttachmentAnnot* fileAttachment = (FileAttachmentAnnot*)pointer;

		if (fileAttachment != NULL) {
			fileAttachment->Save(GetXRef(), path.Path());
		}

		return 0;
	}

private:
	BMessage fMessage;
};

void PDFView::SaveFileAttachment(BMessage* msg)
{
	const char* title = B_TRANSLATE("Saving file attachment:");
	SaveFileAttachmentThread* thread = new SaveFileAttachmentThread(title, fDoc->getXRef(), msg);
	thread->Resume();
}
