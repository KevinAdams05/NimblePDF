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

// AnnotationRenderer
#include "AnnotationRenderer.h"
#include "AnnotWriter.h"
#include "TextConversion.h"

#include <ctype.h>

#include <Bitmap.h>
#include <Polygon.h>
#include <Picture.h>

#include "BepdfApplication.h"

// Font conversion
#include "BeFontEncoding.h"

//------------------------------------------------------------------------
// Font map (pdf standard font -> be default installed font)
//------------------------------------------------------------------------

struct FontMapEntry {
	char* pdfFont;
	char* beFontFamily;
	char* beFontStyle;
};

/////////////////////////////////////////////////////////////////////////
// index: {symbolic:12, fixed:8, serif:4, sans-serif:0} + bold*2 + italic
static FontMapEntry fontMap[] = {{"Helvetica", "Swis721 BT", "Roman"},
    {"Helvetica-Oblique", "Swis721 BT", "Italic"},
    {"Helvetica-Bold", "Swis721 BT", "Bold"},
    {"Helvetica-BoldOblique", "Swis721 BT", "Bold Italic"},
    {"Times-Roman", "Dutch801 Rm BT", "Roman"},
    {"Times-Italic", "Dutch801 Rm BT", "Italic"},
    {"Times-Bold", "Dutch801 Rm BT", "Bold"},
    {"Times-BoldItalic", "Dutch801 Rm BT", "Bold Italic"},
    {"Courier", "Courier10 BT", "Roman"},
    {"Courier-Oblique", "Courier10 BT", "Italic"},
    {"Courier-Bold", "Courier10 BT", "Bold"},
    {"Courier-BoldOblique", "Courier10 BT", "Bold Italic"},
    {"Symbol", "SymbolProp BT", "Regular"},
    {"ZapfDingbats", "Dingbats", ""},
    {NULL}};


bool StandardFontToBeFont(const char* stdFont, const char** family, const char** style)
{
	FontMapEntry* entry;
	for (entry = fontMap; entry->pdfFont != NULL; entry++) {
		if (strcmp(entry->pdfFont, stdFont) == 0) {
			*family = entry->beFontFamily;
			*style = entry->beFontStyle;
			return true;
		}
	}
	return false;
}

AnnotationRenderer::ClipToRect::ClipToRect(AnnotationRenderer* r, Annotation* a)
    : fRenderer(r),
      fAnnot(a),
      fRect(r->ToRect(a->GetRect()))
{
	BRect clip = fRect;
	View()->GetClippingRegion(&fOldClippingRegion);
	BRegion region(clip);
	View()->ConstrainClippingRegion(&region);
}

AnnotationRenderer::ClipToRect::~ClipToRect()
{
	fRenderer->DrawRect(fAnnot, fRect);
	View()->ConstrainClippingRegion(&fOldClippingRegion);
}

float AnnotationRenderer::CvtUserToDev(float f)
{
	const float kMinSize = 1.0 / 32.0;
	float size = f * (float)fZoom / 72.0;
	if (size < kMinSize)
		size = kMinSize;
	return size;
}

void AnnotationRenderer::CvtUserToDev(double ux, double uy, int* dx, int* dy)
{
	*dx = (int)(fCtm[0] * ux + fCtm[2] * uy + fCtm[4] + 0.5);
	*dy = (int)(fCtm[1] * ux + fCtm[3] * uy + fCtm[5] + 0.5);
}

void AnnotationRenderer::CvtUserToDev(PDFPoint* u, BPoint* d, int n)
{
	int x, y;
	for (int i = 0; i < n; i++) {
		CvtUserToDev(u[i].x, u[i].y, &x, &y);
		d[i].x = x;
		d[i].y = y;
	}
}

rgb_color AnnotationRenderer::GetColor(GfxRGB* c, double opacity)
{
	rgb_color rgb;
	rgb.alpha = uint8(opacity * 255);
	rgb.red = uint8(colToDbl(c->r) * 255);
	rgb.green = uint8(colToDbl(c->g) * 255);
	rgb.blue = uint8(colToDbl(c->b) * 255);
	return rgb;
}

BPoint AnnotationRenderer::PointBetween(BPoint p1, BPoint p2, float f)
{
	BPoint p(p2.x - p1.x, p2.y - p1.y);
	p.x *= f;
	p.y *= f;
	p.x += p1.x;
	p.y += p1.y;
	return p;
}

