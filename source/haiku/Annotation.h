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

#ifndef ANNOTATION_H
#define ANNOTATION_H

// xpdf
#include <Object.h>
#include <Link.h>
#include <Page.h>     // for PDFRectangle
#include <GfxState.h> // for GfxRGB
#include <list>

// bepdf
#include "FileSpec.h"

class TextAnnot;
class LinkAnnot;
class StyledAnnot;
class FreeTextAnnot;
class LineAnnot;
class SquareAnnot;
class CircleAnnot;
class MarkupAnnot;
class HighlightAnnot;
class UnderlineAnnot;
class SquigglyAnnot;
class StrikeOutAnnot;
class InkAnnot;
class StampAnnot;
class PopupAnnot;
class FileAttachmentAnnot;
class SoundAnnot;
class MovieAnnot;
class WidgetAnnot;
class PrinterMarkAnnot;
class TrapNetAnnot;

class BePDFAcroForm;

extern Ref empty_ref;

bool is_empty_ref(Ref ref);

const char* to_date(const char* date, char* buffer); // min. buffer length == 80

class PDFFont {
private:
	Ref fRef;
	GooString fName;
	GooString fShortName;

public:
	PDFFont();

	Ref GetRef() const;
	void SetRef(Ref ref);
	const char* GetName();
	void SetName(const char* name);
	const char* GetShortName();
	void SetShortName(const char* name);

	void Print();
};

class PDFStandardFonts {
private:
	enum { num_of_standard_fonts = 12 };
	static const char* fNames[num_of_standard_fonts];
	PDFFont fFonts[num_of_standard_fonts];

public:
	PDFStandardFonts();
	void Reset(); // refs and short name

	int CountFonts() const;
	PDFFont* FontAt(int i);
	PDFFont* FindByName(const char* name);
	PDFFont* FindByShortName(const char* name);
};

class PDFPoint {
public:
	double x, y;
	PDFPoint() {}
	PDFPoint(double x, double y)
	    : x(x),
	      y(y)
	{}

	PDFPoint operator+(const PDFPoint& p) const;
	PDFPoint operator-(const PDFPoint& p) const;
	bool operator==(const PDFPoint& p) const;
	bool operator!=(const PDFPoint& p) const;
	PDFPoint& operator+=(const PDFPoint& p);
	PDFPoint& operator-=(const PDFPoint& p);
};

class PDFPoints {
private:
	int fLength;
	PDFPoint* fPoints;

public:
	PDFPoints()
	    : fLength(0),
	      fPoints(NULL)
	{}
	PDFPoints(PDFPoints* copy);
	~PDFPoints() { delete fPoints; }

	void SetLength(int l)
	{
		delete[] fPoints;
		fLength = l;
		fPoints = new PDFPoint[l];
	}
	int GetLength() { return fLength; }
	PDFPoint* PointAt(int i) { return &fPoints[i]; }
	PDFPoint* Points() { return fPoints; }

	PDFPoints& operator=(PDFPoints& p);
};

class PDFQuadPoints {
public:
	PDFPoint q[4];
	PDFPoint& operator[](int i) { return q[i]; }
};

class AnnotVisitor {
public:
	AnnotVisitor();
	virtual ~AnnotVisitor();

	virtual void DoText(TextAnnot* a) = 0;
	virtual void DoLink(LinkAnnot* a) = 0;
	virtual void DoFreeText(FreeTextAnnot* a) = 0;
	virtual void DoLine(LineAnnot* a) = 0;
	virtual void DoSquare(SquareAnnot* a) = 0;
	virtual void DoCircle(CircleAnnot* a) = 0;
	virtual void DoHighlight(HighlightAnnot* a) = 0;
	virtual void DoUnderline(UnderlineAnnot* a) = 0;
	virtual void DoSquiggly(SquigglyAnnot* a) = 0;
	virtual void DoStrikeOut(StrikeOutAnnot* a) = 0;
	virtual void DoStamp(StampAnnot* a) = 0;
	virtual void DoInk(InkAnnot* a) = 0;
	virtual void DoPopup(PopupAnnot* a) = 0;
	virtual void DoFileAttachment(FileAttachmentAnnot* a) = 0;
	virtual void DoSound(SoundAnnot* a) = 0;
	virtual void DoMovie(MovieAnnot* a) = 0;
	virtual void DoWidget(WidgetAnnot* a) = 0;
	virtual void DoPrinterMark(PrinterMarkAnnot* a) = 0;
	virtual void DoTrapNet(TrapNetAnnot* a) = 0;
};

