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

#if defined(__BEOS__) || defined(__HAIKU__)
#include "BepdfApplication.h"
#include "AnnotationRenderer.h"
#include <Bitmap.h>
#include <View.h>
#endif

#include "AnnotAppearance.h"

#include <ctype.h>

// Implementation of A85Encoder

A85Encoder::A85Encoder(GString* stream)
    : fStream(stream),
      fLength(0)
{}

void A85Encoder::Encode(unsigned char* output, bool* isNull)
{
	unsigned long code = fInput[0] * 256 * 256 * 256 + fInput[1] * 256 * 256 + fInput[2] * 256 + fInput[3];

	*isNull = true;

	for (int i = 4; i >= 0; i--) {
		output[i] = code % 85 + 33;
		code /= 85;
		if (output[i] != '!') {
			*isNull = false;
		}
	}
}

void A85Encoder::Append(unsigned char byte)
{
	fInput[fLength] = byte;
	fLength++;
	if (fLength == 4) {
		unsigned char output[5];
		bool isNull;

		Encode(output, &isNull);

		if (isNull) {
			fStream->append('z');
		} else {
			for (int i = 0; i <= 4; i++) {
				fStream->append(output[i]);
			}
		}
		fLength = 0;
	}
}

void A85Encoder::Flush()
{
	if (fLength != 0) {
		int n = fLength;
		unsigned char output[5];
		bool isNull;
		for (; fLength < 4; fLength++) {
			fInput[fLength] = 0;
		}
		Encode(output, &isNull);
		for (int i = 0; i <= n; i++) {
			fStream->append(output[i]);
		}
	}
	fStream->append("~>\n");
}


// Implementation of GraphicsStream

void GraphicsStream::Append(const char* s)
{
	fStream.append(s);
}

void GraphicsStream::Append(GString* s)
{
	fStream.append(s);
}

void GraphicsStream::Append(double f)
{
	char b[20];
	sprintf(b, "%g", f);
	Append(b);
}

void GraphicsStream::Add(const char* s)
{
	Append(s);
	Append(" ");
}

void GraphicsStream::Add(double f)
{
	Append(f);
	Append(" ");
}

void GraphicsStream::AddCr(const char* s)
{
	Append(s);
	Append("\n");
}

void GraphicsStream::AddCr(double f)
{
	Append(f);
	Append("\n");
}

void GraphicsStream::Add(PDFPoint p)
{
	Add(p.x);
	Add(p.y);
}

void GraphicsStream::Transform(float sx, float sy, float dx, float dy)
{
	Add(sx);
	Add("0 0");
	Add(sy);
	Add(dx);
	Add(dy);
	AddCr("cm");
}

void GraphicsStream::SetColor(bool stroke, GfxColorComp r, GfxColorComp g, GfxColorComp b)
{
	Add("/DeviceRGB");
	AddCr(stroke ? "CS" : "cs");
	Add(colToDbl(r));
	Add(colToDbl(g));
	Add(colToDbl(b));
	AddCr(stroke ? "SC" : "sc");
}

void GraphicsStream::SetColor(bool stroke, GfxRGB* c)
{
	SetColor(stroke, c->r, c->g, c->b);
}

void GraphicsStream::SetLineWidth(float w)
{
	Add(w);
	AddCr("w");
}

void GraphicsStream::SetLineCap(line_cap_style style)
{
	Add(style);
	AddCr("J");
}

void GraphicsStream::SetLineJoin(line_join_style style)
{
	Add(style);
	AddCr("j");
}

void GraphicsStream::MoveTo(PDFPoint p)
{
	Add(p);
	AddCr("m");
}

void GraphicsStream::LineTo(PDFPoint p)
{
	Add(p);
	AddCr("l");
}

void GraphicsStream::BezierTo(PDFPoint p1, PDFPoint p2, PDFPoint p3)
{
	Add(p1);
	Add(p2);
	Add(p3);
	AddCr("c");
}

void GraphicsStream::Close()
{
	AddCr("h");
}

void GraphicsStream::Stroke()
{
	AddCr("S");
}

void GraphicsStream::Fill()
{
	AddCr("f");
}

// Implementation of AnnotAppearance

void AnnotAppearance::Init(Annotation* annot)
{
	fBBox = *annot->GetRect();
	double swap;
	// normalize rectangle
	if (fBBox.x1 > fBBox.x2) {
		swap = fBBox.x1;
		fBBox.x1 = fBBox.x2;
		fBBox.x2 = swap;
	}
	if (fBBox.y1 > fBBox.y2) {
		swap = fBBox.y1;
		fBBox.y1 = fBBox.y2;
		fBBox.y2 = swap;
	}
	// user to form coordinate translation
	fDelta.x = fBBox.x1;
	fDelta.y = fBBox.y1;
	// (0/0) is origin of appearance stream
	fBBox.x2 -= fBBox.x1;
	fBBox.x1 = 0.0;
	fBBox.y2 -= fBBox.y1;
	fBBox.y1 = 0.0;
	// setup graphics state
	fAS.SetColor(GraphicsStream::stroke, annot->GetColor());
}

