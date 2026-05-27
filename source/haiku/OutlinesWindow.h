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

#ifndef OUTLINES_WINDOW_H
#define OUTLINES_WINDOW_H

// xpdf
#include <PDFDoc.h>
#include <XRef.h>
// BeOS
#include <Font.h>
#include <Looper.h>
#include <List.h>
#include <SupportDefs.h>

#include "LayoutUtils.h" // for Bitset
#include "Settings.h"

class OutlineStyle {
	const BFont* fFont;
	rgb_color fColor;

public:
	OutlineStyle(const BFont* font, rgb_color color);
	const BFont* GetFont() const { return fFont; }
	const rgb_color* GetColor() const { return &fColor; }
};

class OutlineStyleList {
	BList fList; // of OutlineStyle
	BFont fFonts[4];

	static void Initialize();

public:
	OutlineStyleList();
	~OutlineStyleList();

	enum { PLAIN_STYLE, BOLD_STYLE, ITALIC_STYLE, BOLD_ITALIC_STYLE };

	const BFont* GetFont(int style) const;
	OutlineStyle* GetStyle(int style, rgb_color color);
	OutlineStyle* GetDefaultStyle();
};

class OutlineListItem : public BListItem {
	BString fString;
	enum { linkDest, linkPageNum, linkString, linkUndefined } fType;
	union {
		LinkDest* dest;
		GooString* string;
		int pageNum;
	} fLink;
	OutlineStyle* fStyle;

public:
	OutlineListItem(const char* string, uint32 level, bool expanded, OutlineStyle* style);
	virtual ~OutlineListItem();
	const char* Text() const { return fString.String(); }
	void SetStyle(OutlineStyle* style) { fStyle = style; }
	void SetLink(LinkDest* dest);
	void SetLink(GooString* s);
	void SetPageNum(int pageNum);

	void DrawItem(BView* owner, BRect frame, bool complete);

	bool isDest() const { return fType == linkDest; }
	bool isString() const { return fType == linkString; }
	bool isPageNum() const { return fType == linkPageNum; }
	LinkDest* getDest() const { return fLink.dest; }
	GooString* getString() const { return fLink.string; }
	int getPageNum() const { return fLink.pageNum; }
};

class OutlinesView : public BScrollView {
	BLooper* fLooper;
	OutlineStyleList fOutlineStyleList;
	BOutlineListView* fList;
	Catalog* fCatalog;
	BMessage* fBookmarks; // archived bookmarks
	bool fNeedsUpdate;
	OutlineListItem* fUserDefined;
	OutlineListItem* fEmptyUserBM; // cached value
	Bitset fBookmark;

	void ReadOutlines(Object* o, uint32 level);
	OutlineListItem* FindUserBookmark(int pageNum);
	void InsertUserBookmark(int pageNum, const char* label);
	void InitUserBookmarks(bool initOnly);
	OutlineStyle* GetDefaultStyle() { return fOutlineStyleList.GetDefaultStyle(); }

public:
	// message sent to fLooper has this fields:
	enum {
		// what                          attribute(s):
		PAGE_NOTIFY = 'OWPg',   // "page"
		REF_NOTIFY = 'OWRf',    // "num", "gen"
		STRING_NOTIFY = 'OWSt', // "string"
		DEST_NOTIFY = 'OWDt',   // "dest" pointer to LinkDest
		QUIT_NOTIFY = 'ORQt',
		STATE_CHANGE_NOTIFY = 'OWCg'
	};

	OutlinesView(Catalog* catalog, BMessage* bookmarks, GlobalSettings* settings, BLooper* looper, uint32 flags);
	~OutlinesView();
	void AttachedToWindow();
	void MessageReceived(BMessage* msg);

	void SetCatalog(Catalog* catalog, BMessage* bookmarks);
	bool HasUserBookmark(int pageNum);
	bool IsUserBMSelected();
	const char* GetUserBMLabel(int pageNum);
	void AddUserBookmark(int pageNum, const char* label);
	void RemoveUserBookmark(int pageNum);
	// fills BMessage with bookmarks to be stored in FileAttributes
	bool GetBookmarks(BMessage* bookmarks);

	void Activate();

	// True once Activate() has populated the list from the bookmarks
	// attribute. Used by PDFWindow::StoreFileAttributes() to avoid
	// overwriting saved bookmarks with an empty list when the panel
	// was never opened during the session. See BePDF issue #115.
	bool WasActivated() const { return !fNeedsUpdate; }
};

class BTextControl;

class BookmarkWindow : public BWindow {
public:
	BookmarkWindow(int pageNum, const char* title, BRect rect, BLooper* looper);
	void MessageReceived(BMessage* msg);
	bool QuitRequested();

	enum { BOOKMARK_ENTERED_NOTIFY = 'BMEt' };

protected:
	BLooper* fLooper;
	BTextControl* fTitle;
	int fPageNum;
};


#endif