class BorderStyle {
public:
	enum border_style_type { solid_style, dashed_style, beveled_style, inset_style, underline_style };

private:
	int fWidth;
	border_style_type fStyle;
	// dash array not supported yet!

public:
	BorderStyle();
	void Set(Dict* d, bool check_type = true);

	int GetWidth() { return fWidth; }
	border_style_type GetStyle() { return fStyle; }

	void SetWidth(int w) { fWidth = w; }
	void SetStyle(border_style_type style) { fStyle = style; }
};


enum {
	invisible_flag = 1 << 0,
	hidden_flag = 1 << 1,
	print_flag = 1 << 2,
	no_zoom_flag = 1 << 3,
	no_rotate_flag = 1 << 4,
	no_view_flag = 1 << 5,
	read_only_flag = 1 << 6
};

class AnnotFlags {
private:
	unsigned int fFlags;

public:
	AnnotFlags()
	    : fFlags(0)
	{}

	void Set(int f) { fFlags = f; }
	unsigned int Flags() { return fFlags; }

	bool IsSet(unsigned int mask) const { return (fFlags & mask) == mask; }
	void ClearMask(unsigned int mask) { fFlags = fFlags & ~mask; }
	void SetMask(unsigned int mask) { fFlags |= mask; }

	bool Invisible() const { return IsSet(invisible_flag); }
	bool Hidden() const { return IsSet(hidden_flag); }
	bool Print() const { return IsSet(print_flag); }
	bool NoZoom() const { return IsSet(no_zoom_flag); }
	bool NoRotate() const { return IsSet(no_rotate_flag); }
	bool NoView() const { return IsSet(no_view_flag); }
	bool ReadOnly() const { return IsSet(read_only_flag); }
};

class Annotation {
private:
	Ref fRef;
	GooString fContents; // in PDFDocEncoding or UCS2
	PDFRectangle fRect;
	GooString* fDate; // optional (can be NULL)
	AnnotFlags fFlags;
	// border style and border -> StyledAnnot
	// appearence stream
	bool fHasAppearanceStream;
	// appearence state
	GfxRGB fColor;
	bool fHasColor;
	double fOpacity;
	GooString* fTitle; // optional
	// popup
	PopupAnnot* fPopup; // optional
	// action
	// additional action
	// structural parent tree

	bool fValid;

	// editing
	bool fDeleted;
	bool fChanged;
	bool fSelected;

protected:
	// This method should be the first statement in the constructor
	// of a child class.
	// Returns false if constructor in parent class has failed.
	bool CheckInvalid()
	{
		if (fValid) {
			fValid = false;
			return false;
		} else
			return true;
	}
	// This method should be called as the last statement
	// in the constructor if there were no errors.
	void SetValid() { fValid = true; }
	bool ReadNum(Array* a, int i, double& d);
	bool ReadPoint(Array* a, int i, PDFPoint* p);
	void SetString(GooString*& s, const char* t);

public:
	Annotation(PDFRectangle rect);
	Annotation(Annotation* copy);
	Annotation(Dict* annot);
	virtual ~Annotation();

	virtual Annotation* Clone() = 0;

	bool IsValid() const { return fValid; }

	Ref GetRef() { return fRef; }
	GooString* GetContents() { return &fContents; }
	PDFRectangle* GetRect() { return &fRect; }
	const char* GetDate() { return fDate ? fDate->c_str() : ""; }
	AnnotFlags* GetFlags() { return &fFlags; }
	bool HasAppearanceStream() { return fHasAppearanceStream; }
	GfxRGB* GetColor() { return &fColor; }
	bool HasColor() { return fHasColor; }
	double GetOpacity() { return fOpacity; }
	GooString* GetTitle() { return fTitle; }
	PopupAnnot* GetPopup() { return fPopup; }

	virtual void Visit(AnnotVisitor* v) = 0;
	virtual void Print();

