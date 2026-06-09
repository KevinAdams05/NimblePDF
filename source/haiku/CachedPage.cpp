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

#include "CachedPage.h"

/////////////////////////////////////////////////////////////////////////
CachedPage::CachedPage()
    : fState(EMPTY),
      fBitmap(NULL),
      fText(NULL),
      fLinks(NULL),
      fAnnotations(NULL)
{}

CachedPage::~CachedPage()
{
	delete fBitmap;
	// poppler TextPage has a private dtor and is reference-counted.
	if (fText)
		fText->decRefCnt();
	delete fLinks;
}

/////////////////////////////////////////////////////////////////////////
void CachedPage::InitCTM(OutputDev* outputDev)
{
	for (int i = 0; i < 6; i++)
		fCtm[i] = outputDev->getDefCTM()[i];
	for (int i = 0; i < 6; i++)
		fIctm[i] = outputDev->getDefICTM()[i];
}

void CachedPage::CvtDevToUser(int dx, int dy, double* ux, double* uy)
{
	*ux = fIctm[0] * dx + fIctm[2] * dy + fIctm[4];
	*uy = fIctm[1] * dx + fIctm[3] * dy + fIctm[5];
}

void CachedPage::CvtUserToDev(double ux, double uy, int* dx, int* dy)
{
	*dx = (int)(fCtm[0] * ux + fCtm[2] * uy + fCtm[4] + 0.5);
	*dy = (int)(fCtm[1] * ux + fCtm[3] * uy + fCtm[5] + 0.5);
}

/////////////////////////////////////////////////////////////////////////
void CachedPage::SetLinks(Links* links)
{
	// ASSERT(fLinks == NULL);
	fLinks = links;
}

// poppler 25.12: Links dropped find()/onLink(); it only exposes getLinks().
// Hit-test the point against each AnnotLink's rect (poppler normalises annot
// rects in the Annot ctor, so x1<=x2 / y1<=y2 holds).
LinkAction* CachedPage::FindLink(double x, double y)
{
	if (fLinks == NULL)
		return NULL;
	for (const auto& link : fLinks->getLinks()) {
		double x1, y1, x2, y2;
		link->getRect(&x1, &y1, &x2, &y2);
		if (x >= x1 && x <= x2 && y >= y1 && y <= y2)
			return link->getAction();
	}
	return NULL;
}

bool CachedPage::OnLink(double x, double y)
{
	if (fLinks == NULL)
		return false;
	for (const auto& link : fLinks->getLinks()) {
		double x1, y1, x2, y2;
		link->getRect(&x1, &y1, &x2, &y2);
		if (x >= x1 && x <= x2 && y >= y1 && y <= y2)
			return true;
	}
	return false;
}

void CachedPage::SetText(TextPage* text)
{
	// ASSERT(fText == NULL);
	fText = text;
}

bool CachedPage::FindText(Unicode* s,
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
    double* yMax)
{
	if (fText
	    && fText->findText(s,
	        len,
	        startAtTop,
	        stopAtBottom,
	        startAtLast,
	        stopAtLast,
	        caseSensitive,
	        backward,
	        false, // wordwise -- TODO/FIXME
	        xMin,
	        yMin,
	        xMax,
	        yMax)) {
		return true;
	}
	return false;
}

GooString* CachedPage::GetText(int xMin, int yMin, int xMax, int yMax)
{
	if (fText) {
		// poppler 25.12: TextPage::getText returns GooString by value and
		// takes an EndOfLineKind. Wrap a heap copy to keep the GooString*
		// contract callers expect.
		return new GooString(fText->getText((double)xMin, (double)yMin,
			(double)xMax, (double)yMax, eolUnix));
	} else {
		return NULL;
	}
}

/////////////////////////////////////////////////////////////////////////
void CachedPage::SetBitmap(BBitmap* bitmap, int32 width, int32 height)
{
	fBitmap = bitmap;
	fWidth = width;
	fHeight = height;
}

void CachedPage::MakeEmpty()
{
	delete fLinks;
	fLinks = NULL;
	if (fText)
		fText->decRefCnt();
	fText = NULL;
	// don't delete fBitmap
}
