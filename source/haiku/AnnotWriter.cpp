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

#include "Logging.h"
#include "AnnotWriter.h"

#include <memory>
#include <vector>

#include <Debug.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>


// Implementation of AnnotTester
AnnotTester::AnnotTester()
    : AnnotVisitor()
{}

AnnotTester::~AnnotTester()
{}

// global function
bool CanWrite(Annotation* annot)
{
	if (annot == NULL)
		return false;
	AnnotTester tester;
	annot->Visit(&tester);
	return tester.CanWrite();
}

// Implementation of AnnotWriter
AnnotWriter::AnnotWriter(XRef* xref, PDFDoc* doc, AnnotsList* list, NimblePDFAcroForm* acroForm)
    : fDoc(doc),
      fAnnots(list), // make a copy
      fNimblePDFAcroForm(acroForm),
      fXRef(xref),
      fInfoRef(empty_ref)
{}

AnnotWriter::~AnnotWriter()
{}

Ref AnnotWriter::ReserveRef()
{
	// Reserve a slot in the in-memory XRef by inserting a placeholder null
	// object. The caller fills it in later with XRef::setModifiedObject(),
	// and PDFDoc::saveAs(writeForceRewrite) serializes the whole document.
	Object placeholder;
	placeholder.setToNull(); // Object(objNull) ctor is private in poppler 25.12
	return fXRef->addIndirectObject(placeholder);
}


// Copy a dictionary excluding specified keys

bool AnnotWriter::IsInList(const char* s, const char* list[])
{
	for (int i = 0; list[i] != NULL; i++) {
		if (strcmp(list[i], s) == 0)
			return true;
	}
	return false;
}

void AnnotWriter::CopyDict(Object* in, Object* out, const char* excludeKeys[])
{
	ASSERT(in->isDict());
	*out = Object(new Dict(fXRef));
	int n = in->dictGetLength();
	for (int i = 0; i < n; i++) {
		const char* key = in->dictGetKey(i);
		if (excludeKeys == NULL || !IsInList(key, excludeKeys)) {
			out->dictAdd(key, in->dictGetValNF(i).copy());
		}
	}
}

// Update modification date

Ref AnnotWriter::GetInfoDictRef()
{
	Ref ref;
	HasRef(fXRef->getTrailerDict(), "Info", ref);
	return ref;
}

static const char* infoDictExcludeKeys[] = {"ModDate", NULL};

// Refresh the document's /Info ModDate. With the full-rewrite save model the
// whole document is re-serialized, so we store ModDate inline in the Info dict
// (the old incremental writer stored it as a separate indirect object).
void AnnotWriter::UpdateInfoDict()
{
	std::unique_ptr<GooString> date = std::make_unique<GooString>();
	AnnotUtils::CurrentDate(date.get());

	fInfoRef = GetInfoDictRef();
	if (is_empty_ref(fInfoRef)) {
		// No Info dictionary yet: create one and link it from the trailer.
		Object info(new Dict(fXRef));
		info.dictAdd("ModDate", Object(std::move(date)));
		fInfoRef = fXRef->addIndirectObject(info);
		// VERIFY(VM): Dict::set on the trailer so saveAs emits the /Info entry.
		fXRef->getTrailerDict()->getDict()->set("Info", Object(fInfoRef));
	} else {
		// Copy the existing Info dictionary, replacing ModDate, and write back.
		Object oldInfo = fXRef->getTrailerDict()->dictLookup("Info");
		Object info(new Dict(fXRef));
		CopyDict(&oldInfo, &info, infoDictExcludeKeys);
		info.dictAdd("ModDate", Object(std::move(date)));
		fXRef->setModifiedObject(&info, fInfoRef);
	}
}