	PDFPoint LeftTop() { return PDFPoint(fRect.x1, fRect.y2); }
	virtual void MoveTo(PDFPoint p);
	virtual void ResizeTo(double w, double h);

	// Editing
	void SetRef(const Ref ref);
	void SetContents(GooString* contents);
	void SetDate(const char* date);
	void SetHasColor(bool color) { fHasColor = color; }
	void SetTitle(GooString* title);
	void SetPopup(PopupAnnot* a) { fPopup = a; }
	void SetDeleted(bool d)
	{
		fDeleted = d;
		if (d)
			SetChanged();
	}
	bool IsDeleted() const { return fDeleted; }
	void SetChanged(bool c = true) { fChanged = c; }
	bool HasChanged() const { return fChanged; }
	void SetSelected(bool s) { fSelected = s; }
	bool IsSelected() const { return fSelected; }
};


class TextAnnot : public Annotation {
public:
	enum text_annot_type {
		comment_type,
		help_type,
		insert_type,
		key_type,
		new_paragraph_type,
		note_type,
		paragraph_type,
		unknown_type,
		no_of_types
	};

private:
	bool fOpen;
	GooString fName; // default Note
	enum text_annot_type fType;

	static const char* fTypeNames[no_of_types - 1];

public:
	TextAnnot(PDFRectangle rect, text_annot_type type);
	TextAnnot(TextAnnot* copy);
	TextAnnot(Dict* annot);

	Annotation* Clone() { return new TextAnnot(this); }

	bool IsOpen() { return fOpen; }
	const char* GetName() { return fName.c_str(); }
	enum text_annot_type GetType() { return fType; }

	virtual void Visit(AnnotVisitor* v) { v->DoText(this); }
	virtual void Print();
};

class LinkAnnot : public Annotation {
private:
	LinkAction* fLinkAction;

public:
	LinkAnnot(LinkAnnot* copy);
	LinkAnnot(Dict* annot);

	Annotation* Clone() { return new LinkAnnot(this); }

	LinkAction* GetLinkAction() { return fLinkAction; }

	virtual void Visit(AnnotVisitor* v) { v->DoLink(this); }
	virtual void Print();
};

class StyledAnnot : public Annotation {
private:
	BorderStyle fStyle;

public:
	StyledAnnot(PDFRectangle rect, float r, float g, float b);
	StyledAnnot(StyledAnnot* copy);
	StyledAnnot(Dict* d);

	BorderStyle* GetBorderStyle() { return &fStyle; }
	virtual void Print();
};

enum free_text_justification { left_justify = 0, centered = 1, right_justify = 2 };

free_text_justification ToFreeTextJustification(const char* name);
const char* ToString(free_text_justification j);

class FreeTextAnnot : public StyledAnnot {
private:
	GooString fAppearance;
	free_text_justification fJustification;
	PDFFont* fFont;
	GfxRGB fFontColor;
	float fFontSize;

	void Init();

public:
	FreeTextAnnot(PDFRectangle rect, PDFFont* font);
	FreeTextAnnot(FreeTextAnnot* copy);
	FreeTextAnnot(Dict* annot, BePDFAcroForm* acroForm);

	Annotation* Clone() { return new FreeTextAnnot(this); }

	GooString* GetAppearance() { return &fAppearance; }
	void SetAppearance(GooString* ap);
	free_text_justification GetJustification() { return fJustification; }
	void SetJustification(free_text_justification j);
	PDFFont* GetFont() const { return fFont; }
	void SetFont(PDFFont* font);
	GfxRGB* GetFontColor() { return &fFontColor; }
	float GetFontSize() const { return fFontSize; }
	void SetFontSize(float s) { fFontSize = s; }

	virtual void Visit(AnnotVisitor* v) { v->DoFreeText(this); }
	virtual void Print();
};

class LineAnnot : public StyledAnnot {
private:
	PDFPoint fLine[2];

public:
	LineAnnot(PDFRectangle rect, PDFPoint* line);
	LineAnnot(LineAnnot* copy);
	LineAnnot(Dict* annot);
	Annotation* Clone() { return new LineAnnot(this); }

	PDFPoint* GetLine() { return fLine; }

