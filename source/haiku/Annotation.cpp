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

#include <stdlib.h>
#include <stdio.h>
#include <Rect.h>
#include <ctype.h>
#include <Debug.h>
#include <stack>

#include "Logging.h"
#include "Annotation.h"

// Local convenience macro for the legacy LOG("string") call sites in this
// file. Routes to the project-wide syslog logger.
#define LOG(text) Trace(LOG_DEBUG, "%s", text)


#define COPYN(N)                   \
	for (n = N; n && date[i]; n--) \
		s[j++] = date[i++];

// to_date() is used in PDFView.cpp
const char* to_date(const char* date, char* s)
{
	if ((date[0] == 'D') && (date[1] == ':')) {
		int i = 2;
		// skip spaces
		while (date[i] == ' ')
			i++;
		int from = i;
		while (date[i] && isdigit(date[i]))
			i++;
		int to = i;
		int j = 0, n;
		i = from;
		if (to - from > 12)
			COPYN(to - from - 10)
		else
			COPYN(to - from - 4);
		s[j++] = '/';
		COPYN(2)
		s[j++] = '/';
		COPYN(2)
		s[j++] = ' ';
		if (date[i]) {
			COPYN(2);
			s[j++] = ':';
			COPYN(2);
			s[j++] = ':';
			COPYN(2);
			if (date[i]) {
				s[j++] = ' ';
				s[j++] = date[i++];
				COPYN(2);
				i++; // skip '
				s[j++] = ':';
				COPYN(2);
			}
		}
		s[j] = 0;
		return s;
	} else
		return date;
}

Ref empty_ref = {0, 65535};

bool is_empty_ref(Ref ref)
{
	return ref.num == empty_ref.num && ref.gen == empty_ref.gen;
}

// Implementation of PDFFont
PDFFont::PDFFont()
    : fRef(empty_ref)
{}

Ref PDFFont::GetRef() const
{
	return fRef;
}

const char* PDFFont::GetName()
{
	return fName.c_str();
}

const char* PDFFont::GetShortName()
{
	return fShortName.c_str();
}

void PDFFont::SetRef(Ref ref)
{
	fRef = ref;
}

void PDFFont::SetName(const char* name)
{
	fName.clear()->append(name);
}

void PDFFont::SetShortName(const char* name)
{
	fShortName.clear()->append(name);
}

void PDFFont::Print()
{
	Trace(LOG_DEBUG, "Name: %s\n", GetName());
	Trace(LOG_DEBUG, "Short name: %s\n", GetShortName());
}

// Implementation of PDFStandardFonts
const char* PDFStandardFonts::fNames[] = {
    "Times-Roman",
    "Times-Bold",
    "Times-Italic",
    "Times-BoldItalic",
    "Helvetica",
    "Helvetica-Bold",
    "Helvetica-Oblique",
    "Helvetica-BoldOblique",
    "Courier",
    "Courier-Bold",
    "Courier-Oblique",
    "Courier-BoldOblique",
    //	"Symbol",
    //	"ZapfDingbats",
};

PDFStandardFonts::PDFStandardFonts()
{
	ASSERT(sizeof(fNames) / sizeof(const char*) == num_of_standard_fonts);
	int i;
	for (i = 0; i < num_of_standard_fonts; i++) {
		fFonts[i].SetName(fNames[i]);
	}
}

void PDFStandardFonts::Reset()
{
	int i;
	for (i = 0; i < num_of_standard_fonts; i++) {
		PDFFont* font = FontAt(i);
		font->SetRef(empty_ref);
		font->SetShortName("");
	}
}

int PDFStandardFonts::CountFonts() const
{
	return num_of_standard_fonts;
}

PDFFont* PDFStandardFonts::FontAt(int i)
{
	ASSERT(i >= 0 && i < num_of_standard_fonts);
	return &fFonts[i];
}

PDFFont* PDFStandardFonts::FindByName(const char* name)
{
	for (int i = 0; i < num_of_standard_fonts; i++) {
		if (strcmp(fFonts[i].GetName(), name) == 0) {
			return &fFonts[i];
		}
	}
	return NULL;
}

PDFFont* PDFStandardFonts::FindByShortName(const char* name)
{
	for (int i = 0; i < num_of_standard_fonts; i++) {
		if (strcmp(fFonts[i].GetShortName(), name) == 0) {
			return &fFonts[i];
		}
	}
	return NULL;
}

// Implementation of PDFPoint
PDFPoint PDFPoint::operator+(const PDFPoint& p) const
{
	return PDFPoint(this->x + p.x, this->y + p.y);
}

PDFPoint PDFPoint::operator-(const PDFPoint& p) const
{
	return PDFPoint(this->x - p.x, this->y - p.y);
}

bool PDFPoint::operator==(const PDFPoint& p) const
{
	return this->x == p.x && this->y == p.y;
}

bool PDFPoint::operator!=(const PDFPoint& p) const
{
	return !(*this == p);
}

PDFPoint& PDFPoint::operator+=(const PDFPoint& p)
{
	*this = *this + p;
	return *this;
}

PDFPoint& PDFPoint::operator-=(const PDFPoint& p)
{
	*this = *this - p;
	return *this;
}

// Implementation of PDFPoints
PDFPoints::PDFPoints(PDFPoints* copy)
    : fLength(copy->fLength),
      fPoints(NULL)
{
	*this = *copy;
}

PDFPoints& PDFPoints::operator=(PDFPoints& p)
{
	if (this != &p) {
		delete[] fPoints;
		fLength = p.fLength;
		if (fLength > 0) {
			fPoints = new PDFPoint[fLength];
			for (int i = 0; i < fLength; i++)
				fPoints[i] = p.fPoints[i];
		} else {
			fPoints = NULL;
		}
	}
	return *this;
}

// Implementation of BorderStyle
BorderStyle::BorderStyle()
    : fWidth(1),
      fStyle(solid_style)
{
	// intentionally empty
}