bool AnnotWriter::HasRef(Object* dict, const char* key, Ref& ref)
{
	bool ok = true;
	ASSERT(dict && dict->isDict());
	const Object& obj = dict->dictLookupNF(key);
	if (obj.isRef()) {
		ref = obj.getRef();
	} else {
		ref = empty_ref;
		ok = false;
	}
	return ok;
}

bool AnnotWriter::HasAnnotRef(Object* page, Ref& annotRef)
{
	return HasRef(page, "Annots", annotRef);
}

static const char* pageDictExcludeKeys[] = {"Annots", NULL};

bool AnnotWriter::CopyPage(Object* page, Ref pageRef, Ref arrayRef)
{
	Object copy;
	Object ar;

	ar = Object(Ref{arrayRef.num, arrayRef.gen});
	CopyDict(page, &copy, pageDictExcludeKeys);
	copy.dictAdd("Annots", std::move(ar));

	// The page dictionary already exists in the file, so update it in place.
	fXRef->setModifiedObject(&copy, pageRef);
	return true;
}

bool AnnotWriter::UpdatePage(int pageNo, Annotations* annots, Ref& annotArray)
{
	Ref* pageRef = fDoc->getCatalog()->getPageRef(pageNo + 1);
	Object page = fXRef->fetch(pageRef->num, pageRef->gen);
	if (page.isNull()) {
		Trace(LOG_ERR, "Could not get page dict for page %d", pageNo + 1);
		return false;
	}
	// If the page already references its annotations indirectly, reuse that
	// ref (UpdateAnnotArray rebuilds the array in place). Otherwise reserve a
	// new array ref and rewrite the page dict to point at it. The full-rewrite
	// save model re-serializes content streams for us, so no page copying.
	if (HasAnnotRef(&page, annotArray))
		return true;
	annotArray = ReserveRef();
	if (!CopyPage(&page, *pageRef, annotArray)) {
		Trace(LOG_ERR, "Could not copy page");
		return false;
	}
	return true;
}


// Annotation:
//   Deleted -> don't add it to Annots array
//   Changed
//     non_empty_ref -> add to Annots array
//     empty_ref -> add to Annots array if CanWrite
//   Otherwise -> add to Annots array

void AnnotWriter::AddToAnnots(Object* array, Annotation* a)
{
	Ref r = a->GetRef();
	if (is_empty_ref(r)) {
		if (CanWrite(a)) {
			a->SetRef(ReserveRef());
			r = a->GetRef();
		} else {
			return;
		}
	}
	Object ref;
	ref = Object(Ref{r.num, r.gen});
	array->arrayAdd(std::move(ref));
}

bool AnnotWriter::UpdateAnnotArray(int pageNo, Annotations* annots, Ref annotArray)
{
	ASSERT(annots->HasChanged());
	Object array;
	array = Object(new Array(fXRef));
	for (int i = 0; i < annots->Length(); i++) {
		Annotation* a = annots->At(i);
		if (!a->IsDeleted()) {
			AddToAnnots(&array, a);
			if (a->GetPopup())
				AddToAnnots(&array, a->GetPopup());
		}
	}
	// The annotation array already exists (or was reserved) in the XRef.
	fXRef->setModifiedObject(&array, annotArray);
	return true;
}