void AnnotationRenderer::SetPenSize(int w)
{
	fView->SetPenSize(CvtUserToDev(w));
}

AnnotationRenderer::AnnotationRenderer(BView* v, double* ctm, int zoom, bool edit)
    : fView(v),
      fZoom(zoom),
      fEdit(edit)
{
	for (int i = 0; i < 6; i++)
		fCtm[i] = ctm[i];

	fDrawingMode = fView->DrawingMode();
	fView->GetBlendingMode(&fSourceAlpha, &fAlphaFunction);

	fView->SetDrawingMode(B_OP_ALPHA);
	fView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
}

AnnotationRenderer::~AnnotationRenderer()
{
	fView->SetDrawingMode(fDrawingMode);
	fView->SetBlendingMode(fSourceAlpha, fAlphaFunction);
}

BRect AnnotationRenderer::ToRect(PDFRectangle* g)
{
	// Note: Keep in sync with PDFView::CvtUserToDev()
	BRect r;
	int x, y;
	CvtUserToDev(g->x1, g->y1, &x, &y);
	r.top = r.bottom = y;
	r.right = r.left = x;
	CvtUserToDev(g->x2, g->y2, &x, &y);
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
	return r;
}

// Replace red pixels in src by "color" and return new bitmap
// Caller is responsible for deleting the bitmap
BBitmap* AnnotationRenderer::ColorBitmap(BBitmap* src, rgb_color color)
{
	// copy bitmap
	BView view(src->Bounds(), "", 0, B_WILL_DRAW);
	BBitmap* dst = new BBitmap(src->Bounds(), B_RGBA32, true);
	dst->Lock();
	dst->AddChild(&view);
	view.DrawBitmap(src, BPoint(0, 0));
	view.Sync();
	view.RemoveSelf();
	dst->Unlock();

	const int w = src->Bounds().IntegerWidth() + 1;
	const int h = src->Bounds().IntegerHeight() + 1;
	char* d = (char*)dst->Bits();

	// Hope this also works on PPC!
	rgb_color r = {0, 0, 255, 255};
	uint32 red = *(uint32*)&r;
	rgb_color n = {color.blue, color.green, color.red, color.alpha};
	uint32 new_color = *(uint32*)&n;
	for (int y = 0; y < h; y++) {
		uint32* p = (uint32*)d;
		for (int x = 0; x < w; x++) {
			if (*p == red)
				*p = new_color;
			p++;
		}
		d += dst->BytesPerRow();
	}

	return dst;
}

void AnnotationRenderer::DrawBitmap(BBitmap* image, BPoint p)
{
	if (image) {
		BRect destRect;
		destRect.left = p.x;
		destRect.top = p.y;
		destRect.right = p.x + CvtUserToDev(image->Bounds().Width() + 1.0) - 1.0;
		destRect.bottom = p.y + CvtUserToDev(image->Bounds().Height() + 1.0) - 1.0;
		image = ColorBitmap(image, fView->HighColor());
		fView->DrawBitmap(image, destRect);
		fView->Sync();
		delete image;
	}
}

void AnnotationRenderer::DrawRect(Annotation* a, BRect r)
{
	if (fEdit && CanWrite(a)) {
		fView->SetHighColor(0, 0, 0);
		fView->SetLowColor(B_TRANSPARENT_COLOR);
		fView->SetPenSize(1.0);
		drawing_mode m = fView->DrawingMode();
		fView->SetDrawingMode(B_OP_OVER);
		fView->StrokeRect(r, B_MIXED_COLORS);
		r.left = r.right - 5;
		r.top = r.bottom - 5;
		fView->FillRect(r, B_MIXED_COLORS);
		fView->SetDrawingMode(m);
	}
}


bool AnnotationRenderer::DrawAnnotation(Annotation* a)
{
	// return a->HasAppearanceStream();
	return !a->IsDeleted();
}


void AnnotationRenderer::DoText(TextAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	PDFRectangle* r = a->GetRect();
	BRect t = ToRect(r);
	fView->SetHighColor(GetColor(a->GetColor(), a->GetOpacity()));
	//	SetPenSize(1);
	//	fView->StrokeRect(t);
	BBitmap* image = gApp->GetTextAnnotImage((int)a->GetType());
	DrawBitmap(image, BPoint(t.left, t.top));
}