void BorderStyle::Set(Dict* d, bool check_type)
{
	Object obj;

	// set default values
	fWidth = 1;
	fStyle = solid_style;

	// sanity check
	if (check_type && d->lookup("Type", &obj) && !obj.isNull()) {
		if (!obj.isName() || strcmp(obj.getName(), "Border") != 0) {
			LOG("Dict is not of type Border!\n");
			obj.free();
			return;
		}
	}
	obj.free();

	// border width in points
	if (d->lookup("W", &obj) && obj.isNum()) {
		fWidth = (int)obj.getNum();
	} else {
	}
	obj.free();

	// border style
	if (d->lookup("S", &obj) && obj.isName()) {
		const char* n = obj.getName();
		switch (n[0]) {
		case 'S':
			fStyle = solid_style;
			break;
		case 'D':
			fStyle = dashed_style;
			break;
		case 'B':
			fStyle = beveled_style;
			break;
		case 'I':
			fStyle = inset_style;
			break;
		case 'U':
			fStyle = underline_style;
			break;
		}
	}
	obj.free();

	// dash array not supported yet!
}

template <class C> static C* Copy(C* s)
{
	if (s) {
		return new C(s);
	} else {
		return NULL;
	}
}

// Implementation of AnnotVisitor
AnnotVisitor::AnnotVisitor()
{
	// no op
}

AnnotVisitor::~AnnotVisitor()
{
	// no op
}

// Implementation of Annotation
Annotation::Annotation(PDFRectangle r)
    : fRef(empty_ref),
      fContents(""),
      fRect(r),
      fDate(NULL),
      fHasAppearanceStream(false),
      fHasColor(true),
      fOpacity(1),
      fTitle(NULL),
      fPopup(NULL),
      fValid(true),
      fDeleted(false),
      fChanged(true),
      fSelected(false)
{
	fColor.r = fColor.g = fColor.b = 0;
	fFlags.Set(print_flag);
}

Annotation::Annotation(Annotation* copy)
    : fRef(copy->fRef),
      fContents(copy->fContents),
      fRect(copy->fRect),
      fDate(Copy(copy->fDate)),
      fFlags(copy->fFlags),
      fHasAppearanceStream(copy->fHasAppearanceStream),
      fColor(copy->fColor),
      fHasColor(copy->fHasColor),
      fOpacity(copy->fOpacity),
      fTitle(Copy(copy->fTitle)),
      fPopup(Copy(copy->fPopup)),
      fValid(copy->fValid),
      fDeleted(copy->fDeleted),
      fChanged(copy->fChanged),
      fSelected(copy->fSelected)
{}

Annotation::Annotation(Dict* d)
    : fRef(empty_ref),
      fDate(NULL),
      fHasAppearanceStream(false),
      fHasColor(true),
      fOpacity(1),
      fTitle(NULL)

      ,
      fPopup(NULL),
      fValid(false),
      fDeleted(false),
      fChanged(false),
      fSelected(false)
{
	fRect.x1 = fRect.x2 = fRect.y1 = fRect.y2 = 0;
	fOpacity = 1;
	fColor.r = fColor.g = fColor.b = 0;

	Object obj;
	if (d->lookup("Contents", &obj) && obj.isString()) {
		fContents.append(obj.getString());
	}
	obj.free();

	if (d->lookup("M", &obj) && obj.isString()) {
		fDate = new GooString(obj.getString()->c_str());
	}
	obj.free();

	if (d->lookup("Rect", &obj) && obj.isArray() && obj.arrayGetLength() == 4) {
		Array* a = obj.getArray();

		if (ReadNum(a, 0, fRect.x1) && ReadNum(a, 1, fRect.y1) && ReadNum(a, 2, fRect.x2) && ReadNum(a, 3, fRect.y2)) {
		} else
			goto error;
	} else
		goto error;
	obj.free();

	if (d->lookup("F", &obj) && obj.isInt()) {
		fFlags.Set(obj.getInt());
	}
	obj.free();

	fHasAppearanceStream = d->lookup("AP", &obj) && !obj.isNull();
	obj.free();

	if (d->lookup("C", &obj) && obj.isArray() && obj.arrayGetLength() == 3) {
		Array* a = obj.getArray();
		double r, g, b;
		if (ReadNum(a, 0, r) && ReadNum(a, 1, g) && ReadNum(a, 2, b)) {
			fColor.r = dblToCol(r);
			fColor.g = dblToCol(g);
			fColor.b = dblToCol(b);
		} else {
			goto error;
		}
	}
	obj.free();

	if (d->lookup("CA", &obj) && obj.isNum()) {
		fOpacity = obj.getNum();
	}
	obj.free();

	if (d->lookup("T", &obj) && obj.isString()) {
		fTitle = new GooString(obj.getString());
	}
	obj.free();

	if (d->lookup("Popup", &obj) && obj.isDict()) {
		PopupAnnot* popup = new PopupAnnot(obj.getDict());
		if (popup->IsValid())
			LOG("Popup is valid\n");
		else
			LOG("Popup is invalid\n");
		if (popup->IsValid()) {
			fPopup = popup;
			Object ref;
			if (d->lookupNF("Popup", &ref) && obj.isRef()) {
				Ref r = ref.getRef();
				popup->SetRef(r);
			}
			ref.free();
		} else
			delete popup;
	}
	obj.free();

	SetValid();
	return;

error:
	obj.free();
}

Annotation::~Annotation()
{
	delete fDate;
	delete fTitle;
	delete fPopup;
}

bool Annotation::ReadNum(Array* a, int i, double& d)
{
	Object n;
	bool ok = a->get(i, &n) && n.isNum();
	if (ok) {
		d = n.getNum();
	}
	n.free();
	return ok;
}

bool Annotation::ReadPoint(Array* a, int i, PDFPoint* p)
{
	return ReadNum(a, i, p->x) && ReadNum(a, i + 1, p->y);
}