// Build the appearance-stream form XObject and add it to the document.
// Returns the new object's Ref, or empty_ref if the annotation has none.
Ref AnnotWriter::WriteAS(Annotation* a)
{
	NimblePDFAnnotAppearance as;
	a->Visit(&as);
	if (as.GetLength() <= 0)
		return empty_ref;

	// Build the XObject form dictionary using the proven Add* helpers.
	Object xobj(new Dict(fXRef));
	AddName(&xobj, "Type", "XObject");
	AddName(&xobj, "Subtype", "Form");
	AddInteger(&xobj, "FormType", 1);
	PDFRectangle r = *a->GetRect();
	r.x2 -= r.x1;
	r.y2 -= r.y1;
	r.x1 = r.y1 = 0;
	AddRect(&xobj, "BBox", &r);
	// resource dictionary
	Object resources(new Dict(fXRef));
	Object procSet(new Array(fXRef));
	Object name(objName, "PDF");
	procSet.arrayAdd(std::move(name));
	resources.dictAdd("ProcSet", std::move(procSet));
	xobj.dictAdd("Resources", std::move(resources));
	// NOTE: /Length is filled in by poppler when the stream is serialized, so
	// (unlike the old hand-rolled writer) we do not add it here.

	// Hand the dictionary and stream bytes to poppler's stream-object factory.
	// addStreamObject takes ownership of one reference to the Dict, so bump the
	// refcount to balance xobj's own reference (released at end of scope).
	// VERIFY(VM): Dict::incRef visibility, addStreamObject ownership + /Length.
	GooString* stream = as.GetStream();
	std::vector<char> buffer(stream->c_str(), stream->c_str() + as.GetLength());
	Dict* dict = xobj.getDict();
	dict->incRef();
	return fXRef->addStreamObject(dict, std::move(buffer), StreamCompression::None);
}


bool AnnotWriter::UpdateAnnot(Annotation* annot)
{
	if (annot->HasChanged()) {
		Ref ref = annot->GetRef();
		ASSERT(!is_empty_ref(ref));
		fAnnot = Object(new Dict(fXRef));
		AddName(&fAnnot, "Type", "Annot");
		annot->Visit(this);
		DoAnnotation(annot);
		fXRef->setModifiedObject(&fAnnot, ref);
	}
	if (annot->GetPopup() != NULL) {
		return UpdateAnnot(annot->GetPopup());
	}
	return true;
}

// Apply all annotation changes to the in-memory PDFDoc, then serialize the
// whole document to "name" with a full rewrite (PDFDoc::saveAs).
bool AnnotWriter::WriteTo(const char* name)
{
	if (!fAnnots.HasChanged()) {
		// Nothing changed: write the document out unmodified.
		return fDoc->saveWithoutChangesAs(std::string(name)) == errNone;
	}
	AssignShortFontNames();
	bool ok = true;
	int numPages = fDoc->getNumPages();
	for (int i = 0; ok && i < numPages; i++) {
		Annotations* a = fAnnots.Get(i);
		if (a == NULL || !a->HasChanged())
			continue;
		fPageRef = *fDoc->getCatalog()->getPageRef(i + 1);
		Ref annotArray;
		ok = ok && UpdatePage(i, a, annotArray);
		ok = ok && UpdateAnnotArray(i, a, annotArray);
		for (int j = 0; ok && j < a->Length(); j++) {
			Annotation* an = a->At(j);
			if (an->IsDeleted()) {
				if (!is_empty_ref(an->GetRef()))
					fXRef->removeIndirectObject(an->GetRef());
			} else if (CanWrite(an)) {
				ok = UpdateAnnot(an);
			}
		}
	}
	if (ok) {
		UpdateInfoDict();
		UpdateNimblePDFAcroForm();
		UpdateCatalog();
	}
	UnassignShortFontNames();
	if (!ok)
		return false;
	fXRef->setModified();
	return fDoc->saveAs(std::string(name), writeForceRewrite) == errNone;
}


void AnnotWriter::AddRef(Object* dict, const char* key, Ref ref)
{
	ASSERT(dict->isDict());
	Object n;
	n = Object(Ref{ref.num, ref.gen});
	dict->dictAdd(key, std::move(n));
}


void AnnotWriter::AddBool(Object* dict, const char* key, bool b)
{
	ASSERT(dict->isDict());
	Object n;
	n = Object(static_cast<bool>(b));
	dict->dictAdd(key, std::move(n));
}


void AnnotWriter::AddName(Object* dict, const char* key, const char* name)
{
	ASSERT(dict->isDict());
	Object n;
	n = Object(objName, name);
	dict->dictAdd(key, std::move(n));
}


