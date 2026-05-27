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
// BeOS
#include <locale/Catalog.h>
#include <Button.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <TextControl.h>
#include <Window.h>
// xpdf
#include <Link.h>
#include <Object.h>
// BePDF
#include "Logging.h"
#include "BePDF.h"
#include "LayoutUtils.h"
#include "TextConversion.h"
#include "OutlinesWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "OutlinesWindow"

// Implementation of OutlineStyle

OutlineStyle::OutlineStyle(const BFont* font, rgb_color color)
    : fFont(font),
      fColor(color)
{}

// Implementation of OutlineStyle

OutlineStyleList::OutlineStyleList()
{
	fFonts[PLAIN_STYLE] = *be_plain_font;
	fFonts[BOLD_STYLE] = *be_plain_font;
	fFonts[BOLD_STYLE].SetFace(B_BOLD_FACE);
	fFonts[ITALIC_STYLE] = *be_plain_font;
	fFonts[ITALIC_STYLE].SetFace(B_ITALIC_FACE);
	fFonts[BOLD_ITALIC_STYLE] = *be_plain_font;
	fFonts[BOLD_ITALIC_STYLE].SetFace(B_BOLD_FACE | B_ITALIC_FACE);
}

OutlineStyleList::~OutlineStyleList()
{
	const int32 n = fList.CountItems();
	for (int32 i = 0; i < n; i++) {
		OutlineStyle* style = (OutlineStyle*)fList.ItemAt(i);
		delete style;
	}
	fList.MakeEmpty();
}

const BFont* OutlineStyleList::GetFont(int style) const
{
	return &fFonts[style];
}

OutlineStyle* OutlineStyleList::GetStyle(int style, rgb_color color)
{
	const int32 n = fList.CountItems();
	for (int32 i = 0; i < n; i++) {
		OutlineStyle* os = (OutlineStyle*)fList.ItemAt(i);
		if (os->GetFont() == GetFont(style) && memcmp(os->GetColor(), &color, sizeof(color)) == 0) {
			return os;
		}
	}
	OutlineStyle* os = new OutlineStyle(GetFont(style), color);
	fList.AddItem(os);
	return os;
}

OutlineStyle* OutlineStyleList::GetDefaultStyle()
{
	rgb_color black = {0, 0, 0, 0};
	return GetStyle(PLAIN_STYLE, black);
}

// Implementation of OutlineListItem

OutlineListItem::OutlineListItem(const char* string, uint32 level, bool expanded, OutlineStyle* style)
    : BListItem(level, expanded),
      fString(string),
      fType(linkUndefined),
      fStyle(style)
{}

OutlineListItem::~OutlineListItem()
{
	if (fType == linkDest)
		delete fLink.dest;
	else if (fType == linkString)
		delete fLink.string;
}


void OutlineListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	rgb_color color;

	owner->PushState();
	// select background color
	if (IsSelected()) {
		color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
	} else {
		color = ui_color(B_LIST_BACKGROUND_COLOR);
	}
	// fill background
	owner->SetHighColor(color);
	owner->FillRect(frame);
	// set font color
	if (IsEnabled()) {
		owner->SetHighColor(*fStyle->GetColor());
	} else {
		owner->SetHighColor(*fStyle->GetColor());
	}
	// set background color
	owner->SetLowColor(color);
	// display text
	owner->MovePenTo(frame.left + 4, frame.bottom - 2);
	owner->SetFont(fStyle->GetFont());
	owner->DrawString(fString.String());

	owner->PopState();
}

void OutlineListItem::SetLink(LinkDest* dest)
{
	if (dest->isOk() && (fType == linkUndefined)) {
		fType = linkDest;
		fLink.dest = dest;
	} else {
		delete dest;
	}
};

void OutlineListItem::SetLink(GooString* s)
{
	if (fType == linkUndefined) {
		fType = linkString;
		fLink.string = s;
	}
}

void OutlineListItem::SetPageNum(int pageNum)
{
	if (fType == linkUndefined) {
		fType = linkPageNum;
		fLink.pageNum = pageNum;
	}
}