	virtual void Visit(AnnotVisitor* v) { v->DoLine(this); }
	virtual void Print();
};

class SquareAnnot : public StyledAnnot {
public:
	SquareAnnot(PDFRectangle rect)
	    : StyledAnnot(rect, 0.0, 0.6, 1.0)
	{}
	SquareAnnot(SquareAnnot* copy)
	    : StyledAnnot(copy)
	{}
	SquareAnnot(Dict* d)
	    : StyledAnnot(d)
	{}
	Annotation* Clone() { return new SquareAnnot(this); }
	virtual void Visit(AnnotVisitor* v) { v->DoSquare(this); }
	virtual void Print();
};

class CircleAnnot : public StyledAnnot {
public:
	CircleAnnot(PDFRectangle rect)
	    : StyledAnnot(rect, 0.0, 0.6, 1.0)
	{}
	CircleAnnot(CircleAnnot* copy)
	    : StyledAnnot(copy)
	{}
	CircleAnnot(Dict* d)
	    : StyledAnnot(d)
	{}
	Annotation* Clone() { return new CircleAnnot(this); }
	virtual void Visit(AnnotVisitor* v) { v->DoCircle(this); }
	virtual void Print();
};

class MarkupAnnot : public StyledAnnot {
private:
	int fLength;
	PDFQuadPoints* fQuadPoints;

public:
	MarkupAnnot(PDFRectangle rect, float r, float g, float b, PDFQuadPoints* p, int l);
	MarkupAnnot(MarkupAnnot* copy);
	MarkupAnnot(Dict* annot);
	~MarkupAnnot();

	int QuadPointsLength() { return fLength; }
	PDFQuadPoints* QuadPointsAt(int i) { return &fQuadPoints[i]; }

	virtual void Visit(AnnotVisitor* v) = 0;
	virtual void Print();

	virtual void MoveTo(PDFPoint p);
	virtual void ResizeTo(double w, double h);
};

class HighlightAnnot : public MarkupAnnot {
public:
	HighlightAnnot(PDFRectangle rect);
	HighlightAnnot(HighlightAnnot* copy)
	    : MarkupAnnot(copy)
	{}
	HighlightAnnot(Dict* annot)
	    : MarkupAnnot(annot)
	{}
	Annotation* Clone() { return new HighlightAnnot(this); }
	virtual void Visit(AnnotVisitor* v) { v->DoHighlight(this); }
};

class UnderlineAnnot : public MarkupAnnot {
public:
	UnderlineAnnot(PDFRectangle rect);
	UnderlineAnnot(UnderlineAnnot* copy)
	    : MarkupAnnot(copy)
	{}
	UnderlineAnnot(Dict* annot)
	    : MarkupAnnot(annot)
	{}
	Annotation* Clone() { return new UnderlineAnnot(this); }
	virtual void Visit(AnnotVisitor* v) { v->DoUnderline(this); }
};

class SquigglyAnnot : public MarkupAnnot {
public:
	SquigglyAnnot(PDFRectangle rect);
	SquigglyAnnot(SquigglyAnnot* copy)
	    : MarkupAnnot(copy)
	{}
	SquigglyAnnot(Dict* annot)
	    : MarkupAnnot(annot)
	{}
	Annotation* Clone() { return new SquigglyAnnot(this); }
	virtual void Visit(AnnotVisitor* v) { v->DoSquiggly(this); }
};

class StrikeOutAnnot : public MarkupAnnot {
public:
	StrikeOutAnnot(PDFRectangle rect);
	StrikeOutAnnot(StrikeOutAnnot* copy)
	    : MarkupAnnot(copy)
	{}
	StrikeOutAnnot(Dict* annot)
	    : MarkupAnnot(annot)
	{}
	Annotation* Clone() { return new StrikeOutAnnot(this); }
	virtual void Visit(AnnotVisitor* v) { v->DoStrikeOut(this); }
};

class StampAnnot : public Annotation {
	GooString fName; // default Draft

public:
	StampAnnot(StampAnnot* copy);
	StampAnnot(Dict* annot);
	Annotation* Clone() { return new StampAnnot(this); }

	const char* GetName() { return fName.c_str(); }

	virtual void Visit(AnnotVisitor* v) { v->DoStamp(this); }
	virtual void Print();
};