void AnnotWriter::AddString(Object* dict, const char* key, GooString* string)
{
	ASSERT(dict->isDict());
	Object n(std::make_unique<GooString>(string->toStr()));
	dict->dictAdd(key, std::move(n));
}


void AnnotWriter::AddString(Object* dict, const char* key, const char* string)
{
	ASSERT(dict->isDict());
	Object n(std::make_unique<GooString>(string));
	dict->dictAdd(key, std::move(n));
}


void AnnotWriter::AddInteger(Object* dict, const char* key, int i)
{
	ASSERT(dict->isDict());
	Object n;
	n = Object(i);
	dict->dictAdd(key, std::move(n));
}


void AnnotWriter::AddReal(Object* dict, const char* key, double r)
{
	ASSERT(dict->isDict());
	Object n;
	n = Object(static_cast<double>(r));
	dict->dictAdd(key, std::move(n));
}


void AnnotWriter::AddReal(Object* array, double r)
{
	ASSERT(array->isArray());
	Object n;
	n = Object(static_cast<double>(r));
	array->arrayAdd(std::move(n));
}


void AnnotWriter::AddRect(Object* dict, const char* key, PDFRectangle* rect)
{
	ASSERT(dict->isDict());
	Object a;
	a = Object(new Array(fXRef));
	AddReal(&a, rect->x1);
	AddReal(&a, rect->y1);
	AddReal(&a, rect->x2);
	AddReal(&a, rect->y2);
	dict->dictAdd(key, std::move(a));
}


void AnnotWriter::AddColor(Object* dict, const char* key, GfxRGB* c)
{
	Object a;
	a = Object(new Array(fXRef));
	AddReal(&a, colToDbl(c->r));
	AddReal(&a, colToDbl(c->g));
	AddReal(&a, colToDbl(c->b));
	dict->dictAdd(key, std::move(a));
}


void AnnotWriter::AddDict(Object* dict, const char* key, Object* d)
{
	ASSERT(dict->isDict());
	// poppler: dictAdd(std::string_view, Object&&) — move d's contents in.
	dict->dictAdd(key, std::move(*d));
}


void AnnotWriter::AddAnnotSubtype(char* type)
{
	AddName(&fAnnot, "Subtype", type);
}

void AnnotWriter::AddAnnotContents(Annotation* a)
{
	AddString(&fAnnot, "Contents", a->GetContents());
}

bool AnnotWriter::HasAppearanceStream(Annotation* a)
{
	NimblePDFAnnotAppearance ap;
	a->Visit(&ap);
	return ap.GetLength() > 0;
}

void AnnotWriter::DoAnnotation(Annotation* a)
{
	AddRect(&fAnnot, "Rect", a->GetRect());
	if (a->HasColor()) {
		AddColor(&fAnnot, "C", a->GetColor());
	}
	if (a->GetDate()[0] != 0) {
		AddString(&fAnnot, "M", (char*)a->GetDate());
	}
	AddInteger(&fAnnot, "F", a->GetFlags()->Flags());
	if (a->GetTitle() != NULL) {
		AddString(&fAnnot, "T", a->GetTitle());
	}
	if (a->GetOpacity() != 1.0) {
		AddReal(&fAnnot, "CA", a->GetOpacity());
	}
	PopupAnnot* popup = a->GetPopup();
	if (popup != NULL) {
		popup->SetParentRef(a->GetRef());
		if (is_empty_ref(popup->GetRef())) {
			popup->SetRef(ReserveRef());
		}
		AddRef(&fAnnot, "Popup", popup->GetRef());
	}
	if (HasAppearanceStream(a)) {
		Ref asRef = WriteAS(a);
		if (!is_empty_ref(asRef)) {
			Object ap;
			ap = Object(new Dict(fXRef));
			AddRef(&ap, "N", asRef);
			AddDict(&fAnnot, "AP", &ap);
		}
	}
	if (dynamic_cast<PopupAnnot*>(a) == NULL) {
		AddRef(&fAnnot, "P", fPageRef);
	}
}