// Implementation of OutlinesView
void OutlinesView::ReadOutlines(Object* o, uint32 level)
{
	Object* current = new Object();
	o->copy(current);
	Object title;
	Object child;
	bool loop;
	do {
		if (current->dictLookup("Title", &title) && !title.isNull()) {
			bool open = true;
			Object count;
			if (current->dictLookup("Count", &count) && count.isInt()) {
				open = count.getInt() > 0;
			}

			OutlineListItem* item;
			if (title.isString()) {
				BString* s = TextToUtf8(title.getString()->c_str(), title.getString()->size());
				if (s && s->Length() > 0) {
					// end string at first newline character
					char* str = s->LockBuffer(s->Length());
					char* newline = strchr(str, '\n');
					if (newline)
						*newline = 0;
					s->UnlockBuffer();

					item = new OutlineListItem(s->String(), level, open, GetDefaultStyle());
					delete s;
				} else {
					item = new OutlineListItem(B_TRANSLATE("No title"), level, open, GetDefaultStyle());
				}
			} else {
				item = new OutlineListItem(B_TRANSLATE("No title"), level, open, GetDefaultStyle());
			}
			fList->AddItem(item);

			Object dest;
			if (current->dictLookup("Dest", &dest)) {
				if (dest.isName()) {
					item->SetLink(new GooString(dest.getName()));
				} else if (dest.isArray()) {
					item->SetLink(new LinkDest(dest.getArray()));
				} else if (dest.isString()) {
					item->SetLink(dest.getString()->copy());
				}
			}

			Object dict;
			if (current->dictLookup("A", &dict) && dict.isDict()) {
				Object s;
				dict.dictLookup("S", &s);
				// GoTo action
				if (s.isName("GoTo")) {
					dict.dictLookup("D", &dest);
					if (dest.isName()) {
						item->SetLink(new GooString(dest.getName()));
					} else if (dest.isArray()) {
						item->SetLink(new LinkDest(dest.getArray()));
					} else if (dest.isString()) {
						item->SetLink(dest.getString()->copy());
					}
				}
			}

			// PDF 1.4
			rgb_color item_color = {0, 0, 0, 0};
			Object color;
			if (current->dictLookup("C", &color) && color.isArray() && color.arrayGetLength() == 3) {
				Object c;
				rgb_color rgb;
				if (color.arrayGet(0, &c) && c.isReal()) {
					rgb.red = (int)(255 * c.getReal());
					if (color.arrayGet(1, &c) && c.isReal()) {
						rgb.green = (int)(255 * c.getReal());
						if (color.arrayGet(2, &c) && c.isReal()) {
							rgb.blue = (int)(255 * c.getReal());
							// set font color
							item_color = rgb;
						}
					}
				}
			}

			Object style;
			int item_style = OutlineStyleList::PLAIN_STYLE;
			if (current->dictLookup("F", &style) && style.isInt()) {
				int s = style.getInt();
				bool bold = (s & 1) != 0;
				bool italic = (s & 2) != 0;
				;
				// set font style
				if (bold)
					item_style |= OutlineStyleList::BOLD_STYLE;
				if (italic)
					item_style |= OutlineStyleList::ITALIC_STYLE;
			}

			item->SetStyle(fOutlineStyleList.GetStyle(item_style, item_color));
			/*
			Object aa;
			if (current->dictLookup("AA", &aa) && !aa.isNull()) {
				Trace(LOG_DEBUG, " <AA>\n");
			}

			Object se;
			if (current->dictLookup("SE", &se) && !se.isNull()) {
				Trace(LOG_DEBUG, " <SE>\n");
			}
*/
			// traverse child
			if (current->dictLookup("First", &child) && child.isDict() && !child.isNull()) {
				ReadOutlines(&child, level + 1);
			}

			// expanded argument of OutlineListItem constructor does not work!
			if (open)
				fList->Expand(item);
			else
				fList->Collapse(item);
		}


		Object* next = new Object();
		if (current->dictLookup("Next", next) && next->isDict() && !next->isNull()) {
			delete current;
			current = next;
			loop = true;
		} else {
			loop = false;
			delete next;
		}
	} while (loop);
	delete current;
}

OutlinesView::OutlinesView(Catalog* catalog, BMessage* bookmarks, GlobalSettings* settings, BLooper* looper, uint32 flags)
    : BScrollView("BookmarksScroll", NULL, 0, true, true),
      fLooper(looper),
      fList(NULL),
      fCatalog(NULL),
      fBookmarks(NULL),
      fNeedsUpdate(true),
      fUserDefined(NULL),
      fEmptyUserBM(NULL)
{
	SetTarget(fList = new BOutlineListView("", B_SINGLE_SELECTION_LIST));
	fEmptyUserBM = new OutlineListItem(B_TRANSLATE("<empty>"), 1, true, GetDefaultStyle());
	SetCatalog(catalog, bookmarks);
}

OutlinesView::~OutlinesView()
{
	if (fLooper) {
		BMessage msg(QUIT_NOTIFY);
		fLooper->PostMessage(&msg);
	}
	if (fList) {
		fList->RemoveItem(fEmptyUserBM);
		delete fEmptyUserBM;
		MakeEmpty(fList);
	}
}