// Coordinate transformation from user to form
PDFPoint AnnotAppearance::UserToForm(PDFPoint p)
{
	return p - fDelta;
}

PDFPoint AnnotAppearance::PointBetween(PDFPoint p1, PDFPoint p2, float f)
{
	PDFPoint p(p2.x - p1.x, p2.y - p1.y);
	p.x *= f;
	p.y *= f;
	p.x += p1.x;
	p.y += p1.y;
	return p;
}

void AnnotAppearance::DoStyledAnnot(StyledAnnot* a)
{
	float w = a->GetBorderStyle()->GetWidth();
	if (w != 1.0) {
		fAS.SetLineWidth(w);
	}
}

void AnnotAppearance::Stroke(PDFQuadPoints& qp, float f)
{
	PDFPoint p1 = PointBetween(qp[0], qp[2], f);
	PDFPoint p2 = PointBetween(qp[1], qp[3], f);
	fAS.MoveTo(UserToForm(p1));
	fAS.LineTo(UserToForm(p2));
	fAS.Stroke();
}

void AnnotAppearance::DoMarkupAnnot(MarkupAnnot* a, float f)
{
	DoStyledAnnot(a);
	for (int i = 0; i < a->QuadPointsLength(); i++) {
		Stroke(*(a->QuadPointsAt(i)), f);
	}
}

void AnnotAppearance::Stroke(PDFQuadPoints& qp)
{
	fAS.MoveTo(UserToForm(qp[0]));
	fAS.LineTo(UserToForm(qp[1]));
	fAS.LineTo(UserToForm(qp[3]));
	fAS.LineTo(UserToForm(qp[2]));
	fAS.Close();
	fAS.Stroke();
}

AnnotAppearance::AnnotAppearance()
    : fEncoder(fAS.GetString())
{}

AnnotAppearance::~AnnotAppearance()
{}

// Annotation visitor implementation
void AnnotAppearance::DoText(TextAnnot* a)
{
#if defined(__BEOS__) || defined(__HAIKU__)
	Init(a);
	BBitmap* src = gApp->GetTextAnnotImage((int)a->GetType());
	rgb_color c = AnnotationRenderer::GetColor(a->GetColor());
	BBitmap* image = AnnotationRenderer::ColorBitmap(src, c);

	if (image->ColorSpace() == B_RGBA32) {
		BRect b = image->Bounds();
		int h = b.IntegerHeight() + 1;
		int w = b.IntegerWidth() + 1;
		fAS.AddCr("q");
		fAS.Transform(w, h, 0, fBBox.y2 - fBBox.y1 - h);
		// begin image
		fAS.AddCr("BI");
		// 8 bits per channel
		fAS.AddCr("/BPC 8");
		fAS.AddCr("/CS/DeviceRGB");
		fAS.Add("/H");
		fAS.AddCr(h);
		fAS.Add("/W");
		fAS.AddCr(w);
		fAS.AddCr("/F[/A85]");
		// Default is identity matrix:
		// fAS.AddCr("/Decode [0 1 0 1 0 1]");
		// TODO what is default?
		fAS.AddCr("/I false");
		// image data
		fAS.AddCr("ID");
		// Use ASCII 85 Encoder for bitmap
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				unsigned char* p = ((unsigned char*)image->Bits()) + x * 4 + y * image->BytesPerRow();
				if (p[3] != 0) {
					// write RGB values
					fEncoder.Append(p[2]);
					fEncoder.Append(p[1]);
					fEncoder.Append(p[0]);
				} else {
					// convert 100% transparent pixel to white pixel
					fEncoder.Append(255);
					fEncoder.Append(255);
					fEncoder.Append(255);
				}
			}
		}
		fEncoder.Flush();
		// end image
		fAS.AddCr("EI");

		fAS.AddCr("Q");
	} else {
		fprintf(stderr, "Wrong color space\n");
	}
	delete image;
#endif
}


void AnnotAppearance::DoLink(LinkAnnot* a)
{}


void AnnotAppearance::DoFreeText(FreeTextAnnot* a)
{
	/* Acrobat 5 does not require it
	Init(a);
	fAS.Add("BT");
		fAS.AddCr(a->GetAppearance()->getCString());
		fAS.AddCr("0 0 Td");
		GString* s = AnnotUtils::EscapeString(a->GetContents());
		fAS.Append("(");
		fAS.Append(s);
		fAS.AddCr(") Tj");
		delete s;
	fAS.AddCr("ET");
*/
}


void AnnotAppearance::DoLine(LineAnnot* a)
{
	Init(a);
	DoStyledAnnot(a);
	fAS.MoveTo(UserToForm(a->GetLine()[0]));
	fAS.LineTo(UserToForm(a->GetLine()[1]));
}


