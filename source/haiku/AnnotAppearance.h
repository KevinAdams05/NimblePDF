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

#ifndef ANNOT_APPEARANCE_H
#define ANNOT_APPEARANCE_H

// xpdf
#include <Object.h>
#include <XRef.h>
#include <Catalog.h>
#include <PDFDoc.h>

#include "Annotation.h"

class AnnotWriter;

class GraphicsStream {
	GooString fStream;

public:
	int GetLength() { return fStream.size(); }
	const char* GetStream() { return fStream.c_str(); }
	GooString* GetString() { return &fStream; }

	void Append(const char* s);
	void Append(GooString* s);
	void Append(double f);
	void Add(const char* s);
	void Add(double f);
	void AddCr(const char* s);
	void AddCr(double f);
	void Add(PDFPoint p);

	void Transform(float sx, float sy, float dx, float dy);

	enum { stroke = true, fill = false };

	void SetColor(bool stroke, GfxColorComp r, GfxColorComp g, GfxColorComp b);
	void SetColor(bool stroke, GfxRGB* c);

	void SetLineWidth(float w);
	enum line_cap_style { butt_cap = 0, round_cap = 1, square_cap = 2 };
	void SetLineCap(line_cap_style style);
	enum line_join_style { line_join = 0, round_join = 1, bevel_join = 2 };
	void SetLineJoin(line_join_style style);

	void MoveTo(PDFPoint p);
	void LineTo(PDFPoint p);
	void BezierTo(PDFPoint p1, PDFPoint p2, PDFPoint p3);
	void Close();

	void Stroke();
	void Fill();
};

// ASCII 85 Encoder
class A85Encoder {
	GooString* fStream;
	unsigned char fInput[4];
	int fLength;

	void Encode(unsigned char* output, bool* isNull);

public:
	A85Encoder(GooString* stream);
	void Append(unsigned char byte);
	void Flush();
};

// Appearance Stream Creator
class NimblePDFAnnotAppearance : public AnnotVisitor {
public:
	NimblePDFAnnotAppearance();
	~NimblePDFAnnotAppearance();

	int GetLength() { return fAS.GetLength(); }
	GooString* GetStream() { return fAS.GetString(); }

	// visitor functionality
	void DoText(TextAnnot* a);
	void DoLink(LinkAnnot* a);
	void DoFreeText(FreeTextAnnot* a);
	void DoLine(LineAnnot* a);
	void DoSquare(SquareAnnot* a);
	void DoCircle(CircleAnnot* a);
	void DoHighlight(HighlightAnnot* a);
	void DoUnderline(UnderlineAnnot* a);
	void DoSquiggly(SquigglyAnnot* a);
	void DoStrikeOut(StrikeOutAnnot* a);
	void DoStamp(StampAnnot* a);
	void DoInk(InkAnnot* a);
	void DoPopup(PopupAnnot* a);
	void DoFileAttachment(FileAttachmentAnnot* a);
	void DoSound(SoundAnnot* a);
	void DoMovie(MovieAnnot* a);
	void DoWidget(WidgetAnnot* a);
	void DoPrinterMark(PrinterMarkAnnot* a);
	void DoTrapNet(TrapNetAnnot* a);

private:
	void Init(Annotation* annot);
	PDFPoint UserToForm(PDFPoint p);
	void DoStyledAnnot(StyledAnnot* a);
	PDFPoint PointBetween(PDFPoint p1, PDFPoint p2, float f);

	void Stroke(PDFQuadPoints& qp, float f);
	void DoMarkupAnnot(MarkupAnnot* a, float f);
	void Stroke(PDFQuadPoints& qp);
	void StrokeSquiggly(PDFPoint p1, PDFPoint p2, float height);

	GraphicsStream fAS;
	PDFRectangle fBBox;
	PDFPoint fDelta;
	A85Encoder fEncoder;
};

#endif
