//========================================================================
//
// BeSplashOutputDev.cpp
//
// BePDF's Haiku-specific subclass of poppler's SplashOutputDev. Ported
// from xpdf XSplashOutputDev (Glyph & Cog, LLC, 2003) by Michael W.
// Pfeiffer (2004-2005). Moved into source/haiku/ and ported to poppler
// 25.12 during the NimblePDF migration (Kevin Adams, 2026).
//
//========================================================================

#include <utility>

#include <Screen.h>

#include <GfxState.h>
#include <Object.h>
#include <TextOutputDev.h>
#include <goo/gmem.h>
#include <splash/SplashBitmap.h>
#include <splash/SplashTypes.h>

#include "BeSplashOutputDev.h"


static SplashColorMode getSplashColorModeFor(BeSplashOutputDev::ColorMode mode)
{
	if (mode == BeSplashOutputDev::kColorMode) {
		return splashModeRGB8;
	}
	return splashModeMono8;
}


//------------------------------------------------------------------------
// BeSplashOutputDev
//------------------------------------------------------------------------

BeSplashOutputDev::BeSplashOutputDev(bool reverseVideoA,
	SplashColor paperColorA,
	bool incrementalUpdateA,
	BeSplashOutRedrawCbk redrawCbkA,
	void* redrawCbkDataA,
	ColorMode colorMode)
	: SplashOutputDev(getSplashColorModeFor(colorMode),
		1,
		reverseVideoA,
		paperColorA)
{
	fColorMode = colorMode;
	fIncrementalUpdate = false; // broken since XPDF 4
	fRedrawCallback = redrawCbkA;
	fRedrawCallbackData = redrawCbkDataA;

	// Create text object. Poppler's TextPage takes a simple bool
	// rawOrder flag instead of xpdf's TextOutputControl struct.
	fText = new TextPage(true /* rawOrder */);
}


BeSplashOutputDev::~BeSplashOutputDev()
{
	// Poppler's TextPage destructor is private; lifetime is ref-counted.
	// Calling decRefCnt drops our reference, which deletes the object
	// when the count reaches zero.
	fText->decRefCnt();
}


void BeSplashOutputDev::drawChar(GfxState* state, double x, double y,
	double dx, double dy,
	double originX, double originY,
	CharCode code, int nBytes, const Unicode* u, int uLen)
{
	fText->addChar(state, x, y, dx, dy, code, nBytes, u, uLen);
	SplashOutputDev::drawChar(state, x, y, dx, dy, originX, originY,
		code, nBytes, u, uLen);
}


bool BeSplashOutputDev::beginType3Char(GfxState* state, double x, double y,
	double dx, double dy,
	CharCode code, const Unicode* u, int uLen)
{
	return SplashOutputDev::beginType3Char(state, x, y, dx, dy, code, u, uLen);
}


void BeSplashOutputDev::clear()
{
	startDoc(NULL);
	startPage(0, NULL, NULL);
}


void BeSplashOutputDev::startPage(int pageNum, GfxState* state, XRef* xref)
{
	SplashOutputDev::startPage(pageNum, state, xref);
	fText->startPage(state);
}


void BeSplashOutputDev::endPage()
{
	SplashOutputDev::endPage();
	if (!fIncrementalUpdate) {
		(*fRedrawCallback)(fRedrawCallbackData, 0, 0, getBitmapWidth(), getBitmapHeight(), true);
	}
}


void BeSplashOutputDev::dump()
{
	// No-op. xpdf SplashOutputDev had getModRegion()/clearModRegion()
	// for incremental updates; poppler dropped those. The incremental
	// path has been disabled in BeSplashOutputDev since xpdf 4 anyway
	// (see fIncrementalUpdate = false in the constructor), so this
	// method has no effective work to do. Retained for source-level
	// compatibility with callers.
}


void BeSplashOutputDev::updateFont(GfxState* state)
{
	SplashOutputDev::updateFont(state);
	fText->updateFont(state);
}


// RGB24 Conversion Functions

static void rgb24_to_rgb24(uchar r, uchar g, uchar b, uchar* dest)
{
	dest[0] = b;
	dest[1] = g;
	dest[2] = r;
}


static void rgb24_to_rgb32(uchar r, uchar g, uchar b, uchar* dest)
{
	dest[0] = b;
	dest[1] = g;
	dest[2] = r;
	dest[3] = 0;
}


// GRAY8 Conversion Functions

static bool gIndexForGray8Initialized = false;
static uint8 gIndexForGray8[256];

static void initialzeIndexForGray8()
{
	if (gIndexForGray8Initialized) {
		return;
	}

	BScreen screen;
	if (!screen.IsValid()) {
		return;
	}

	for (int16 i = 0; i <= 255; i++) {
		gIndexForGray8[i] = screen.IndexForColor(i, i, i);
	}

	gIndexForGray8Initialized = true;
}


static void gray8_to_gray8(uchar gray, uchar* dest)
{
	dest[8] = gray;
}


static void gray8_to_cmap8(uchar gray, uchar* dest)
{
	dest[0] = gIndexForGray8[gray];
}


static void gray8_to_rgb24(uchar gray, uchar* dest)
{
	dest[0] = dest[1] = dest[2] = gray;
}


static void gray8_to_rgb32(uchar gray, uchar* dest)
{
	dest[0] = dest[1] = dest[2] = gray;
	dest[3] = 0;
}