void Annotation::Print()
{
	if (is_empty_ref(fRef))
		Trace(LOG_DEBUG, "Empty Ref\n");
	else
		Trace(LOG_DEBUG, "Ref num %d gen %d\n", fRef.num, fRef.gen);
	Trace(LOG_DEBUG, "Contents:\n%s\n", GetContents()->c_str());
	Trace(LOG_DEBUG, "Date: %s\n", GetDate());
	Trace(LOG_DEBUG, "Rect: %f %f %f %f\n", GetRect()->x1, GetRect()->y1, GetRect()->x2, GetRect()->y2);
	Trace(LOG_DEBUG, "Flags: ");
	if (GetFlags()->Invisible())
		Trace(LOG_DEBUG, "Invisible, ");
	if (GetFlags()->Hidden())
		Trace(LOG_DEBUG, "Hidden, ");
	if (GetFlags()->Print())
		Trace(LOG_DEBUG, "Print, ");
	if (GetFlags()->NoZoom())
		Trace(LOG_DEBUG, "NoZoom, ");
	if (GetFlags()->NoRotate())
		Trace(LOG_DEBUG, "NoRotate, ");
	if (GetFlags()->NoView())
		Trace(LOG_DEBUG, "NoView, ");
	if (GetFlags()->ReadOnly())
		Trace(LOG_DEBUG, "ReadOnly, ");
	Trace(LOG_DEBUG, "\n");
	Trace(LOG_DEBUG, "HasAppearanceStream: %s\n", HasAppearanceStream() ? "true" : "false");
	Trace(LOG_DEBUG, "Color: %d %d %d\n", int(GetColor()->r * 255), int(GetColor()->g * 255), int(GetColor()->b * 255));
	Trace(LOG_DEBUG, "Color: %f %f %f\n", float(GetColor()->r), float(GetColor()->g), float(GetColor()->b));
	Trace(LOG_DEBUG, "Opacity: %d\n", int(GetOpacity() * 255));
	Trace(LOG_DEBUG, "\n");
	Trace(LOG_DEBUG, "Title: %s\n", GetTitle() ? GetTitle()->c_str() : "NULL");
	if (fPopup) {
		Trace(LOG_DEBUG, "---Popup:\n");
		fPopup->Print();
	}
}

void Annotation::SetRef(const Ref ref)
{
	fRef = ref;
}

void Annotation::SetContents(GooString* c)
{
	fContents.clear()->append(c);
}

void Annotation::SetString(GooString*& s, const char* t)
{
	if (t) {
		if (s) {
			s->clear()->append(t);
		} else {
			s = new GooString(t);
		}
	} else {
		delete s;
		s = NULL;
	}
}

void Annotation::SetDate(const char* date)
{
	SetString(fDate, date);
}

void Annotation::SetTitle(GooString* title)
{
	if (fTitle) {
		fTitle->clear()->append(title);
	} else {
		fTitle = new GooString(title);
	}
}

void Annotation::MoveTo(PDFPoint p)
{
	fRect.x2 = p.x + fRect.x2 - fRect.x1;
	fRect.x1 = p.x;
	fRect.y1 = p.y - fRect.y2 + fRect.y1;
	fRect.y2 = p.y;
}

void Annotation::ResizeTo(double w, double h)
{
	fRect.x2 = fRect.x1 + w;
	fRect.y1 = fRect.y2 - h;
}


// Implementation of TextAnnot
TextAnnot::TextAnnot(PDFRectangle rect, text_annot_type type)
    : Annotation(rect),
      fOpen(true),
      fName(fTypeNames[type]),
      fType(type)
{
	GfxRGB* c = GetColor();
	c->r = dblToCol(1.0);
	c->g = dblToCol(1.0);
	c->b = dblToCol(0.0);
	GetFlags()->Set(print_flag | no_zoom_flag | no_rotate_flag);
}

TextAnnot::TextAnnot(TextAnnot* copy)
    : Annotation(copy),
      fOpen(copy->fOpen),
      fName(copy->fName),
      fType(copy->fType)
{}

const char* TextAnnot::fTypeNames[TextAnnot::no_of_types - 1] = {"Comment", "Help", "Insert", "Key", "NewParagraph", "Note", "Paragraph"};

TextAnnot::TextAnnot(Dict* d)
    : Annotation(d),
      fOpen(false),
      fName("Note"),
      fType(note_type)
{
	Object obj;
	if (d->lookup("Open", &obj) && obj.isBool()) {
		fOpen = obj.getBool();
	}
	obj.free();

	if (d->lookup("Name", &obj) && obj.isName()) {
		fName.clear()->append(obj.getName());
		fType = unknown_type;
		for (int i = 0; i < no_of_types - 1; i++) {
			if (strcmp(obj.getName(), fTypeNames[i]) == 0) {
				fType = (enum text_annot_type)i;
				break;
			}
		}
	}
	obj.free();
}

void TextAnnot::Print()
{
	Trace(LOG_DEBUG, "Text\n");
	Annotation::Print();
	Trace(LOG_DEBUG, "Name: %s\n", GetName());
	Trace(LOG_DEBUG, "Open: %s\n", IsOpen() ? "true" : "false");
}

LinkAnnot::LinkAnnot(LinkAnnot* copy)
    : Annotation(copy),
      fLinkAction(NULL)
{}


LinkAnnot::LinkAnnot(Dict* d)
    : Annotation(d),
      fLinkAction(NULL)
{
	Object obj;

	// look for destination
	if (d->lookup("Dest", &obj) && !obj.isNull()) {
		fLinkAction = LinkAction::parseDest(&obj);
		// look for action
	} else if (d->lookup("A", &obj) && obj.isDict()) {
		fLinkAction = LinkAction::parseAction(&obj /*, baseURI*/);
	}
	obj.free();
}

void LinkAnnot::Print()
{
	Trace(LOG_DEBUG, "Link\n");
	Annotation::Print();
}

free_text_justification ToFreeTextJustification(const char* name)
{
	if (strcmp(name, "left") == 0)
		return left_justify;
	if (strcmp(name, "centered") == 0)
		return centered;
	if (strcmp(name, "right") == 0)
		return right_justify;
	return left_justify; // default
}

const char* ToString(free_text_justification j)
{
	switch (j) {
	case left_justify:
		return "left";
	case centered:
		return "centered";
	case right_justify:
		return "right";
	default:
		return "left";
	}
}

// Implementation of FreeTextAnnot

void FreeTextAnnot::Init()
{
	SetHasColor(false);
	GfxRGB* c = GetColor();
	c->r = dblToCol(1.0);
	c->g = dblToCol(1.0);
	c->b = dblToCol(1.0);
	fFontColor.r = dblToCol(0.0);
	fFontColor.g = dblToCol(0.0);
	fFontColor.b = dblToCol(0.0);
}