void AnnotWriter::DoStyledAnnot(StyledAnnot* s)
{
	AddAnnotContents(s);
	// border style
	char* style = NULL;
	Object bs;
	bs = Object(new Dict(fXRef));
	AddName(&bs, "Type", "Border");
	AddInteger(&bs, "W", (float)s->GetBorderStyle()->GetWidth()); // width
	switch (s->GetBorderStyle()->GetStyle()) {
	case BorderStyle::solid_style:
		style = "S";
	case BorderStyle::dashed_style:
		style = "D";
	case BorderStyle::beveled_style:
		style = "B";
	case BorderStyle::inset_style:
		style = "I";
	case BorderStyle::underline_style:
		style = "U";
	}
	if (style != NULL) {
		AddName(&bs, "S", style); // border style
	}
	AddDict(&fAnnot, "BS", &bs);
}

void AnnotWriter::DoMarkupAnnot(MarkupAnnot* m)
{
	DoStyledAnnot(m);
	Object array;
	array = Object(new Array(fXRef));
	for (int i = 0; i < m->QuadPointsLength(); i++) {
		PDFQuadPoints* q = m->QuadPointsAt(i);
		for (int j = 0; j < 4; j++) {
			PDFPoint p = (*q)[j];
			Object val;
			array.arrayAdd(Object(static_cast<double>(p.x)));
			array.arrayAdd(Object(static_cast<double>(p.y)));
		}
	}
	fAnnot.dictAdd("QuadPoints", std::move(array));
}

// Annotation visitor implementation
void AnnotWriter::DoText(TextAnnot* a)
{
	AddAnnotSubtype("Text");
	AddAnnotContents(a);
	AddName(&fAnnot, "Name", (char*)a->GetName());
}


void AnnotWriter::DoLink(LinkAnnot* a)
{
	AddAnnotSubtype("Link");
}


void AnnotWriter::DoFreeText(FreeTextAnnot* a)
{
	GooString appearance;
	char buf[250];
	PDFFont* font;
	GfxRGB* color;
	// build appearance string
	font = a->GetFont();
	color = a->GetFontColor();
	appearance.clear();
	// color
	sprintf(buf, "[%g %g %g] rg ", colToDbl(color->r), colToDbl(color->g), colToDbl(color->b));
	appearance.append(buf);

	// font and size
	sprintf(buf, "/%s %g Tf", font->GetShortName(), a->GetFontSize());
	appearance.append(buf);
	a->SetAppearance(&appearance);

	DoStyledAnnot(a);
	AddAnnotSubtype("FreeText");
	AddAnnotContents(a);
	AddString(&fAnnot, "DA", a->GetAppearance());
	if (a->GetJustification() != left_justify) {
		AddInteger(&fAnnot, "Q", a->GetJustification());
	}

	WriteFont(a->GetFont());
}


void AnnotWriter::DoLine(LineAnnot* a)
{
	AddAnnotSubtype("Line");
	DoStyledAnnot(a);
	Object array;
	array = Object(new Array(fXRef));
	Object val;
	PDFPoint* line = a->GetLine();
	array.arrayAdd(Object(static_cast<double>(line[0].x)));
	array.arrayAdd(Object(static_cast<double>(line[0].y)));
	array.arrayAdd(Object(static_cast<double>(line[1].x)));
	array.arrayAdd(Object(static_cast<double>(line[1].y)));
	fAnnot.dictAdd("L", std::move(array));
}


void AnnotWriter::DoSquare(SquareAnnot* a)
{
	AddAnnotSubtype("Square");
	DoStyledAnnot(a);
}


void AnnotWriter::DoCircle(CircleAnnot* a)
{
	AddAnnotSubtype("Circle");
	DoStyledAnnot(a);
}