void AnnotationRenderer::DoLink(LinkAnnot* a)
{
	//	if (!DrawAnnotation(a)) return;
	//	ClipToRect clip(this, a);
}

// declared in BeOutputFont.h:
extern bool StandardFontToBeFont(const char* stdFont, const char** family, const char** style);
// alternative: include "BeOutputFont.h" and put xpdf into search path

class LineParser {
	BString* fText;
	const char* fString;
	BFont fFont;
	float fWidth;

	int fPos;
	BString fLine;

	int SkipWhiteSpaces(int pos);
	int SkipNonWhiteSpaces(int pos);

public:
	LineParser(BString* text, BFont font, float width);
	const char* NextLine();
};

LineParser::LineParser(BString* text, BFont font, float width)
    : fText(text),
      fFont(font),
      fWidth(width),
      fPos(0)
{
	fString = fText->String();
	fPos = SkipWhiteSpaces(0);
}

int LineParser::SkipWhiteSpaces(int pos)
{
	while (isspace(fString[pos]))
		pos++;
	return pos;
}

int LineParser::SkipNonWhiteSpaces(int pos)
{
	while (fString[pos] != 0 && !isspace(fString[pos]))
		pos++;
	return pos;
}

const char* LineParser::NextLine()
{
	int afterLine, nextWord, afterNextWord;
	float width;
	if (fPos < fText->Length()) {
		afterLine = fPos;
		nextWord = fPos;
		for (;;) {
			afterNextWord = SkipNonWhiteSpaces(nextWord);
			if (nextWord == afterNextWord) { // at end of text
				if (afterLine != fPos) {
					fLine.SetTo(&fString[fPos], afterLine - fPos);
					fPos = nextWord;
					return fLine.String();
				} else {
					return NULL; // should not reach here anyway
				}
			}

			width = fFont.StringWidth(&fString[fPos], afterNextWord - fPos);

			if (fWidth > width) { // inside, try to append next word
				nextWord = SkipWhiteSpaces(afterNextWord);
				afterLine = afterNextWord;
			} else {                     // outside
				if (fPos == afterLine) { // return first word
					fLine.SetTo(&fString[fPos], afterNextWord - fPos);
					// move to next word
					fPos = SkipWhiteSpaces(afterNextWord);
					return fLine.String();
				} else { // return string
					fLine.SetTo(&fString[fPos], afterLine - fPos);
					fPos = nextWord;
					return fLine.String();
				}
			}
		};
	}
	return NULL;
}

// Returns: value >= 0 inside bounds; value < 0 outside bounds
float AnnotationRenderer::LayoutText(BString* text, BFont font, free_text_justification justification, BRect bounds, bool draw)
{
	const char* line;
	float y, x, prevY;
	float lineHeight, lineWidth, width;
	font_height height;

	font.GetHeight(&height);
	y = height.ascent + bounds.top; // draw from top to bottom
	prevY = y;
	lineWidth = bounds.Width() + 1;
	lineHeight = height.ascent + height.descent + height.leading;

	LineParser lp(text, font, lineWidth);
	while ((line = lp.NextLine()) != NULL) {
		if (draw) {
			width = font.StringWidth(line);

			// calculate x for proper justification
			switch (justification) {
			default:
			case left_justify:
				x = bounds.left;
				break;
			case right_justify:
				x = bounds.left + lineWidth - width;
				break;
			case centered:
				x = bounds.left + (lineWidth - width) / 2.0;
				break;
			}

			fView->DrawString(line, BPoint(x, y));
		}
		prevY = y;
		y += lineHeight;
	}
	return bounds.bottom - prevY - height.descent;
}