FreeTextAnnot::FreeTextAnnot(PDFRectangle rect, PDFFont* font)
    : StyledAnnot(rect, 1.0, 1.0, 1.0),
      fJustification(left_justify),
      fFont(font),
      fFontSize(12)
{
	Init();
	GetBorderStyle()->SetWidth(0.0);
}

FreeTextAnnot::FreeTextAnnot(FreeTextAnnot* copy)
    : StyledAnnot(copy),
      fAppearance(copy->fAppearance),
      fJustification(copy->fJustification),
      fFont(copy->fFont),
      fFontColor(copy->fFontColor),
      fFontSize(copy->fFontSize)
{}

FreeTextAnnot::FreeTextAnnot(Dict* d, BePDFAcroForm* acroForm)
    : StyledAnnot(d),
      fAppearance(""),
      fJustification(left_justify),
      fFont(NULL),
      fFontSize(12)
{
	Init();

	Object obj;
	if (d->lookup("DA", &obj) && obj.isString()) {
		fAppearance.append(obj.getString());
	}
	obj.free();

	fJustification = acroForm->GetJustification(); // inherit property from BePDFAcroForm
	if (d->lookup("Q", &obj) && obj.isInt()) {
		int j = obj.getInt();
		if (j >= left_justify && j <= right_justify)
			fJustification = (free_text_justification)j;
	}
	obj.free();

	// use appearance string to extract font short name and size
	const char* appearance;
	if (fAppearance.cmp("") != 0) {
		appearance = fAppearance.c_str();
	} else {
		// inherit it from BePDFAcroForm if it is not defined in annotation
		appearance = acroForm->GetAppearance();
	}

	AppearanceStringParser asp(appearance);
	if (asp.IsOK()) {
		fFontSize = asp.GetFontSize();
		fFontColor = *asp.GetColor();
		fFont = acroForm->GetStandardFonts()->FindByShortName(asp.GetFontName());
		if (fFont == NULL) {
			// try to find a matching font
			PDFFont* font = acroForm->FindFontByShortName(asp.GetFontName());
			if (font) {
				fFont = acroForm->GetStandardFonts()->FindByName(font->GetName());
			}
		}
	}

	// assign default font
	if (fFont == NULL) {
		fFont = acroForm->GetStandardFonts()->FindByName("Helvetica");
		ASSERT(fFont != NULL);
	}
}

void FreeTextAnnot::Print()
{
	Trace(LOG_DEBUG, "FreeText\n");
	Annotation::Print();
	Trace(LOG_DEBUG, "Appearance: %s\n", GetAppearance()->c_str());
	Trace(LOG_DEBUG, "Justification: %s", ToString(fJustification));
	Trace(LOG_DEBUG, "Font: ");
	if (fFont) {
		Trace(LOG_DEBUG, "\n");
		fFont->Print();
	} else {
		Trace(LOG_DEBUG, "<null>\n");
	}
}

void FreeTextAnnot::SetAppearance(GooString* ap)
{
	fAppearance.clear()->append(ap);
}

void FreeTextAnnot::SetJustification(free_text_justification j)
{
	fJustification = j;
}

void FreeTextAnnot::SetFont(PDFFont* font)
{
	fFont = font;
}

// Implementation of StyledAnnot
StyledAnnot::StyledAnnot(PDFRectangle rect, float r, float g, float b)
    : Annotation(rect)
{
	GfxRGB* c = GetColor();
	c->r = dblToCol(r);
	c->g = dblToCol(g);
	c->b = dblToCol(b);
}

StyledAnnot::StyledAnnot(StyledAnnot* copy)
    : Annotation(copy),
      fStyle(copy->fStyle)
{}

StyledAnnot::StyledAnnot(Dict* d)
    : Annotation(d)
{
	Object obj;
	if (d->lookup("BS", &obj) && obj.isDict()) {
		fStyle.Set(obj.getDict());
	}
	obj.free();
}

void StyledAnnot::Print()
{
	Annotation::Print();
	Trace(LOG_DEBUG, "BorderStyle:\n");
	Trace(LOG_DEBUG, "  Width: %d\n", GetBorderStyle()->GetWidth());
	Trace(LOG_DEBUG, "  Style: ");
	switch (GetBorderStyle()->GetStyle()) {
	case BorderStyle::solid_style:
		Trace(LOG_DEBUG, "solid");
		break;
	case BorderStyle::dashed_style:
		Trace(LOG_DEBUG, "dashed");
		break;
	case BorderStyle::beveled_style:
		Trace(LOG_DEBUG, "beveled");
		break;
	case BorderStyle::inset_style:
		Trace(LOG_DEBUG, "inset");
		break;
	case BorderStyle::underline_style:
		Trace(LOG_DEBUG, "underline");
		break;
	default:
		Trace(LOG_DEBUG, "unknown");
		break;
	};
	Trace(LOG_DEBUG, "\n");
}

// Implementation of LineAnnot
LineAnnot::LineAnnot(PDFRectangle rect, PDFPoint* line)
    : StyledAnnot(rect, 0, 0, 0)
{
	fLine[0] = line[0];
	fLine[1] = line[1];
}

LineAnnot::LineAnnot(LineAnnot* copy)
    : StyledAnnot(copy)
{
	fLine[0] = copy->fLine[0];
	fLine[1] = copy->fLine[1];
}

LineAnnot::LineAnnot(Dict* d)
    : StyledAnnot(d)
{
	if (CheckInvalid())
		return;

	Object obj;
	if (d->lookup("L", &obj) && obj.isArray() && obj.arrayGetLength() == 4) {
		Array* a = obj.getArray();
		if (ReadPoint(a, 0, &fLine[0]) && ReadPoint(a, 2, &fLine[1])) {
		} else
			goto error;
	} else {
		goto error;
	}
	obj.free();
	SetValid();
	return;
error:
	obj.free();
}

void LineAnnot::Print()
{
	Trace(LOG_DEBUG, "Line\n");
	StyledAnnot::Print();
}


void SquareAnnot::Print()
{
	Trace(LOG_DEBUG, "Square\n");
	StyledAnnot::Print();
}

void CircleAnnot::Print()
{
	Trace(LOG_DEBUG, "Circle\n");
	StyledAnnot::Print();
}