class InkAnnot : public StyledAnnot {
private:
	int fLength;
	PDFPoints* fInkList;

public:
	InkAnnot(InkAnnot* copy);
	InkAnnot(Dict* annot);
	~InkAnnot();
	Annotation* Clone() { return new InkAnnot(this); }

	int GetLength() { return fLength; }
	PDFPoints* PathAt(int i) { return &fInkList[i]; }
	virtual void Visit(AnnotVisitor* v) { v->DoInk(this); }
	virtual void Print();
};

class PopupAnnot : public Annotation {
	Ref fParentRef;

public:
	PopupAnnot(PDFRectangle rect);
	PopupAnnot(PopupAnnot* copy);
	PopupAnnot(Dict* annot);
	Annotation* Clone() { return new PopupAnnot(this); }

	void SetParentRef(Ref ref) { fParentRef = ref; }
	Ref GetParentRef() { return fParentRef; }

	virtual void Visit(AnnotVisitor* v) { v->DoPopup(this); }
	virtual void Print();
};

class FileAttachmentAnnot : public Annotation {
public:
	enum attachment_type { graphic_type, paper_clip_type, push_pin_type, tag_type, unknown_type, no_of_types };

private:
	GooString fName;
	GooString fFileName;
	enum attachment_type fType;
	static const char* fTypeNames[no_of_types - 1];
	FileSpec fFileSpec;

public:
	FileAttachmentAnnot(FileAttachmentAnnot* copy);
	FileAttachmentAnnot(Dict* annot);
	Annotation* Clone() { return new FileAttachmentAnnot(this); }

	const char* GetName() { return fName.c_str(); }
	enum attachment_type GetType() { return fType; }
	// Return file name
	const char* GetFileName() { return fFileSpec.GetFileName()->c_str(); }
	// Save file
	bool Save(XRef* xref, const char* file);

	virtual void Visit(AnnotVisitor* v) { v->DoFileAttachment(this); }
	virtual void Print();
};

class AnnotSorter : public AnnotVisitor {
public:
	AnnotSorter();
	~AnnotSorter();

	virtual void DoText(TextAnnot* a) { fResult = 1; }
	virtual void DoLink(LinkAnnot* a) { fResult = 2; }
	virtual void DoFreeText(FreeTextAnnot* a) { fResult = 3; }
	virtual void DoLine(LineAnnot* a) { fResult = 4; }
	virtual void DoSquare(SquareAnnot* a) { fResult = 5; }
	virtual void DoCircle(CircleAnnot* a) { fResult = 6; }
	virtual void DoHighlight(HighlightAnnot* a) { fResult = 7; }
	virtual void DoUnderline(UnderlineAnnot* a) { fResult = 8; }
	virtual void DoSquiggly(SquigglyAnnot* a) { fResult = 9; }
	virtual void DoStrikeOut(StrikeOutAnnot* a) { fResult = 10; }
	virtual void DoStamp(StampAnnot* a) { fResult = 11; }
	virtual void DoInk(InkAnnot* a) { fResult = 12; }
	virtual void DoPopup(PopupAnnot* a) { fResult = 13; }
	virtual void DoFileAttachment(FileAttachmentAnnot* a) { fResult = 14; }
	virtual void DoSound(SoundAnnot* a) { fResult = 15; }
	virtual void DoMovie(MovieAnnot* a) { fResult = 16; }
	virtual void DoWidget(WidgetAnnot* a) { fResult = 17; }
	virtual void DoPrinterMark(PrinterMarkAnnot* a) { fResult = 18; }
	virtual void DoTrapNet(TrapNetAnnot* a) { fResult = 19; }

	int GetResult() { return fResult; }

private:
	int fResult;
};

class AnnotName : public AnnotVisitor {
public:
	AnnotName();
	~AnnotName();