void AnnotationRenderer::DoFreeText(FreeTextAnnot* a)
{
	const char *family, *style;
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);
	BRect rect(ToRect(a->GetRect()));

	if (a->HasColor()) {
		// fill background
		fView->SetHighColor(GetColor(a->GetColor()));
		fView->FillRect(rect);
	}

	fView->SetHighColor(GetColor(a->GetFontColor()));

	if (a->GetBorderStyle()->GetWidth() > 0) {
		// draw border
		fView->SetPenSize(CvtUserToDev(a->GetBorderStyle()->GetWidth()));
		fView->StrokeRect(rect);
	}

	// set font
	BFont font(be_plain_font);
	if (StandardFontToBeFont(a->GetFont()->GetName(), &family, &style)) {
		font.SetFamilyAndStyle(family, style);
	}

	BString* text = TextToUtf8(a->GetContents()->c_str(), a->GetContents()->size());

	// calculate font size
	float size;
	if (a->GetFontSize() == 0) { // automatic
		// unoptimized version:
		/*
		size = CvtUserToDev(6);
		for (i = 6; i < 96; i ++) {
			s = CvtUserToDev(i);
			font.SetSize(s);
			h = LayoutText(text, font, a->GetJustification(), rect, false);
			if (h < 0) { // text (height) reaches outside bounding box
				break;
			}
			size = s;
		}
		*/

		int low, hi, i;
		float s, h;
		size = CvtUserToDev(6);
		low = 6;
		hi = 96;
		// find largest font size where text (height) fits into bounding box
		while (low < hi) {
			i = low + (hi - low) / 2;
			s = CvtUserToDev(i);
			font.SetSize(s);
			h = LayoutText(text, font, a->GetJustification(), rect, false);
			if (h < 0) {
				hi = i - 1;
			} else if (h > 0) {
				low = i + 1; // i could be proper font size, see correction below
			} else {
				break;
			}
		}
		// off by one correction:
		i = low + (hi - low) / 2;
		if (i > 6) { // font size must be greater than 5!
			size = CvtUserToDev(i);
			font.SetSize(size);
			h = LayoutText(text, font, a->GetJustification(), rect, false);
			if (h < 0) {
				size = CvtUserToDev(i - 1);
			}
		}
	} else {
		size = CvtUserToDev(a->GetFontSize());
	}
	font.SetSize(size);
	fView->SetFont(&font);

	if (text) {
		LayoutText(text, font, a->GetJustification(), rect, true);
	}
	delete text;
}

void AnnotationRenderer::DoLine(LineAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	BPoint line[2];
	CvtUserToDev(a->GetLine(), line, 2);
	fView->SetHighColor(GetColor(a->GetColor(), 1));
	SetPenSize(a->GetBorderStyle()->GetWidth());
	fView->StrokeLine(line[0], line[1]);
}

void AnnotationRenderer::DoSquare(SquareAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	PDFRectangle* r = a->GetRect();
	BRect t = ToRect(r);
	fView->SetHighColor(GetColor(a->GetColor(), a->GetOpacity()));
	SetPenSize(a->GetBorderStyle()->GetWidth());
	fView->StrokeRect(t);
}

void AnnotationRenderer::DoCircle(CircleAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	PDFRectangle* r = a->GetRect();
	BRect t = ToRect(r);
	fView->SetHighColor(GetColor(a->GetColor(), a->GetOpacity()));
	SetPenSize(a->GetBorderStyle()->GetWidth());
	fView->StrokeEllipse(t);
}

void AnnotationRenderer::DoHighlight(HighlightAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	BPoint p[4];
	rgb_color c = GetColor(a->GetColor(), 0.50);
	fView->SetHighColor(c);
	SetPenSize(a->GetBorderStyle()->GetWidth());

	for (int i = 0; i < a->QuadPointsLength(); i++) {
		CvtUserToDev(a->QuadPointsAt(i)->q, p, 4);
		BPoint swap = p[3];
		p[3] = p[2];
		p[2] = swap;
		BPolygon polygon(p, 4);
		fView->FillPolygon(&polygon);
	}
}

void AnnotationRenderer::StrokeSquiggly(BPoint p1, BPoint p2, float height)
{
	if (height <= 0.001)
		return;
	const float width = 0.05 * height;
	const float amplitude = 1.1; // relative to width
	BPoint d(p2.x - p1.x, p2.y - p1.y);
	float len = sqrt(d.x * d.x + d.y * d.y);
	if (len <= 0.001)
		return;
	fView->MovePenTo(p1);
	d.x = width * d.x / len;
	d.y = width * d.y / len;
	BPoint h1(d.y, d.x), h2(-d.y, -d.x);
	h1.x *= amplitude;
	h1.y *= amplitude;
	h2.x *= amplitude;
	h2.y *= amplitude;
	const int m = int(len / width);
	for (int i = 0; i < m; i++) {
		BPoint bezier[4];
		bezier[0] = p1;
		bezier[1] = p1;
		p1 += d;
		bezier[2] = (i % 2 == 0) ? p1 + h1 : p1 + h2;
		p1 += d;
		bezier[3] = p1;
		fView->StrokeBezier(bezier);
	}
}

