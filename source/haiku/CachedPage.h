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

#ifndef CACHED_PAGE_H
#define CACHED_PAGE_H

// xpdf
#include <XRef.h>
#include <Annot.h>
#include <Link.h>
#include <TextOutputDev.h>
#include <PDFDoc.h>
#include "Annotation.h"
// BeOS
#include <Bitmap.h>
#include <SupportDefs.h>

class PageCache;

class CachedPage {
public:
	CachedPage();
	virtual ~CachedPage();

	enum State {
		EMPTY = 0,     // Page
		WAITING = 1,   // Waits to be rendered
		RENDERING = 2, // Is rendering
		READY = 3      // Has been rendered
	};

	enum State GetState() const { return fState; }
	// Is this page displayed?
	bool GetDisplayed() const { return fDisplayed; }

	bool FindText(Unicode* s,
	    int len,
	    bool startAtTop,
	    bool stopAtBottom,
	    bool startAtLast,
	    bool stopAtLast,
	    bool caseSensitive,
	    bool backward,
	    double* xMin,
	    double* yMin,
	    double* xMax,
	    double* yMax);
	GooString* GetText(int xMin, int yMin, int xMax, int yMax);

	// If point <x>,<y> is in a link, return the associated action;
	// else return NULL.
	LinkAction* FindLink(double x, double y);

	// Return true if <x>,<y> is in a link.
	bool OnLink(double x, double y);

	void CvtDevToUser(int dx, int dy, double* ux, double* uy);
	void CvtUserToDev(double ux, double uy, int* dx, int* dy);

	BBitmap* GetBitmap() const { return fBitmap; }
	int32 GetWidth() const { return fWidth; }
	int32 GetHeight() const { return fHeight; }

	float GetDeltaX() { return fGSdeltaX; }
	float GetDeltaY() { return fGSdeltaY; }

	void SetAnnotations(Annotations* a) { fAnnotations = a; }
	Annotations* GetAnnotations() { return fAnnotations; }

	friend class PageCache;
	friend class PageRenderer;
	friend int gsdll_callback(int message, char* str, unsigned long count);

	double* GetCTM() { return fCtm; }

protected:
	void MakeEmpty();
	void SetState(enum State state) { fState = state; };
	void SetDisplayed(bool displayed) { fDisplayed = displayed; };
	void SetText(TextPage* text);
	void SetLinks(Links* links);
	void InitCTM(OutputDev* outputDev);
	void SetBitmap(BBitmap* bitmap, int32 width, int32 height);
	void SetBitmapSize(int32 width, int32 height)
	{
		fWidth = width;
		fHeight = height;
	};
	enum State fState;
	bool fDisplayed;

	BBitmap* fBitmap;
	int32 fWidth, fHeight;
	int32 fPage, fZoom, fRotation;
	// correction for Ghostscript
	double fGSdeltaX, fGSdeltaY;
	// from BeOutputDev
	TextPage* fText;
	// from PDFDoc
	Links* fLinks;
	// from OutputDev
	double fCtm[6], fIctm[6];
	Annotations* fAnnotations;
};

#endif