void AnnotWriter::DoHighlight(HighlightAnnot* a)
{
	AddAnnotSubtype("Highlight");
	DoMarkupAnnot(a);
}


void AnnotWriter::DoUnderline(UnderlineAnnot* a)
{
	AddAnnotSubtype("Underline");
	DoMarkupAnnot(a);
}


void AnnotWriter::DoSquiggly(SquigglyAnnot* a)
{
	AddAnnotSubtype("Squiggly");
	DoMarkupAnnot(a);
}


void AnnotWriter::DoStrikeOut(StrikeOutAnnot* a)
{
	AddAnnotSubtype("StrikeOut");
	DoMarkupAnnot(a);
}

void AnnotWriter::DoStamp(StampAnnot* a)
{
	AddAnnotSubtype("Stamp");
}


void AnnotWriter::DoInk(InkAnnot* a)
{
	AddAnnotSubtype("Ink");
}


void AnnotWriter::DoPopup(PopupAnnot* a)
{
	AddAnnotSubtype("Popup");
	if (!is_empty_ref(a->GetParentRef())) {
		AddRef(&fAnnot, "Parent", a->GetParentRef());
	}
}


void AnnotWriter::DoFileAttachment(FileAttachmentAnnot* a)
{
	AddAnnotSubtype("FileAttachment");
}


void AnnotWriter::DoSound(SoundAnnot* a)
{
	AddAnnotSubtype("Sound");
}


void AnnotWriter::DoMovie(MovieAnnot* a)
{
	AddAnnotSubtype("Movie");
}


void AnnotWriter::DoWidget(WidgetAnnot* a)
{
	AddAnnotSubtype("Widget");
}


void AnnotWriter::DoPrinterMark(PrinterMarkAnnot* a)
{
	AddAnnotSubtype("PrinterMark");
}


void AnnotWriter::DoTrapNet(TrapNetAnnot* a)
{
	AddAnnotSubtype("TrapNet");
}

// FreeTextAnnot
void AnnotWriter::AssignShortFontNames()
{
	// scan all fonts
	std::list<int> fontIDs;
	{
		std::list<PDFFont*>* fonts = fNimblePDFAcroForm->GetFonts();
		std::list<PDFFont*>::iterator it;
		for (it = fonts->begin(); it != fonts->end(); it++) {
			int d;
			PDFFont* font = *it;
			if (sscanf(font->GetShortName(), "F%d", &d) != 0 && d >= 0) {
				fontIDs.push_back(d);
			}
		}
	}

	// assign short names to standard fonts
	PDFStandardFonts* stdFonts = NimblePDFAcroForm::GetStandardFonts();
	int id = 0;
	std::list<int>::iterator it;
	fontIDs.sort();
	it = fontIDs.begin();
	for (int i = 0; i < stdFonts->CountFonts(); i++) {
		PDFFont* font = stdFonts->FontAt(i);
		if (strcmp(font->GetShortName(), "") == 0) {
			GooString shortName("F");
			char number[80];
			while (it != fontIDs.end() && id == *it) {
				id++;
				it++;
			}
			sprintf(number, "%d", id);
			shortName.append(number);
			font->SetShortName(shortName.c_str());
			fTemporaryFonts.push_back(font);
			id++;
		}
	}
}

void AnnotWriter::UnassignShortFontNames()
{
	// reverse steps to have proper state in case file is saved again
	std::list<PDFFont*>::iterator it;
	for (it = fTemporaryFonts.begin(); it != fTemporaryFonts.end(); it++) {
		PDFFont* font = *it;
		font->SetRef(empty_ref);
		font->SetShortName("");
	}
}