void AnnotAppearance::DoSquare(SquareAnnot* a)
{
	Init(a);
	DoStyledAnnot(a);
	float w = a->GetBorderStyle()->GetWidth() / 2.0;
	PDFPoint p1(fBBox.x1 + w, fBBox.y1 + w), p3(fBBox.x2 - w, fBBox.y2 - w);
	PDFPoint p2(p3.x, p1.y), p4(p1.x, p3.y);

	fAS.MoveTo(p1);
	fAS.LineTo(p2);
	fAS.LineTo(p3);
	fAS.LineTo(p4);
	fAS.Close();
	fAS.Stroke();
}


void AnnotAppearance::DoCircle(CircleAnnot* a)
{
	Init(a);
	DoStyledAnnot(a);
	// approximate ellipse with four bezier curves
	float w = a->GetBorderStyle()->GetWidth();
	const double rx = (fBBox.x2 - w) / 2.0;
	const double ry = (fBBox.y2 - w) / 2.0;
	const double x = fBBox.x2 / 2.0;
	const double y = fBBox.y2 / 2.0;
	const double k = 0.552284749;
	const double sx = rx * k;
	const double sy = ry * k;
	fAS.MoveTo(PDFPoint(x + rx, y));
	fAS.BezierTo(PDFPoint(x + rx, y + sy), PDFPoint(x + sx, y + ry), PDFPoint(x, y + ry));
	fAS.BezierTo(PDFPoint(x - sx, y + ry), PDFPoint(x - rx, y + sy), PDFPoint(x - rx, y));
	fAS.BezierTo(PDFPoint(x - rx, y - sy), PDFPoint(x - sx, y - ry), PDFPoint(x, y - ry));
	fAS.BezierTo(PDFPoint(x + sx, y - ry), PDFPoint(x + rx, y - sy), PDFPoint(x + rx, y));
	fAS.Stroke();
}


void AnnotAppearance::DoHighlight(HighlightAnnot* a)
{
	Init(a);
	fAS.SetLineWidth(3.0);
	for (int i = 0; i < a->QuadPointsLength(); i++) {
		Stroke(*(a->QuadPointsAt(i)));
	}
}


void AnnotAppearance::DoUnderline(UnderlineAnnot* a)
{
	Init(a);
	DoMarkupAnnot(a, 0.85);
}


void AnnotAppearance::StrokeSquiggly(PDFPoint p1, PDFPoint p2, float height)
{
	if (height <= 0.001)
		return;
	const float width = 0.05 * height;
	const float amplitude = 1.1; // relative to width
	PDFPoint d(p2.x - p1.x, p2.y - p1.y);
	float len = sqrt(d.x * d.x + d.y * d.y);
	if (len <= 0.001)
		return;
	fAS.MoveTo(p1);
	d.x = width * d.x / len;
	d.y = width * d.y / len;
	PDFPoint h1(d.y, d.x), h2(-d.y, -d.x);
	h1.x *= amplitude;
	h1.y *= amplitude;
	h2.x *= amplitude;
	h2.y *= amplitude;
	const int m = int(len / width);
	for (int i = 0; i < m; i++) {
		PDFPoint bezier[3];
		bezier[0] = p1;
		p1 += d;
		bezier[1] = (i % 2 == 1) ? p1 + h1 : p1 + h2;
		p1 += d;
		bezier[2] = p1;
		fAS.BezierTo(bezier[0], bezier[1], bezier[2]);
	}
	fAS.Stroke();
}

void AnnotAppearance::DoSquiggly(SquigglyAnnot* a)
{
	Init(a);
	DoStyledAnnot(a);
	for (int i = 0; i < a->QuadPointsLength(); i++) {
		PDFQuadPoints& qp = *(a->QuadPointsAt(i));
		PDFPoint p1 = UserToForm(PointBetween(qp[0], qp[2], 0.85));
		PDFPoint p2 = UserToForm(PointBetween(qp[1], qp[3], 0.85));
		PDFPoint p = UserToForm(qp[2]) - UserToForm(qp[0]);
		float height = sqrt(p.x * p.x + p.y * p.y);
		StrokeSquiggly(p1, p2, height);
	}
}


void AnnotAppearance::DoStrikeOut(StrikeOutAnnot* a)
{
	Init(a);
	DoMarkupAnnot(a, 0.55);
}

void AnnotAppearance::DoStamp(StampAnnot* a)
{
	// not implemented yet
}


void AnnotAppearance::DoInk(InkAnnot* a)
{
	// not implemented yet
}


void AnnotAppearance::DoPopup(PopupAnnot* a)
{
	// has no appearance stream
}


void AnnotAppearance::DoFileAttachment(FileAttachmentAnnot* a)
{
	// not implemented yet
}


void AnnotAppearance::DoSound(SoundAnnot* a)
{
	// not implemented yet
}


void AnnotAppearance::DoMovie(MovieAnnot* a)
{
	// not implemented yet
}


void AnnotAppearance::DoWidget(WidgetAnnot* a)
{
	// not implemented yet
}


void AnnotAppearance::DoPrinterMark(PrinterMarkAnnot* a)
{
	// not implemented yet
}


void AnnotAppearance::DoTrapNet(TrapNetAnnot* a)
{
	// not implemented yet
}