void OutlinesView::AttachedToWindow()
{
	fList->SetSelectionMessage(new BMessage('Outl'));
	fList->SetTarget(this);
}

void OutlinesView::SetCatalog(Catalog* catalog, BMessage* bookmarks)
{
	if (fCatalog != catalog) {
		fCatalog = catalog;
		fBookmarks = bookmarks;
		fNeedsUpdate = true;
		InitUserBookmarks(true);
	}
}

void OutlinesView::Activate()
{
	if (fNeedsUpdate) {
		fNeedsUpdate = false;
		fList->RemoveItem(fEmptyUserBM); // keep fEmptyUserBM
		MakeEmpty(fList);
		Object obj;
		fList->AddItem(new OutlineListItem(B_TRANSLATE("Document"), 0, true, GetDefaultStyle()));
		gPdfLock->Lock();
		if (fCatalog->getOutline()->isDict() && fCatalog->getOutline()->dictLookup("First", &obj) && !obj.isNull()) {
			ReadOutlines(&obj, 1);
		}
		gPdfLock->Unlock();
		if (fList->CountItems() == 1) {
			fList->AddItem(new OutlineListItem(B_TRANSLATE("<empty>"), 1, true, GetDefaultStyle()));
		}
		fUserDefined = new OutlineListItem(B_TRANSLATE("User defined"), 0, true, GetDefaultStyle());
		fList->AddItem(fUserDefined);
		InitUserBookmarks(false);
	}
}


// handling of user bookmarks
void OutlinesView::InitUserBookmarks(bool initOnly)
{
	fBookmark.Clear();
	if (fBookmarks == NULL || fBookmarks->IsEmpty()) {
		if (!initOnly) {
			fList->AddItem(fEmptyUserBM);
		}
	} else {
		BString label;
		int32 pageNum, i = 0;
		while (fBookmarks->FindString("l", i, &label) == B_OK && fBookmarks->FindInt32("p", i, &pageNum) == B_OK) {
			fBookmark.Set(pageNum, true);
			if (!initOnly) {
				OutlineListItem* item = new OutlineListItem(label.String(), 1, true, GetDefaultStyle());
				item->SetPageNum(pageNum);
				fList->AddItem(item);
			}
			i++;
		}
	}
}

static BListItem* store_bookmarks(BListItem* i, void* d)
{
	OutlineListItem* item = (OutlineListItem*)i;
	BMessage* bm = (BMessage*)d;
	if (item->isPageNum()) {
		bm->AddString("l", item->Text());
		bm->AddInt32("p", item->getPageNum());
	}
	return NULL;
}

bool OutlinesView::GetBookmarks(BMessage* bm)
{
	if (fList == NULL) {
		return false;
	}
	fList->EachItemUnder(fUserDefined, true, store_bookmarks, bm);
	return true;
}

static BListItem* find_bookmark(BListItem* i, void* d)
{
	OutlineListItem* item = (OutlineListItem*)i;
	int pageNum = *(int*)d;
	if (item->isPageNum() && item->getPageNum() == pageNum)
		return i;
	return NULL;
}

OutlineListItem* OutlinesView::FindUserBookmark(int pageNum)
{
	if (fList == NULL) {
		return NULL;
	}
	return (OutlineListItem*)fList->EachItemUnder(fUserDefined, true, find_bookmark, &pageNum);
}

bool OutlinesView::HasUserBookmark(int pageNum)
{
	return fBookmark.IsSet(pageNum);
}

bool OutlinesView::IsUserBMSelected()
{
	if (fNeedsUpdate)
		return false;
	int i = fList->CurrentSelection(0);
	if (i >= 0) {
		OutlineListItem* item = (OutlineListItem*)fList->ItemAt(i);
		return item->isPageNum();
	}
	return false;
}

void OutlinesView::AddUserBookmark(int pageNum, const char* label)
{
	RemoveUserBookmark(pageNum);
	if (fList->CountItemsUnder(fUserDefined, true) == 1) {
		fList->RemoveItem(fEmptyUserBM);
	}
	OutlineListItem* item;
	int32 i = 0;
	int32 index = fList->FullListIndexOf(fUserDefined) + 1;
	item = (OutlineListItem*)fList->ItemUnderAt(fUserDefined, true, i);
	while (item != NULL) {
		if (item->isPageNum() && item->getPageNum() > pageNum) {
			// insert new OutlineListItem before item
			break;
		}
		i++;
		item = (OutlineListItem*)fList->ItemUnderAt(fUserDefined, true, i);
	}
	index += i;
	OutlineListItem* n = new OutlineListItem(label, 1, true, GetDefaultStyle());
	n->SetPageNum(pageNum);
	fList->AddItem(n, index);
	fBookmark.Set(pageNum, true);
}