	virtual void DoText(TextAnnot* a) { fResult = a->GetName(); }
	virtual void DoLink(LinkAnnot* a) { fResult = "Link"; }
	virtual void DoFreeText(FreeTextAnnot* a) { fResult = "FreeText"; }
	virtual void DoLine(LineAnnot* a) { fResult = "Line"; }
	virtual void DoSquare(SquareAnnot* a) { fResult = "Square"; }
	virtual void DoCircle(CircleAnnot* a) { fResult = "Circle"; }
	virtual void DoHighlight(HighlightAnnot* a) { fResult = "Highlight"; }
	virtual void DoUnderline(UnderlineAnnot* a) { fResult = "Underline"; }
	virtual void DoSquiggly(SquigglyAnnot* a) { fResult = "Squiggly"; }
	virtual void DoStrikeOut(StrikeOutAnnot* a) { fResult = "StrikeOut"; }
	virtual void DoStamp(StampAnnot* a) { fResult = "Stamp"; }
	virtual void DoInk(InkAnnot* a) { fResult = "Ink"; }
	virtual void DoPopup(PopupAnnot* a) { fResult = "Popup"; }
	virtual void DoFileAttachment(FileAttachmentAnnot* a) { fResult = "FileAttachment"; }
	virtual void DoSound(SoundAnnot* a) { fResult = "Sound"; }
	virtual void DoMovie(MovieAnnot* a) { fResult = "Movie"; }
	virtual void DoWidget(WidgetAnnot* a) { fResult = "Widget"; }
	virtual void DoPrinterMark(PrinterMarkAnnot* a) { fResult = "PrinterMark"; }
	virtual void DoTrapNet(TrapNetAnnot* a) { fResult = "TrapNet"; }

	const char* GetResult() { return fResult; }

private:
	const char* fResult;
};

class BePDFAcroForm {
private:
	static PDFStandardFonts* fStandardFonts;
	Ref fRef;
	GooString fAppearance;
	free_text_justification fJustification;
	std::list<PDFFont*> fFonts;

	void ParseFont(const char* shortName, Ref ref, Dict* font);

public:
	BePDFAcroForm(XRef* xref, Object* acroForm);
	~BePDFAcroForm();

	static PDFStandardFonts* GetStandardFonts();

	Ref GetRef() { return fRef; }
	void SetRef(Ref ref) { fRef = ref; }

	const char* GetAppearance() { return fAppearance.c_str(); }
	void SetAppearance(const char* a) { fAppearance.clear(); fAppearance.append(a); }

	free_text_justification GetJustification() const { return fJustification; }
	void SetJustification(free_text_justification j) { fJustification = j; }

	std::list<PDFFont*>* GetFonts() { return &fFonts; }
	PDFFont* FindFontByShortName(const char* name);
};

typedef Annotation* AnnotationPtr;

class Annotations {
private:
	int fMax;
	int fLength;
	AnnotationPtr* fAnnots;

	void Resize(int max);

public:
	Annotations(Annotations* copy);
	Annotations(Object* annots, BePDFAcroForm* acroForm);
	~Annotations();

	int Length() const { return fLength; }
	Annotation* At(int i) const { return fAnnots[i]; }
	void Append(Annotation* a);
	Annotation* Remove(int i);

	void Iterate(AnnotVisitor* visitor);
	Annotation* OverAnnotation(double x, double y, bool edit = false);
	void Sort(AnnotSorter* sort); // not thread safe!!!
	bool HasChanged() const;
};

typedef Annotations* AnnotationsPtr;


class AnnotsList {
	AnnotationsPtr* fAnnots;
	int fLength;

	void MakeEmpty();

public:
	AnnotsList(AnnotsList* copy);
	AnnotsList();
	~AnnotsList();
	void SetSize(int size);
	Annotations* Get(int i) const;
	void Set(int i, Annotations* a);
	bool HasChanged() const;
};

// we are interested in font name and size only
class AppearanceStringParser {
public:
	AppearanceStringParser(const char* as);

	bool IsOK() const { return fOK; }
	const char* GetFontName() { return fFontName.c_str(); }
	float GetFontSize() const { return fFontSize; }
	GfxRGB* GetColor() { return &fColor; }

private:
	bool fOK;
	GooString fFontName;
	float fFontSize;
	GfxRGB fColor;
};

class AnnotUtils {
public:
	// helper functions
	static bool InUCS2(const GooString* s);
	static GooString* EscapeString(const GooString* text);
	static GooString* EscapeName(const char* text);
	static void CurrentDate(GooString* date);
};

#endif