void BeSplashOutputDev::redraw(int srcX, int srcY,
	BView* view,
	int destX, int destY,
	int width, int height,
	bool composited)
{
	SplashColor color;
	int bw, x, y;
	uchar* destRow;
	uchar* dest;
	int32 destBPR;
	int32 destBPP; // bytes per pixel
	rgb24_to_color_space* rgb24ToCS = NULL;
	gray8_to_color_space* gray8ToCS = NULL;
	SplashBitmap* splashBitmap = getBitmap();
	unsigned char* ap = splashBitmap->getAlphaPtr();

	if (splashBitmap->getWidth() < srcX + width) {
		width = splashBitmap->getWidth() - srcX;
	}
	if (splashBitmap->getHeight() < srcY + height) {
		height = splashBitmap->getHeight() - srcY;
	}

	// TODO reuse bitmap?
	color_space cs = getColorSpace();
	size_t pixel_chunk;
	size_t row_alignment;
	size_t pixels_per_chunk;
	if (get_pixel_size_for(cs, &pixel_chunk, &row_alignment, &pixels_per_chunk) != B_OK) {
		// should not reach here!
		// TODO Fill
		return;
	}
	// we support color space with at least one byte per pixel only
	destBPP = pixel_chunk / pixels_per_chunk;

	rgb24ToCS = getRGB24ToColorSpace(cs);
	gray8ToCS = getGray8ToColorSpace(cs);

	BBitmap* bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), cs);
	if (bitmap == NULL) {
		// TODO Fill
		return;
	}
	if (bitmap->InitCheck() != B_OK || !bitmap->IsValid()) {
		// TODO Fill
		delete bitmap;
		return;
	}
	destRow = (uchar*)bitmap->Bits();
	destBPR = bitmap->BytesPerRow();

	bw = splashBitmap->getWidth();

	height += srcY;
	width += srcX;

	for (y = srcY; y < height; ++y, destRow += destBPR) {
		dest = destRow;
		if (fColorMode == kColorMode) {
			if (ap == NULL) {
				for (x = srcX; x < width; ++x, dest += destBPP) {
					splashBitmap->getPixel(x, y, color);
					rgb24ToCS(splashRGB8R(color), splashRGB8G(color), splashRGB8B(color), dest);
				}
			} else {
				for (x = srcX; x < width; ++x, dest += destBPP) {
					splashBitmap->getPixel(x, y, color);
					blend24(splashRGB8R(color), splashRGB8G(color), splashRGB8B(color),
						splashBitmap->getAlpha(x, y),
						dest);
				}
			}
		} else {
			for (x = srcX; x < width; ++x, dest += destBPP) {
				splashBitmap->getPixel(x, y, color);
				gray8ToCS(*color, dest);
			}
		}
	}

	view->DrawBitmap(bitmap,
		BRect(0, 0, width - 1, height - 1),
		BRect(destX, destY, destX + width - 1, destY + height - 1));

	delete bitmap;
}


color_space BeSplashOutputDev::getColorSpace()
{
	if (fColorMode == kGrayScaleMode) {
		if (viewsSupportDrawBitmap(B_GRAY8)) {
			// Not supported by BeOS R5!
			return B_GRAY8;
		}
		if (viewsSupportDrawBitmap(B_CMAP8)) {
			return B_CMAP8;
		}
	}

	if (viewsSupportDrawBitmap(B_RGB24)) {
		// Not supported by BeOS R5!
		return B_RGB24;
	}

	// fall-back
	return B_RGB32;
}


rgb24_to_color_space* BeSplashOutputDev::getRGB24ToColorSpace(color_space cs)
{
	switch (cs) {
		case B_RGB24:
			return rgb24_to_rgb24;
		case B_RGB32:
			return rgb24_to_rgb32;
		default:
			return NULL;
	}
}


gray8_to_color_space* BeSplashOutputDev::getGray8ToColorSpace(color_space cs)
{
	switch (cs) {
		case B_CMAP8:
			initialzeIndexForGray8();
			return gray8_to_cmap8;
		case B_GRAY8:
			return gray8_to_gray8;
		case B_RGB24:
			return gray8_to_rgb24;
		case B_RGB32:
			return gray8_to_rgb32;
		default:
			return NULL;
	}
}


bool BeSplashOutputDev::viewsSupportDrawBitmap(color_space cs)
{
	uint32 flags = 0;
	return bitmaps_support_space(cs, &flags)
		&& ((flags & B_VIEWS_SUPPORT_DRAW_BITMAP) != 0);
}


void BeSplashOutputDev::blend24(uchar r, uchar g, uchar b, uchar alpha, uchar* dest)
{
	int a = alpha;
	int a1 = 255 - alpha;
	dest[0] = (b * a + 255 * a1) / 255;
	dest[1] = (g * a + 255 * a1) / 255;
	dest[2] = (r * a + 255 * a1) / 255;
}


bool BeSplashOutputDev::findText(Unicode* s, int len,
	bool startAtTop, bool stopAtBottom,
	bool startAtLast, bool stopAtLast,
	bool caseSensitive, bool backward,
	double* xMin, double* yMin,
	double* xMax, double* yMax)
{
	// Poppler's findText adds a `wholeWord` parameter after `backward`;
	// we always pass false (xpdf had no equivalent, and BePDF never
	// exposed a whole-word search option in the UI).
	return fText->findText(s, len, startAtTop, stopAtBottom,
		startAtLast, stopAtLast,
		caseSensitive, backward,
		false /* wholeWord */,
		xMin, yMin, xMax, yMax);
}


GooString* BeSplashOutputDev::getText(double xMin, double yMin, double xMax, double yMax)
{
	// Poppler's TextPage::getText returns GooString by value and requires
	// an EndOfLineKind parameter; wrap the returned value into a
	// heap-allocated GooString via the move constructor. Callers
	// continue to own the returned pointer.
	return new GooString(std::move(fText->getText(xMin, yMin, xMax, yMax, eolUnix)));
}


TextPage* BeSplashOutputDev::acquireText()
{
	TextPage* textPage = fText;
	fText = new TextPage(true /* rawOrder */);
	return textPage;
}