// Implementation of MarkupAnnot
MarkupAnnot::MarkupAnnot(PDFRectangle rect, float r, float g, float b, PDFQuadPoints* p, int l)
    : StyledAnnot(rect, r, g, b),
      fLength(l),
      fQuadPoints(p)
{}

MarkupAnnot::MarkupAnnot(MarkupAnnot* copy)
    : StyledAnnot(copy),
      fLength(copy->fLength),
      fQuadPoints(NULL)
{
	if (fLength > 0) {
		fQuadPoints = new PDFQuadPoints[fLength];
		for (int i = 0; i < fLength; i++) {
			fQuadPoints[i] = copy->fQuadPoints[i];
		}
	}
}


MarkupAnnot::MarkupAnnot(Dict* d)
    : StyledAnnot(d),
      fLength(0),
      fQuadPoints(NULL)
{
	if (CheckInvalid())
		return;

	Object qp;
	if (d->lookup("QuadPoints", &qp) && qp.isArray() && qp.arrayGetLength() % 8 == 0) {
		fLength = qp.arrayGetLength() / 8;
		if (fLength == 0)
			goto error;

		fQuadPoints = new PDFQuadPoints[fLength];
		int index = 0;
		for (int j = 0; j < fLength; j++) {
			PDFQuadPoints* quad = &fQuadPoints[j];
			for (int i = 0; i < 4; i++) {
				if (!ReadPoint(qp.getArray(), index, &quad->q[i]))
					goto error;
				index += 2;
			}
		}
	} else
		goto error;
	qp.free();


	SetValid();
	return;

error:
	qp.free();
	delete[] fQuadPoints;
}

MarkupAnnot::~MarkupAnnot()
{
	delete[] fQuadPoints;
}

void MarkupAnnot::Print()
{
	Trace(LOG_DEBUG, "Markup\n");
	StyledAnnot::Print();
}

void MarkupAnnot::MoveTo(PDFPoint p)
{
	PDFPoint d(p.x - GetRect()->x1, p.y - GetRect()->y2);
	for (int i = 0; i < fLength; i++) {
		PDFQuadPoints& p = fQuadPoints[i];
		p.q[0] += d;
		p.q[1] += d;
		p.q[2] += d;
		p.q[3] += d;
	}
	StyledAnnot::MoveTo(p);
}

void MarkupAnnot::ResizeTo(double w, double h)
{
	double oldW = GetRect()->x2 - GetRect()->x1;
	double oldH = GetRect()->y2 - GetRect()->y1;
	PDFPoint origin(GetRect()->x1, GetRect()->y2);
	for (int i = 0; i < fLength; i++) {
		PDFQuadPoints& q = fQuadPoints[i];
		for (int j = 0; j < 4; j++) {
			PDFPoint& p = q[j];
			p -= origin;
			p.x *= w / oldW;
			p.y *= h / oldH;
			p += origin;
		}
	}
	StyledAnnot::ResizeTo(w, h);
}

// Implementation of HighlightAnnot

static PDFQuadPoints* QuadPoints(PDFRectangle rect)
{
	PDFQuadPoints* ps = new PDFQuadPoints[1];
	PDFQuadPoints& p = *ps;
	p[2] = PDFPoint(rect.x1, rect.y1);
	p[3] = PDFPoint(rect.x2, rect.y1);
	p[0] = PDFPoint(rect.x1, rect.y2);
	p[1] = PDFPoint(rect.x2, rect.y2);
	return ps;
}

static int NofQuadPoints()
{
	return 1;
}

HighlightAnnot::HighlightAnnot(PDFRectangle rect)
    : MarkupAnnot(rect, 1.0, 0.992, 0.18, QuadPoints(rect), NofQuadPoints())
{}

// Implementation of UnderlineAnnot

UnderlineAnnot::UnderlineAnnot(PDFRectangle rect)
    : MarkupAnnot(rect, 0.0, 0.5, 0.0, QuadPoints(rect), NofQuadPoints())
{}

// Implementation of SquigglyAnnot

SquigglyAnnot::SquigglyAnnot(PDFRectangle rect)
    : MarkupAnnot(rect, 0.0, 0.5, 0.0, QuadPoints(rect), NofQuadPoints())
{}

// Implementation of StrikeOutAnnot
StrikeOutAnnot::StrikeOutAnnot(PDFRectangle rect)
    : MarkupAnnot(rect, 0.702, 0.0015, 0.0, QuadPoints(rect), NofQuadPoints())
{}

// Implementation of StampAnnot
StampAnnot::StampAnnot(StampAnnot* copy)
    : Annotation(copy),
      fName(copy->fName)
{}

StampAnnot::StampAnnot(Dict* d)
    : Annotation(d),
      fName("Draft")
{
	Object obj;
	if (d->lookup("Name", &obj) && obj.isName()) {
		fName.clear()->append(obj.getName());
	}
	obj.free();
}

void StampAnnot::Print()
{
	Trace(LOG_DEBUG, "StampAnnot\n");
	Annotation::Print();
	Trace(LOG_DEBUG, "Name: %s\n", GetName());
}

// Implementation of InkAnnot
InkAnnot::InkAnnot(InkAnnot* copy)
    : StyledAnnot(copy),
      fLength(copy->fLength),
      fInkList(NULL)
{
	if (fLength > 0) {
		fInkList = new PDFPoints[fLength];
		for (int i = 0; i < fLength; i++)
			fInkList[i] = copy->fInkList[i];
	}
}

InkAnnot::InkAnnot(Dict* d)
    : StyledAnnot(d),
      fLength(0),
      fInkList(NULL)
{
	if (CheckInvalid())
		return;
	Object obj;
	if (d->lookup("InkList", &obj) && obj.isArray()) {
		Array* a = obj.getArray();
		fLength = a->size();
		fInkList = new PDFPoints[fLength];
		for (int i = 0; i < fLength; i++) {
			PDFPoints* p = PathAt(i);
			Object obj2;
			if (a->get(i, &obj2) && obj2.isArray() && obj2.arrayGetLength() % 2 == 0) {
				const int n = obj2.arrayGetLength() / 2;
				p->SetLength(n);
				for (int j = 0; j < n; j++) {
					if (!ReadPoint(obj2.getArray(), 2 * j, p->PointAt(j))) {
						obj2.free();
						goto error;
					}
				}
			} else {
				obj2.free();
				goto error;
			}
			obj2.free();
		}
	} else
		goto error;
	obj.free();
	SetValid();
	return;

error:
	obj.free();
}