void AnnotationRenderer::DoUnderline(UnderlineAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	BPoint p[4];
	rgb_color c = GetColor(a->GetColor(), 1);
	fView->SetHighColor(c);
	SetPenSize(a->GetBorderStyle()->GetWidth());

	for (int i = 0; i < a->QuadPointsLength(); i++) {
		CvtUserToDev(a->QuadPointsAt(i)->q, p, 4);
		BPoint p1 = PointBetween(p[0], p[2], 0.85);
		BPoint p2 = PointBetween(p[1], p[3], 0.85);
		fView->StrokeLine(p1, p2);
	}
}

void AnnotationRenderer::DoSquiggly(SquigglyAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	BPoint p[4];
	rgb_color c = GetColor(a->GetColor(), 1);
	fView->SetHighColor(c);
	SetPenSize(a->GetBorderStyle()->GetWidth());

	for (int i = 0; i < a->QuadPointsLength(); i++) {
		CvtUserToDev(a->QuadPointsAt(i)->q, p, 4);
		BPoint p1 = PointBetween(p[0], p[2], 0.85);
		BPoint p2 = PointBetween(p[1], p[3], 0.85);
		BPoint d = p[0] - p[2];
		float height = sqrt(d.x * d.x + d.y * d.y);
		StrokeSquiggly(p1, p2, height);
	}
}

void AnnotationRenderer::DoStrikeOut(StrikeOutAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	BPoint p[4];
	rgb_color c = GetColor(a->GetColor(), 1);
	fView->SetHighColor(c);
	SetPenSize(a->GetBorderStyle()->GetWidth());

	for (int i = 0; i < a->QuadPointsLength(); i++) {
		CvtUserToDev(a->QuadPointsAt(i)->q, p, 4);
		BPoint p1 = PointBetween(p[0], p[2], 0.55);
		BPoint p2 = PointBetween(p[1], p[3], 0.55);
		fView->StrokeLine(p1, p2);
	}
}

void AnnotationRenderer::DoStamp(StampAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);
}

void AnnotationRenderer::DoInk(InkAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	rgb_color c = GetColor(a->GetColor(), 1);
	fView->SetHighColor(c);
	SetPenSize(a->GetBorderStyle()->GetWidth());
	for (int i = 0; i < a->GetLength(); i++) {
		PDFPoints* path = a->PathAt(i);
		const int n = path->GetLength();
		BPoint* points = new BPoint[n];
		CvtUserToDev(path->Points(), points, path->GetLength());
		fView->MovePenTo(points[0]);
		for (int j = 0; j < path->GetLength(); j++) {
			fView->StrokeLine(points[j]);
		}
		delete[] points;
		// bug in Be's implementation of StrokePolygon?
		// fView->StrokePolygon(points, path->GetLength(), false);
	}
}

void AnnotationRenderer::DoPopup(PopupAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);
}

void AnnotationRenderer::DoFileAttachment(FileAttachmentAnnot* a)
{
	if (!DrawAnnotation(a))
		return;
	ClipToRect clip(this, a);

	PDFRectangle* r = a->GetRect();
	BRect t = ToRect(r);
	fView->SetHighColor(GetColor(a->GetColor(), a->GetOpacity()));
	//	fView->StrokeRect(t, B_SOLID_HIGH);
	BBitmap* image = gApp->GetAttachmentImage((int)a->GetType());
	DrawBitmap(image, BPoint(t.left, t.top));
}

void AnnotationRenderer::DoSound(SoundAnnot* a)
{
	//	if (!DrawAnnotation(a)) return;
	//	ClipToRect clip(this, a);
}

void AnnotationRenderer::DoMovie(MovieAnnot* a)
{
	//	if (!DrawAnnotation(a)) return;
	//	ClipToRect clip(this, a);
}

void AnnotationRenderer::DoWidget(WidgetAnnot* a)
{
	//	if (!DrawAnnotation(a)) return;
	//	ClipToRect clip(this, a);
}

void AnnotationRenderer::DoPrinterMark(PrinterMarkAnnot* a)
{
	//	if (!DrawAnnotation(a)) return;
	//	ClipToRect clip(this, a);
}

void AnnotationRenderer::DoTrapNet(TrapNetAnnot* a)
{
	//	if (!DrawAnnotation(a)) return;
	//	ClipToRect clip(this, a);
}
