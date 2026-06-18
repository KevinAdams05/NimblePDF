//========================================================================
//
// BeSplashOutputDev.h
//
// NimblePDF's Haiku-specific subclass of poppler's SplashOutputDev. Ported
// from xpdf XSplashOutputDev (Glyph & Cog, LLC, 2003) by Michael W.
// Pfeiffer (2004-2005). Moved into source/haiku/ and ported to poppler
// 25.12 during the NimblePDF migration (Kevin Adams, 2026).
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
//========================================================================

#ifndef BE_SPLASH_OUTPUT_DEV_H
#define BE_SPLASH_OUTPUT_DEV_H

#include <Bitmap.h>
#include <View.h>

#include <SplashOutputDev.h>
#include <TextOutputDev.h>
#include <splash/SplashTypes.h>


typedef void(rgb24_to_color_space)(uchar r, uchar g, uchar b, uchar* dest);
typedef void(gray8_to_color_space)(uchar gray, uchar* dest);

typedef void (*BeSplashOutRedrawCbk)(void* data, int left, int top, int right, int bottom,
	bool composited);

class BeSplashOutputDev : public SplashOutputDev {
public:
	enum ColorMode {
		kColorMode,
		kGrayScaleMode
	};

	BeSplashOutputDev(bool reverseVideoA, SplashColor paperColorA,
		bool incrementalUpdateA,
		BeSplashOutRedrawCbk redrawCbkA,
		void* redrawCbkDataA,
		ColorMode colorMode = kColorMode);

	~BeSplashOutputDev() override;

	// Start a page. Poppler's signature added an XRef* parameter.
	void startPage(int pageNum, GfxState* state, XRef* xref) override;

	// End a page.
	void endPage() override;

	// Dump page contents to display (no-op since the incremental-update
	// path has been broken since xpdf 4; left for source compatibility).
	void dump();

	// Update text state.
	void updateFont(GfxState* state) override;

	// Text drawing.
	void drawChar(GfxState* state, double x, double y,
		double dx, double dy,
		double originX, double originY,
		CharCode code, int nBytes, const Unicode* u, int uLen) override;
	bool beginType3Char(GfxState* state, double x, double y,
		double dx, double dy,
		CharCode code, const Unicode* u, int uLen) override;

	// Clear out the document (used when displaying an empty window).
	void clear();

	// Copy the rectangle (srcX, srcY, width, height) to (destX, destY)
	// in a BView.
	void redraw(int srcX, int srcY,
		BView* view,
		int destX, int destY,
		int width, int height,
		bool composited);

	// Find a string. Same semantics as xpdf's findText, forwarded to
	// poppler's TextPage. (Poppler's API adds a `wholeWord` parameter
	// that we always pass false for.)
	bool findText(Unicode* s, int len,
		bool startAtTop, bool stopAtBottom,
		bool startAtLast, bool stopAtLast,
		bool caseSensitive, bool backward,
		double* xMin, double* yMin,
		double* xMax, double* yMax);

	// Get the text inside the specified rectangle.
	GooString* getText(double xMin, double yMin, double xMax, double yMax);

	// Returns the text page and sets the member variable to NULL.
	TextPage* acquireText();

private:
	color_space getColorSpace();
	bool viewsSupportDrawBitmap(color_space cs);
	rgb24_to_color_space* getRGB24ToColorSpace(color_space cs);
	gray8_to_color_space* getGray8ToColorSpace(color_space cs);
	void blend24(uchar r, uchar g, uchar b, uchar alpha, uchar* dest);

	ColorMode fColorMode;
	bool fIncrementalUpdate; // incrementally update the display?
	BeSplashOutRedrawCbk fRedrawCallback;
	void* fRedrawCallbackData;
	TextPage* fText; // text from the current page
};


#endif  // BE_SPLASH_OUTPUT_DEV_H