InkAnnot::~InkAnnot()
{
	if (fInkList) {
		delete[] fInkList;
	}
}

void InkAnnot::Print()
{
	Trace(LOG_DEBUG, "Ink\n");
	StyledAnnot::Print();
}

// Implementation of PopupAnnot
PopupAnnot::PopupAnnot(PDFRectangle rect)
    : Annotation(rect),
      fParentRef(empty_ref)
{
	GetFlags()->Set(print_flag | no_zoom_flag | no_rotate_flag);
}

PopupAnnot::PopupAnnot(PopupAnnot* copy)
    : Annotation(copy),
      fParentRef(empty_ref)
{}

PopupAnnot::PopupAnnot(Dict* annot)
    : Annotation(annot),
      fParentRef(empty_ref)
{
	if (CheckInvalid())
		return;

	SetValid();
}

void PopupAnnot::Print()
{
	Trace(LOG_DEBUG, "Popup\n");
	Annotation::Print();
}

// Implementation of FileAttachmentAnnot
const char* FileAttachmentAnnot::fTypeNames[FileAttachmentAnnot::no_of_types - 1] = {"Graphic", "PaperClip", "PushPin", "Tag"};

FileAttachmentAnnot::FileAttachmentAnnot(FileAttachmentAnnot* copy)
    : Annotation(copy),
      fName(copy->fName),
      fFileName(copy->fFileName),
      fType(copy->fType)
{}

FileAttachmentAnnot::FileAttachmentAnnot(Dict* annot)
    : Annotation(annot),
      fName("PushPin"),
      fType(push_pin_type)
{
	if (CheckInvalid())
		return;

	Object obj;
	if (annot->lookup("Name", &obj) && obj.isName()) {
		fName.clear()->append(obj.getName());
		fType = unknown_type;
		for (int i = 0; i < no_of_types - 1; i++) {
			if (strcmp(obj.getName(), fTypeNames[i]) == 0) {
				fType = (enum attachment_type)i;
				break;
			}
		}
	}
	obj.free();

	// File specification
	if (annot->lookup("FS", &obj) && obj.isDict() && obj.dictIs("Filespec")) {
		fFileSpec.SetTo(obj.getDict());
	}
	obj.free();

	SetValid();
}

bool FileAttachmentAnnot::Save(XRef* xref, const char* file)
{
	return fFileSpec.Save(xref, file);
}

void FileAttachmentAnnot::Print()
{
	Trace(LOG_DEBUG, "FileAttachment\n");
	Annotation::Print();
	Trace(LOG_DEBUG, "Name: %s\n", GetName());
}


// Implementation of AnnotSorter

AnnotSorter::AnnotSorter()
{}

AnnotSorter::~AnnotSorter()
{}


// Implementation of AnnotName

AnnotName::AnnotName()
{}

AnnotName::~AnnotName()
{}


// Implementation of BePDFAcroForm

PDFStandardFonts* BePDFAcroForm::fStandardFonts = NULL;

PDFStandardFonts* BePDFAcroForm::GetStandardFonts()
{
	if (fStandardFonts == NULL) {
		fStandardFonts = new PDFStandardFonts();
	}
	return fStandardFonts;
}


BePDFAcroForm::BePDFAcroForm(XRef* xref, Object* acroFormRef)
    : fRef(empty_ref),
      fJustification(left_justify)
{
	GetStandardFonts()->Reset();
	if (!acroFormRef->isRef())
		return;

	Object acroForm;
	if (acroFormRef->fetch(xref, &acroForm) == NULL)
		return;


	if (!acroForm.isDict()) {
		acroForm.free();
		return;
	}

	fRef = acroFormRef->getRef();

	Object obj;
	Dict* d = acroForm.getDict();

	// default appearance string
	if (d->lookup("DA", &obj) && obj.isString()) {
		fAppearance.append(obj.getString());
	}
	obj.free();

	// default quadding
	if (d->lookup("Q", &obj) && obj.isInt()) {
		int j = obj.getInt();
		if (j >= left_justify && j <= right_justify)
			fJustification = (free_text_justification)j;
	}
	obj.free();

	if (d->lookup("DR", &obj) && obj.isDict()) {
		Dict* resources = obj.getDict();
		Object obj2;
		if (resources->lookup("Font", &obj2) && obj2.isDict()) {
			Dict* font = obj2.getDict();
			for (int i = 0; i < font->size(); i++) {
				Object obj3, obj4;
				if (font->getVal(i, &obj3) && obj3.isDict() && font->getValNF(i, &obj4) && obj4.isRef()) {
					ParseFont(font->getKey(i), obj4.getRef(), obj3.getDict());
				}
				obj3.free();
				obj4.free();
			}
		}
		obj2.free();
	}
	obj.free();
}

BePDFAcroForm::~BePDFAcroForm()
{
	std::list<PDFFont*>::iterator it;
	for (it = fFonts.begin(); it != fFonts.end(); it++) {
		PDFFont* font = *it;
		delete font;
	}
}

void BePDFAcroForm::ParseFont(const char* shortName, Ref ref, Dict* dict)
{
	bool isType1;
	GooString baseFont;
	GooString encoding;
	bool hasSupportedEncoding = false;
	PDFFont* font;
	int firstChar = 0;
	int lastChar = 255;

	Object obj;
	if (dict->lookup("Type", &obj) == NULL || !obj.isName() || strcmp(obj.getName(), "Font") != 0) {
		goto error;
	}
	obj.free();

	isType1 = dict->lookup("Subtype", &obj) && obj.isName() && strcmp(obj.getName(), "Type1") == 0;
	obj.free();

	if (dict->lookup("FirstChar", &obj) && obj.isNum()) {
		firstChar = (int)obj.getNum();
	}
	obj.free();

	if (dict->lookup("LastChar", &obj) && obj.isNum()) {
		lastChar = (int)obj.getNum();
	}
	obj.free();

	if (dict->lookupNF("Encoding", &obj) && obj.isName()) {
		encoding.append(obj.getName());
		if (encoding.cmp("WinAnsiEncoding") == 0) {
			hasSupportedEncoding = true;
		}
	}
	obj.free();

	if (dict->lookup("BaseFont", &obj) && obj.isName()) {
		baseFont.append(obj.getName());
	} else {
		goto error;
	}
	obj.free();

	// add font to list
	font = new PDFFont();
	font->SetRef(ref);
	font->SetName(baseFont.c_str());
	font->SetShortName(shortName);
	fFonts.push_back(font);

	// If it is a standard font set reference to it and its short name
	// Note: AnnotWriter must not write a referenced standard font.
	if (isType1 && lastChar - firstChar == 255 && hasSupportedEncoding) {
		font = GetStandardFonts()->FindByName(baseFont.c_str());
		if (font != NULL) {
			if (is_empty_ref(font->GetRef())) {
				font->SetRef(ref);
				font->SetShortName(shortName);
			}
		}
	}

	return;

error:
	obj.free();
}