void OutlinesView::RemoveUserBookmark(int pageNum)
{
	Activate();
	OutlineListItem* item;
	int32 i = 0;
	item = (OutlineListItem*)fList->ItemUnderAt(fUserDefined, true, i);
	while (item != NULL) {
		if (item->isPageNum() && item->getPageNum() == pageNum) {
			// remove item
			fList->RemoveItem(item);
			delete item;
			fBookmark.Set(pageNum, false);
			break;
		}
		i++;
		item = (OutlineListItem*)fList->ItemUnderAt(fUserDefined, true, i);
	}
	if (fList->CountItemsUnder(fUserDefined, true) == 0) {
		fList->AddItem(fEmptyUserBM);
	}
}

const char* OutlinesView::GetUserBMLabel(int pageNum)
{
	if (fNeedsUpdate)
		return NULL;
	OutlineListItem* item = FindUserBookmark(pageNum);
	if (item)
		return item->Text();
	return NULL;
}

void OutlinesView::MessageReceived(BMessage* msg)
{
	if (msg->what == 'Outl') {
		// get first selected item
		int32 selected = fList->CurrentSelection(0);
		if (selected >= 0) {
			bool msgSent = false;
			OutlineListItem* item = (OutlineListItem*)fList->ItemAt(selected);
			if (item) {
				LinkDest* link = NULL;
				bool deleteLink = false;
				if (item->isDest()) {
					link = item->getDest();
				}
				if (link != NULL) {
					// XXX: race condition: Link handled after a new pdf document has been loaded.
					// Should add a field to the message that represents the current document,
					// to check in the handler of this message if still contains a vaild pointer.
					BMessage msg(DEST_NOTIFY);
					msg.AddPointer("dest", link);
					fLooper->PostMessage(&msg);
					msgSent = true;
				} else if (item->isString()) {
					BMessage msg(STRING_NOTIFY);
					msg.AddString("string", item->getString()->c_str());
					fLooper->PostMessage(&msg);
					msgSent = true;
				} else if (link && link->isPageRef()) {
					BMessage msg(REF_NOTIFY);
					Ref r = link->getPageRef();
					int32 num = r.num, gen = r.gen;
					msg.AddInt32("num", num);
					msg.AddInt32("gen", gen);
					fLooper->PostMessage(&msg);
					msgSent = true;
				} else {
					int32 p = -1;
					if (item->isPageNum()) {
						p = item->getPageNum();
					} else if (link) {
						p = link->getPageNum();
					}
					if (p != -1) {
						BMessage msg(PAGE_NOTIFY);
						msg.AddInt32("page", p);
						fLooper->PostMessage(&msg);
						msgSent = true;
					}
				}
				if (deleteLink)
					delete link;
				if (!msgSent) {
					// notify window that state has changed
					BMessage msg(STATE_CHANGE_NOTIFY);
					fLooper->PostMessage(&msg);
				}
			}
		}
	} else {
		BView::MessageReceived(msg);
	}
}


// BookmarkWindow

BookmarkWindow::BookmarkWindow(int pageNum, const char* title, BRect aRect, BLooper* looper)
    : BWindow(aRect,
          B_TRANSLATE("Edit title for bookmark"),
          B_TITLED_WINDOW_LOOK,
          B_MODAL_APP_WINDOW_FEEL,
          B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fLooper = looper;
	fPageNum = pageNum;

	AddCommonFilter(new EscapeMessageFilter(this, B_QUIT_REQUESTED));

	// center window

	aRect.OffsetBy(aRect.Width() / 2, aRect.Height() / 2);
	float width = 300, height = 45;
	aRect.SetRightBottom(BPoint(aRect.left + width, aRect.top + height));
	aRect.OffsetBy(-aRect.Width() / 2, -aRect.Height() / 2);
	MoveTo(aRect.left, aRect.top);
	ResizeTo(width, height);

	fTitle = new BTextControl("fTitle", "", title, NULL);

	BButton* button = new BButton("button", B_TRANSLATE("OK"), new BMessage('OK'));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL).SetInsets(B_USE_WINDOW_INSETS).Add(fTitle).Add(button);

	SetDefaultButton(button);

	fTitle->MakeFocus();
	Show();
}


bool BookmarkWindow::QuitRequested()
{
	return true;
}

void BookmarkWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case 'OK': {
		// post message to application

		BMessage msg(BOOKMARK_ENTERED_NOTIFY);
		msg.AddString("label", fTitle->Text());
		msg.AddInt32("pageNum", fPageNum);
		fLooper->PostMessage(&msg, NULL);
		Quit();
		break;
	}
	default:
		BWindow::MessageReceived(msg);
	}
}