void AnnotWriter::WriteFont(PDFFont* font)
{
	if (!is_empty_ref(font->GetRef()))
		return; // already saved
	font->SetRef(ReserveRef());
	fWrittenFonts.push_back(font);
	Object dict;
	dict = Object(new Dict(fXRef));
	AddName(&dict, "Type", "Font");
	AddName(&dict, "Subtype", "Type1");
	AddName(&dict, "BaseFont", (char*)font->GetName());
	AddName(&dict, "Encoding", "WinAnsiEncoding");
	fXRef->setModifiedObject(&dict, font->GetRef());
}

void AnnotWriter::AddFonts(Object* dict, std::list<PDFFont*>* fonts)
{
	std::list<PDFFont*>::iterator it;
	for (it = fonts->begin(); it != fonts->end(); it++) {
		PDFFont* font = *it;
		ASSERT(!is_empty_ref(font->GetRef()));
		AddRef(dict, (char*)font->GetShortName(), font->GetRef());
	}
}

static const char* acroFormExcludeKeys[] = {"DR", NULL};

static const char* drExcludeKeys[] = {"Font", NULL};

void AnnotWriter::UpdateNimblePDFAcroForm()
{
	if (fWrittenFonts.empty())
		return;

	Object acroForm;
	Object oldDR;

	acroForm = Object(new Dict(fXRef));
	oldDR.setToNull();  // poppler: Object(objNull) ctor is private; use setToNull()

	if (is_empty_ref(fNimblePDFAcroForm->GetRef())) {
		Ref fieldsRef = ReserveRef();
		// create empty array for fields
		Object fields;
		fields = Object(new Array(fXRef));
		fXRef->setModifiedObject(&fields, fieldsRef);

		// create new NimblePDFAcroForm
		fNimblePDFAcroFormRef = ReserveRef();
		AddName(&acroForm, "Type", "NimblePDFAcroForm");
		AddRef(&acroForm, "Fields", fieldsRef);
	} else {
		// copy existing NimblePDFAcroForm except DR
		fNimblePDFAcroFormRef = fNimblePDFAcroForm->GetRef();
		Object ref;
		Object oldForm;
		ref = Object(Ref{fNimblePDFAcroFormRef.num, fNimblePDFAcroFormRef.gen});
		oldForm = ref.fetch(fXRef);
		CopyDict(&oldForm, &acroForm, acroFormExcludeKeys);
		oldDR = oldForm.dictLookup("DR");
	}
	// Add DR to NimblePDFAcroForm
	Object dr;
	dr = Object(new Dict(fXRef));
	if (oldDR.isDict()) {
		CopyDict(&oldDR, &dr, drExcludeKeys);
	}
	// Add font dict
	Object font;
	font = Object(new Dict(fXRef));
	// add old fonts
	AddFonts(&font, fNimblePDFAcroForm->GetFonts());
	// add new fonts
	AddFonts(&font, &fWrittenFonts);
	AddDict(&dr, "Font", &font);
	AddDict(&acroForm, "DR", &dr);
	fXRef->setModifiedObject(&acroForm, fNimblePDFAcroFormRef);
}

void AnnotWriter::UpdateCatalog()
{
	// Return if NimblePDFAcroForm has not been written or ref exists already in Catalog
	if (is_empty_ref(fNimblePDFAcroFormRef) || !is_empty_ref(fNimblePDFAcroForm->GetRef()))
		return;
	// Copy catalog and add ref to new NimblePDFAcroForm
	Ref root;
	Object oldCatalogRef;
	Object oldCatalog;
	Object catalog;
	root.num = fXRef->getRootNum();
	root.gen = fXRef->getRootGen();
	oldCatalogRef = Object(Ref{root.num, root.gen});
	oldCatalog = oldCatalogRef.fetch(fXRef);
	catalog = Object(new Dict(fXRef));
	CopyDict(&oldCatalog, &catalog);
	AddRef(&catalog, "NimblePDFAcroForm", fNimblePDFAcroFormRef);
	// The catalog already exists in the file; update it in place.
	fXRef->setModifiedObject(&catalog, root);
}