PDFFont* BePDFAcroForm::FindFontByShortName(const char* name)
{
	std::list<PDFFont*>::iterator it;
	for (it = fFonts.begin(); it != fFonts.end(); it++) {
		PDFFont* font = *it;
		if (strcmp(font->GetShortName(), name) == 0) {
			return font;
		}
	}
	return NULL;
}

// Implementation of Annotations
Annotations::Annotations(Annotations* copy)
    : fMax(copy->fLength),
      fLength(fMax),
      fAnnots(NULL)
{
	if (fLength > 0) {
		fAnnots = new AnnotationPtr[fMax];
		for (int i = 0; i < fLength; i++) {
			fAnnots[i] = copy->At(i)->Clone();
		}
	}
}

Annotations::Annotations(Object* annots, BePDFAcroForm* acroForm)
    : fMax(0),
      fLength(0),
      fAnnots(NULL)
{
	if (!annots->isArray() || annots->arrayGetLength() <= 0)
		return;

	fMax = annots->arrayGetLength();
	fAnnots = new AnnotationPtr[fMax];

	for (int i = 0; i < fMax; i++)
		fAnnots[i] = NULL;

	for (int i = 0; i < annots->arrayGetLength(); i++) {
		Ref ref = empty_ref;
		Object r;
		if (annots->arrayGetNF(i, &r) && r.isRef()) {
			ref = r.getRef();
		}
		Object annot;
		if (annots->arrayGet(i, &annot) && annot.isDict()) {
			Object type; // optional, "Annot" if present
			if (annot.dictLookup("Type", &type) && (type.isNull() || type.isName()) && type.isName("Annot")) {
				Object subType;
				if (annot.dictLookup("Subtype", &subType) && subType.isName()) {
					Annotation* a = NULL;
					if (subType.isName("Text")) {
						a = new TextAnnot(annot.getDict());
					} else if (subType.isName("FreeText")) {
						a = new FreeTextAnnot(annot.getDict(), acroForm);
					} else if (subType.isName("Highlight")) {
						a = new HighlightAnnot(annot.getDict());
					} else if (subType.isName("Underline")) {
						a = new UnderlineAnnot(annot.getDict());
					} else if (subType.isName("Squiggly")) {
						a = new SquigglyAnnot(annot.getDict());
					} else if (subType.isName("StrikeOut")) {
						a = new StrikeOutAnnot(annot.getDict());
					} else if (subType.isName("Line")) {
						a = new LineAnnot(annot.getDict());
					} else if (subType.isName("Square")) {
						a = new SquareAnnot(annot.getDict());
					} else if (subType.isName("Circle")) {
						a = new CircleAnnot(annot.getDict());
					} else if (subType.isName("Stamp")) {
						a = new StampAnnot(annot.getDict());
					} else if (subType.isName("Ink")) {
						a = new InkAnnot(annot.getDict());
					} else if (subType.isName("Link")) {
						a = new LinkAnnot(annot.getDict());
					} else if (subType.isName("FileAttachment") == 0) {
						a = new FileAttachmentAnnot(annot.getDict());
					}

					if (a && a->IsValid()) {
						a->SetRef(ref);
						fAnnots[fLength++] = a;
					} else {
#ifdef DEBUG
						Trace(LOG_WARNING, "Could not parse annotation %s", s);
#endif
						delete a;
					}
				}
				subType.free();
			}
			type.free();
		} // else skip it
		r.free();
		annot.free();
	}
}


Annotations::~Annotations()
{
	if (fAnnots) {
		for (int i = 0; i < fLength; i++) {
			delete fAnnots[i];
		}
		delete[] fAnnots;
		fAnnots = NULL;
	}
}

Annotation* Annotations::OverAnnotation(double x, double y, bool edit)
{
	for (int i = 0; i < fLength; i++) {
		Annotation* a = fAnnots[i];
		if (a->IsDeleted())
			continue;
		PDFRectangle* r = a->GetRect();
		if (r->x1 <= x && x <= r->x2 && r->y1 <= y && y <= r->y2) {
			AnnotFlags* f = a->GetFlags();
			if (edit || (!f->Invisible() && !f->Hidden() && !f->NoView()))
				return a;
		}
	}
	return NULL;
}

void Annotations::Iterate(AnnotVisitor* v)
{
	for (int i = 0; i < fLength; i++) {
		fAnnots[i]->Visit(v);
	}
}

static AnnotSorter* sorter = NULL;

int cmp_func(const void* x, const void* y)
{
	AnnotationPtr* a = (AnnotationPtr*)x;
	AnnotationPtr* b = (AnnotationPtr*)y;
	(*a)->Visit(sorter);
	int i = sorter->GetResult();
	(*b)->Visit(sorter);
	if (i < sorter->GetResult())
		return -1;
	if (i == sorter->GetResult())
		return 0;
	return 1;
}

void Annotations::Sort(AnnotSorter* s)
{
	sorter = s;
	qsort(fAnnots, fLength, sizeof(AnnotationPtr), cmp_func);
}

void Annotations::Resize(int m)
{
	if (fMax < m) {
		if (fMax + 10 > m)
			m = fMax + 10;
		fMax = m;
		AnnotationPtr* a = new AnnotationPtr[fMax];
		int i = 0;
		for (; i < fLength; i++)
			a[i] = fAnnots[i];
		for (; i < fMax; i++)
			a[i] = NULL;
		delete[] fAnnots;
		fAnnots = a;
	}
}

void Annotations::Append(Annotation* a)
{
	Resize(fLength + 1);
	fAnnots[fLength] = a;
	fLength++;
}

Annotation* Annotations::Remove(int i)
{
	Annotation* a = NULL;
	if (i >= 0 && i < fLength) {
		a = At(i);
		const int n = fLength - 2;
		for (; i < n; i++)
			fAnnots[i] = fAnnots[i + 1];
		fAnnots[i] = NULL;
		fLength--;
	}
	return a;
}

bool Annotations::HasChanged() const
{
	for (int i = 0; i < Length(); i++) {
		Annotation* a = At(i);
		if (a->HasChanged())
			return true;
	}
	return false;
}

// Implementation of AnnotsList
AnnotsList::AnnotsList(AnnotsList* copy)
    : fAnnots(NULL),
      fLength(copy->fLength)
{
	if (fLength > 0) {
		fAnnots = new AnnotationsPtr[fLength];
		for (int i = 0; i < fLength; i++) {
			Annotations* a = copy->Get(i);
			fAnnots[i] = a ? new Annotations(a) : NULL;
		}
	}
}
AnnotsList::AnnotsList()
    : fAnnots(NULL),
      fLength(0)
{}

AnnotsList::~AnnotsList()
{
	MakeEmpty();
}

void AnnotsList::MakeEmpty()
{
	if (fAnnots) {
		for (int i = 0; i < fLength; i++)
			delete fAnnots[i];
		delete[] fAnnots;
		fAnnots = NULL;
		fLength = 0;
	}
}

void AnnotsList::SetSize(int size)
{
	MakeEmpty();
	ASSERT(size >= 1);
	fLength = size;
	fAnnots = new AnnotationsPtr[size];
	for (int i = 0; i < fLength; i++)
		fAnnots[i] = NULL;
}

Annotations* AnnotsList::Get(int i) const
{
	ASSERT(0 <= i && i < fLength);
	ASSERT(fAnnots);
	return fAnnots[i];
}

void AnnotsList::Set(int i, Annotations* a)
{
	ASSERT(0 <= i && i < fLength);
	ASSERT(fAnnots);
	fAnnots[i] = a;
}

bool AnnotsList::HasChanged() const
{
	for (int i = 0; i < fLength; i++) {
		Annotations* a = Get(i);
		if (a && a->HasChanged())
			return true;
	}
	return false;
}

// Implementation of AppearanceStringParser
AppearanceStringParser::AppearanceStringParser(const char* as)
{
	char* text = strdup(as); // copy string, because it will be changed
	std::stack<const char*> operands;
	int32 length;
	char* s;
	char* end;
	fColor.r = fColor.g = fColor.b = dblToCol(0.0);

	fOK = false;
	length = strlen(text);
	s = text;
	end = s + length;
	for (; s < end && (s = strtok(s, " []\r\n\t")) != NULL; s += strlen(s) + 1) {
		if (isdigit(*s)) {
			operands.push(s);
		} else if (*s == '/') {
			operands.push(s + 1);
		} else {
			if (strcmp(s, "Tf") == 0 && operands.size() >= 2) {
				fFontSize = atof(operands.top());
				operands.pop();
				fFontName.clear()->append(operands.top());
				operands.pop();
				fOK = true;
			} else if (strcmp(s, "rg") == 0 && operands.size() >= 3) {
				fColor.b = dblToCol(atof(operands.top()));
				operands.pop();
				fColor.g = dblToCol(atof(operands.top()));
				operands.pop();
				fColor.r = dblToCol(atof(operands.top()));
				operands.pop();
			} else {
#if DEBUG
				Trace(LOG_DEBUG, "AppearanceStringParser: Unsupported operand '%s'\n", s);
#endif
			}
			if (!operands.empty()) {
#if DEBUG
				Trace(LOG_DEBUG, "AppearanceStringParser: Not all operands consumed in operation '%s'!\n", s);
#endif
			}
		}
	}
	free(text);
}

// Implementation of AnnotUtils

bool AnnotUtils::InUCS2(GooString* s)
{
	if (s->size() < 2) {
		return false;
	}
	const unsigned char* t = (const unsigned char*)s->c_str();
	return t[0] == 0xfe && t[1] == 0xff;
}

GooString* AnnotUtils::EscapeString(GooString* text)
{
	const char* t = text->c_str();
	int len = text->size();
	GooString* s = new GooString();
	while (len > 0) {
		len--;

		switch (*t) {
		case '\t':
			s->append("\\t");
			break;
		case '\n':
			s->append("\\n");
			break;
		case '\f':
			s->append("\\f");
			break;
		case '\r':
			s->append("\\r");
			break;
		case '\b': // backspace
			s->append("\\b");
			break;
		case '(':
		case ')':
		case '\\':
			s->append('\\');
			s->append(*t);
			break;
		default:
			s->append(*t);
		}
		t++;
	}
	return s;
}

GooString* AnnotUtils::EscapeName(const char* t)
{
	GooString* s = new GooString();
	char b[80];
	if (t != NULL) {
		while (*t != 0) {
			if (!isprint(*t) || isspace(*t) || *t == '/') {
				int i = *t && 0xff;
				sprintf(b, "#%2.2x", i);
			} else {
				s->append(*t);
			}
			t++;
		}
	}
	return s;
}

void AnnotUtils::CurrentDate(GooString* date)
{
	time_t tme;
	struct tm tm;
	char s[80];
	time(&tme);
	tm = *localtime(&tme);
	char o = tm.tm_gmtoff >= 0 ? '+' : '-';
	if (tm.tm_gmtoff < 0)
		tm.tm_gmtoff = -tm.tm_gmtoff;
	int min = tm.tm_gmtoff / 3600;
	int sec = (tm.tm_gmtoff / 60) % 60;
	sprintf(s,
	    "D:%4.4d%2.2d%2.2d%2.2d%2.2d%2.2d%c%2.2d'%2.2d'",
	    1900 + tm.tm_year,
	    tm.tm_mon + 1,
	    tm.tm_mday,
	    tm.tm_hour,
	    tm.tm_min,
	    tm.tm_sec,
	    o,
	    min,
	    sec);
	date->append(s);
}
