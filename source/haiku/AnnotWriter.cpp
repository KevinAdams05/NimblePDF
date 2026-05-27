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

#include "Logging.h"
#include "AnnotWriter.h"

#include <Debug.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>


// Implementation of XRefTable

XRefTable::XRefTable(XRef* xref)
    : fXRef(xref),
      fLength(xref->getSize() + INITIAL_INCREMENT),
      fSize(xref->getSize()),
      fEntries((XRefEntry*)malloc(sizeof(XRefEntry) * fLength))
{
	for (int i = 0; i < fSize; i++) {
		fEntries[i] = *xref->getEntry(i);
	}
}

XRefTable::~XRefTable()
{
	free(fEntries);
	fEntries = NULL;
}

// increase fEntries if necessary only
void XRefTable::Resize(int l)
{
	fSize = l;
	if (fLength < l) {
		fLength = l + INCREMENT;
		fEntries = (XRefEntry*)realloc(fEntries, sizeof(XRefEntry) * fLength);
		ASSERT(fEntries != NULL);
	}
}

bool XRefTable::InRange(int num)
{
	return num >= 0 && num < fSize;
}

XRefEntry* XRefTable::GetUnusedHead()
{
	XRefEntry* head = GetXRef(0);
	ASSERT(head->gen == DEAD_GEN && head->type == xrefEntryFree);
	return head;
}

// simply insert "e" after the head entry and its successor
void XRefTable::InsertInUnusedList(int num, XRefEntry* e)
{
	XRefEntry* head = GetUnusedHead();
	e->offset = head->offset;
	head->offset = num;
}

// return (available) successor of head entry in unused list
Ref XRefTable::ActivateUnusedEntry(XRefEntryType type)
{
	ASSERT(type != xrefEntryFree);
	XRefEntry* head = GetUnusedHead();
	XRefEntry* prev;
	XRefEntry* cur = head;
	do {
		prev = cur;
		cur = GetXRef(head->offset); // skip "dead" entries
	} while (cur != head && cur->gen == DEAD_GEN);

	if (cur != head) {
		// create a Ref for current entry
		Ref ref;
		ref.num = prev->offset;
		ref.gen = cur->gen;
		// unlink entry from list
		prev->offset = cur->offset;
		// mark entry as used
		cur->type = type;
		cur->offset = (Guint)-1;
		return ref;
	} else {
		return empty_ref;
	}
}

XRefEntry* XRefTable::GetXRef(int num)
{
	ASSERT(InRange(num));
	return &fEntries[num];
}

int XRefTable::GetSize()
{
	return fSize;
}

bool XRefTable::HasChanged(int num)
{
	ASSERT(InRange(num));
	if (num >= fXRef->getSize())
		return true;
	if (num == 0)
		return true;
	XRefEntry* o = fXRef->getEntry(num);
	XRefEntry* n = GetXRef(num);
	return o->offset != n->offset || o->gen != n->gen || o->type != n->type;
}


void XRefTable::DeleteRef(Ref ref)
{
	XRefEntry* e = GetXRef(ref.num);
	ASSERT(e->type != xrefEntryFree && e->gen == ref.gen && e->gen != DEAD_GEN);
	e->gen++;
	e->type = xrefEntryFree;
	InsertInUnusedList(ref.num, e);
}

Ref XRefTable::AppendNewRef(XRefEntryType type)
{
	ASSERT(type != xrefEntryFree);
	Ref ref;
	ref.num = fSize;
	ref.gen = 0;
	Resize(fSize + 1);
	XRefEntry* e = GetXRef(ref.num);
	e->offset = (Guint)-1;
	e->gen = ref.gen;
	e->type = type;
	return ref;
}

Ref XRefTable::GetNewRef(XRefEntryType type)
{
	ASSERT(type != xrefEntryFree);
	Ref ref = ActivateUnusedEntry(type);
	if (!is_empty_ref(ref))
		return ref;
	return AppendNewRef(type);
}

void XRefTable::SetOffset(Ref ref, int offset)
{
	XRefEntry* e = GetXRef(ref.num);
	e->offset = offset;
}

bool XRefTable::NextGroup(int first, int* num, int* nof)
{
	int n = GetSize();
	while (first < n && !HasChanged(first))
		first++;
	bool found = first < n;
	if (found) {
		*num = first;
		do {
			first++;
		} while (first < n && HasChanged(first));
		*nof = first - *num;
	}
	return found;
}

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
AnnotWriter::AnnotWriter(XRef* xref, PDFDoc* doc, AnnotsList* list, BePDFAcroForm* acroForm)
    : fDoc(doc),
      fAnnots(list) // make a copy
      ,
      fBePDFAcroForm(acroForm),
      fXRef(xref),
      fXRefTable(xref),
      fASRef(empty_ref),
      fInfoRef(empty_ref)
{}

AnnotWriter::~AnnotWriter()
{}

void AnnotWriter::Write(const char* s)
{
	fprintf(fFile, "%s", s);
}

void AnnotWriter::Write(GooString* s)
{
	fwrite(s->c_str(), s->size(), 1, fFile);
}

void AnnotWriter::Write(Ref r)
{
	fprintf(fFile, "%d %d", r.num, r.gen);
}

void AnnotWriter::WriteCr()
{
	Write("\r");
}

void AnnotWriter::WriteCrLf()
{
	Write("\r\n");
}

int AnnotWriter::Tell()
{
	return ftell(fFile);
}

// Convert xpdf Object to PDF output

void AnnotWriter::InsertWhiteSpace(Object* obj)
{
	bool startsWithDelimiter;
	switch (obj->getType()) {
	case objString:
	case objArray:
	case objDict:
		startsWithDelimiter = true;
		break;
	default:
		startsWithDelimiter = false;
	}
	if (!startsWithDelimiter)
		Write(" ");
}


void AnnotWriter::WriteObject(Object* obj)
{
	ASSERT(fFile != NULL);
	int i;
	Object o;
	GooString* s = NULL;

	switch (obj->getType()) {
		// simple objects
	case objBool:
		fprintf(fFile, "%s", obj->getBool() ? "true" : "false");
		break;
	case objInt:
		fprintf(fFile, "%d", obj->getInt());
		break;
	case objReal:
		fprintf(fFile, "%g", obj->getReal());
		break;
	case objString:
		if (AnnotUtils::InUCS2(obj->getString())) {
			s = AnnotUtils::EscapeString(obj->getString());
			Write("(");
			Write(s);
			Write(")");
		} else {
			s = AnnotUtils::EscapeString(obj->getString());
			fprintf(fFile, "(%s)", s->c_str());
		}
		break;
	case objName:
		s = AnnotUtils::EscapeName(obj->getName());
		fprintf(fFile, "/%s", s->c_str());
		break;
	case objNull:
		Write("null");
		break;

	// complex objects
	case objArray:
		Write("[");
		for (i = 0; i < obj->arrayGetLength(); i++) {
			o = obj->arrayGetNF(i);
			if (i > 0)
				InsertWhiteSpace(&o);
			WriteObject(&o);
		}
		Write("]");
		break;
	case objDict:
		Write("<<");
		for (i = 0; i < obj->dictGetLength(); i++) {
			if (i > 0)
				WriteCr();
			fprintf(fFile, "/%s", obj->dictGetKey(i));
			o = obj->dictGetValNF(i);
			InsertWhiteSpace(&o);
			WriteObject(&o);
		}
		Write(">>");
		break;
	case objStream:
		fflush(fFile);
		Trace(LOG_ERR, "Cannot serialize stream object");
		ASSERT(false);
		break;
	case objRef:
		Write(obj->getRef());
		Write(" R");
		break;
	default:
		fflush(fFile);
		Trace(LOG_ERR, "WriteObj: unknown object type %d", obj->getType());
		obj->print(stderr);
		Trace(LOG_DEBUG, "\n");
		ASSERT(false);
	}
	delete s;
}

void AnnotWriter::WriteObject(Ref ref, Object* obj, GooString* stream)
{
	fXRefTable.SetOffset(ref, Tell());
	Write(ref);
	Write(" obj");
	WriteCr();
	WriteObject(obj);
	WriteCr();
	if (stream != NULL) {
		Write("stream");
		WriteCr();
		Write(stream);
		Write("endstream");
		WriteCr();
	}
	Write("endobj");
	WriteCr();
}

bool AnnotWriter::WriteXRefTable()
{
	WriteCr();
	fXRefOffset = ftell(fFile);
	Write("xref\r");
	int first = 0;
	int nof;
	while (fXRefTable.NextGroup(first, &first, &nof)) {
		// write start num and count
		fprintf(fFile, "%d %d\r", first, nof);
		for (int i = 0; i < nof; i++) {
			XRefEntry* x = fXRefTable.GetXRef(i + first);
			ASSERT(x->offset >= 0);
			// write offset, gen and used or unused char
			fprintf(fFile, "%10.10lld %5.5d %c\r\n", x->offset, x->gen, x->type != xrefEntryFree ? 'n' : 'f');
		}
		first += nof;
	}
	return true;
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
	out->initDict(fXRef);
	int n = in->dictGetLength();
	for (int i = 0; i < n; i++) {
		char* key = in->dictGetKey(i);
		if (excludeKeys == NULL || !IsInList(key, excludeKeys)) {
			Object val;
			val = in->dictGetValNF(i);
			out->dictAdd(copyString(key), &val);
		}
	}
}

// Update modification date

Ref AnnotWriter::GetModDateRef(Ref infoDictRef)
{
	Ref dateRef = empty_ref;
	if (!is_empty_ref(infoDictRef)) {
		Object ref, dict;
		ref.initRef(infoDictRef.num, infoDictRef.gen);
		ref.fetch(fXRef, &dict);
		if (dict.isDict())
			HasRef(&dict, "ModDate", dateRef);
	}
	return dateRef;
}

Ref AnnotWriter::GetInfoDictRef()
{
	Ref ref;
	HasRef(fXRef->getTrailerDict(), "Info", ref);
	return ref;
}

static const char* infoDictExcludeKeys[] = {"ModDate", NULL};

void AnnotWriter::CopyInfoDict(Object* dict)
{
	ASSERT(!is_empty_ref(fInfoRef));
	Object info;
	info = fXRef->getTrailerDict()->dictLookup("Info");
	CopyDict(&info, dict, infoDictExcludeKeys);
}

void AnnotWriter::WriteModDate(Ref ref)
{
	GooString* date = new GooString();
	AnnotUtils::CurrentDate(date);

	Object obj;
	obj.initString(date);
	WriteObject(ref, &obj);
}

void AnnotWriter::UpdateInfoDict()
{
	fInfoRef = GetInfoDictRef();
	Ref modDate = GetModDateRef(fInfoRef);
	if (is_empty_ref(modDate)) {
		modDate = fXRefTable.GetNewRef(xrefEntryUncompressed);
		Object info, val;
		if (is_empty_ref(fInfoRef)) {
			fInfoRef = fXRefTable.GetNewRef(xrefEntryUncompressed);
			info.initDict(fXRef);
		} else {
			CopyInfoDict(&info);
		}
		info.dictAdd(copyString("ModDate"), val.initRef(modDate.num, modDate.gen));
		WriteObject(fInfoRef, &info);
	}
	WriteModDate(modDate);
}

static const char* fileTrailerExcludeKeys[] = {"Size", "Prev", "Root", "Info", NULL};

bool AnnotWriter::WriteFileTrailer()
{
	Write("trailer\r");
	Object trailer;
	Object val;
	CopyDict(fXRef->getTrailerDict(), &trailer, fileTrailerExcludeKeys);
	trailer.dictAdd(copyString("Size"), val.initInt(fXRefTable.GetSize()));
	trailer.dictAdd(copyString("Prev"), val.initInt(fXRef->getLastXRefPos()));
	trailer.dictAdd(copyString("Root"), val.initRef(fXRef->getRootNum(), fXRef->getRootGen()));
	trailer.dictAdd(copyString("Info"), val.initRef(fInfoRef.num, fInfoRef.gen));
	WriteObject(&trailer);
	Write("\rstartxref\r");
	fprintf(fFile, "%d\r", fXRefOffset);
	Write("%%EOF\r");
	return true;
}

bool AnnotWriter::CopyFile(const char* name)
{
	GooString n(name);
	return fDoc->saveAs(&n);
}

bool AnnotWriter::HasRef(Object* dict, const char* key, Ref& ref)
{
	Object obj;
	bool ok = true;
	ASSERT(dict && dict->isDict());
	if (dict->dictLookupNF((char*)key, &obj) && obj.isRef()) {
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

bool AnnotWriter::HasEmbeddedContent(Object* page)
{
	ASSERT(page && page->isDict());
	Object obj;
	bool embedded = !(page->dictLookupNF("Contents", &obj) && (obj.isArray() || obj.isRef() || obj.isNull()));
	return embedded;
}

bool AnnotWriter::CopyContentStream(Object* page)
{
	// not implemented yet!!!
	return false;
}

static const char* pageDictExcludeKeys[] = {"Annots", NULL};

bool AnnotWriter::CopyPage(Object* page, Ref pageRef, Ref arrayRef)
{
	Object copy;
	Object ar;

	ar.initRef(arrayRef.num, arrayRef.gen);
	CopyDict(page, &copy, pageDictExcludeKeys);
	copy.dictAdd(copyString("Annots"), &ar);

	// write to file
	WriteObject(pageRef, &copy);
	return true;
}

bool AnnotWriter::UpdatePage(int pageNo, Annotations* annots, Ref& annotArray)
{
	bool ok = false;
	Object page;
	Ref* pageRef = fDoc->getCatalog()->getPageRef(pageNo + 1);
	if (!fXRef->fetch(pageRef->num, pageRef->gen, &page)->isNull()) {
		if (HasAnnotRef(&page, annotArray))
			return true;
		annotArray = fXRefTable.GetNewRef(xrefEntryUncompressed);
		if (HasEmbeddedContent(&page)) {
			if (!CopyContentStream(&page)) {
				Trace(LOG_ERR, "Could not copy content stream");
				goto error;
			}
		}
		if (!CopyPage(&page, *pageRef, annotArray)) {
			Trace(LOG_ERR, "Could not copy page");
		} else {
			ok = true;
		}
	} else {
		Trace(LOG_ERR, "Could not get page dict for page %d", pageNo + 1);
	}
error:
	return ok;
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
			a->SetRef(fXRefTable.GetNewRef(xrefEntryUncompressed));
			r = a->GetRef();
		} else {
			return;
		}
	}
	Object ref;
	ref.initRef(r.num, r.gen);
	array->arrayAdd(&ref);
}

bool AnnotWriter::UpdateAnnotArray(int pageNo, Annotations* annots, Ref annotArray)
{
	ASSERT(annots->HasChanged());
	Object array;
	array.initArray(fXRef);
	for (int i = 0; i < annots->Length(); i++) {
		Annotation* a = annots->At(i);
		if (!a->IsDeleted()) {
			AddToAnnots(&array, a);
			if (a->GetPopup())
				AddToAnnots(&array, a->GetPopup());
		}
	}
	// write to file
	WriteObject(annotArray, &array);
	return true;
}

bool AnnotWriter::WriteAS(Ref& ref, Annotation* a)
{
	if (is_empty_ref(ref))
		return true;

	Object xobj;
	xobj.initDict(fXRef);
	// setup XObject dictionary
	AddName(&xobj, "Type", "XObject");
	AddName(&xobj, "Subtype", "Form");
	AddInteger(&xobj, "FormType", 1);
	PDFRectangle r = *a->GetRect();
	r.x2 -= r.x1;
	r.y2 -= r.y1;
	r.x1 = r.y1 = 0;
	AddRect(&xobj, "BBox", &r);
	// setup resource dictionary
	Object resources, array, name;
	resources.initDict(fXRef);
	array.initArray(fXRef);
	name.initName("PDF");
	array.arrayAdd(&name);
	resources.dictAdd(copyString("ProcSet"), &array);
	xobj.dictAdd(copyString("Resources"), &resources);

	// create appearance stream
	BePDFAnnotAppearance as;
	a->Visit(&as);

	// set length
	AddInteger(&xobj, "Length", as.GetLength());
	ASSERT(as.GetLength() > 0);

	// write form XObject
	WriteObject(ref, &xobj, as.GetStream());
	ref = empty_ref;
	return true;
}


bool AnnotWriter::UpdateAnnot(Annotation* annot)
{
	if (annot->HasChanged()) {
		Ref ref = annot->GetRef();
		ASSERT(!is_empty_ref(ref));
		fASRef = empty_ref;
		fAnnot.initDict(fXRef);
		AddName(&fAnnot, "Type", "Annot");
		annot->Visit(this);
		DoAnnotation(annot);
		WriteObject(ref, &fAnnot);
		WriteAS(fASRef, annot);
	}
	if (annot->GetPopup() != NULL) {
		return UpdateAnnot(annot->GetPopup());
	}
	return true;
}

// Create new PDF file and append changed or new annotations
bool AnnotWriter::WriteTo(const char* name)
{
	if (!CopyFile(name))
		return false;
	if (!fAnnots.HasChanged())
		return true;
	AssignShortFontNames();
	fFile = fopen(name, "a+b");
	bool ok = fFile != NULL;
	int numPages = fDoc->getNumPages();
	for (int i = 0; ok && i < numPages; i++) {
		fPageRef = *fDoc->getCatalog()->getPageRef(i + 1);
		Annotations* a = fAnnots.Get(i);
		if (a && a->HasChanged()) {
			Ref annotArray;
			ok = ok && UpdatePage(i, a, annotArray);
			ok = ok && UpdateAnnotArray(i, a, annotArray);
			for (int j = 0; ok && j < a->Length(); j++) {
				Annotation* an = a->At(j);
				if (!an->IsDeleted()) {
					if (CanWrite(an)) {
						ok = UpdateAnnot(an);
					}
				} else {
					if (!is_empty_ref(an->GetRef())) {
						fXRefTable.DeleteRef(an->GetRef());
					}
				}
			}
		}
	}
	if (ok) {
		UpdateInfoDict();
		UpdateBePDFAcroForm();
		UpdateCatalog();
		ok = WriteXRefTable();
		ok = ok && WriteFileTrailer();
	}
	if (fFile) {
		fclose(fFile);
		fFile = NULL;
	}
	if (!ok) {
		// delete file on error
		unlink(name);
	}
	UnassignShortFontNames();
	return ok;
}


void AnnotWriter::AddRef(Object* dict, char* key, Ref ref)
{
	ASSERT(dict->isDict());
	Object n;
	n.initRef(ref.num, ref.gen);
	dict->dictAdd(copyString(key), &n);
}


void AnnotWriter::AddBool(Object* dict, char* key, bool b)
{
	ASSERT(dict->isDict());
	Object n;
	n.initBool(b);
	dict->dictAdd(copyString(key), &n);
}


void AnnotWriter::AddName(Object* dict, char* key, char* name)
{
	ASSERT(dict->isDict());
	Object n;
	n.initName(name);
	dict->dictAdd(copyString(key), &n);
}


void AnnotWriter::AddString(Object* dict, char* key, GooString* string)
{
	ASSERT(dict->isDict());
	Object n;
	n.initString(new GooString(string));
	dict->dictAdd(copyString(key), &n);
}


void AnnotWriter::AddString(Object* dict, char* key, char* string)
{
	ASSERT(dict->isDict());
	Object n;
	n.initString(new GooString(string));
	dict->dictAdd(copyString(key), &n);
}


void AnnotWriter::AddInteger(Object* dict, char* key, int i)
{
	ASSERT(dict->isDict());
	Object n;
	n.initInt(i);
	dict->dictAdd(copyString(key), &n);
}


void AnnotWriter::AddReal(Object* dict, char* key, double r)
{
	ASSERT(dict->isDict());
	Object n;
	n.initReal(r);
	dict->dictAdd(copyString(key), &n);
}


void AnnotWriter::AddReal(Object* array, double r)
{
	ASSERT(array->isArray());
	Object n;
	n.initReal(r);
	array->arrayAdd(&n);
}


void AnnotWriter::AddRect(Object* dict, char* key, PDFRectangle* rect)
{
	ASSERT(dict->isDict());
	Object a;
	a.initArray(fXRef);
	AddReal(&a, rect->x1);
	AddReal(&a, rect->y1);
	AddReal(&a, rect->x2);
	AddReal(&a, rect->y2);
	dict->dictAdd(copyString(key), &a);
}


void AnnotWriter::AddColor(Object* dict, char* key, GfxRGB* c)
{
	Object a;
	a.initArray(fXRef);
	AddReal(&a, colToDbl(c->r));
	AddReal(&a, colToDbl(c->g));
	AddReal(&a, colToDbl(c->b));
	dict->dictAdd(copyString(key), &a);
}


void AnnotWriter::AddDict(Object* dict, char* key, Object* d)
{
	ASSERT(dict->isDict());
	dict->dictAdd(copyString(key), d);
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
	BePDFAnnotAppearance ap;
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
			popup->SetRef(fXRefTable.GetNewRef(xrefEntryUncompressed));
		}
		AddRef(&fAnnot, "Popup", popup->GetRef());
	}
	if (HasAppearanceStream(a)) {
		fASRef = fXRefTable.GetNewRef(xrefEntryUncompressed);
		Object ap;
		ap.initDict(fXRef);
		AddRef(&ap, "N", fASRef);
		AddDict(&fAnnot, "AP", &ap);
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
	bs.initDict(fXRef);
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
	array.initArray(fXRef);
	for (int i = 0; i < m->QuadPointsLength(); i++) {
		PDFQuadPoints* q = m->QuadPointsAt(i);
		for (int j = 0; j < 4; j++) {
			PDFPoint p = (*q)[j];
			Object val;
			array.arrayAdd(val.initReal(p.x));
			array.arrayAdd(val.initReal(p.y));
		}
	}
	fAnnot.dictAdd(copyString("QuadPoints"), &array);
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
	array.initArray(fXRef);
	Object val;
	PDFPoint* line = a->GetLine();
	array.arrayAdd(val.initReal(line[0].x));
	array.arrayAdd(val.initReal(line[0].y));
	array.arrayAdd(val.initReal(line[1].x));
	array.arrayAdd(val.initReal(line[1].y));
	fAnnot.dictAdd(copyString("L"), &array);
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
		std::list<PDFFont*>* fonts = fBePDFAcroForm->GetFonts();
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
	PDFStandardFonts* stdFonts = BePDFAcroForm::GetStandardFonts();
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
	font->SetRef(fXRefTable.GetNewRef(xrefEntryUncompressed));
	fWrittenFonts.push_back(font);
	Object dict;
	dict.initDict(fXRef);
	AddName(&dict, "Type", "Font");
	AddName(&dict, "Subtype", "Type1");
	AddName(&dict, "BaseFont", (char*)font->GetName());
	AddName(&dict, "Encoding", "WinAnsiEncoding");
	WriteObject(font->GetRef(), &dict);
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

void AnnotWriter::UpdateBePDFAcroForm()
{
	if (fWrittenFonts.empty())
		return;

	Object acroForm;
	Object oldDR;

	acroForm.initDict(fXRef);
	oldDR.initNull();

	if (is_empty_ref(fBePDFAcroForm->GetRef())) {
		Ref fieldsRef = fXRefTable.GetNewRef(xrefEntryUncompressed);
		// create empty array for fields
		Object fields;
		fields.initArray(fXRef);
		WriteObject(fieldsRef, &fields);

		// create new BePDFAcroForm
		fBePDFAcroFormRef = fXRefTable.GetNewRef(xrefEntryUncompressed);
		AddName(&acroForm, "Type", "BePDFAcroForm");
		AddRef(&acroForm, "Fields", fieldsRef);
	} else {
		// copy existing BePDFAcroForm except DR
		fBePDFAcroFormRef = fBePDFAcroForm->GetRef();
		Object ref;
		Object oldForm;
		ref.initRef(fBePDFAcroFormRef.num, fBePDFAcroFormRef.gen);
		ref.fetch(fXRef, &oldForm);
		CopyDict(&oldForm, &acroForm, acroFormExcludeKeys);
		oldDR = oldForm.dictLookup("DR");
	}
	// Add DR to BePDFAcroForm
	Object dr;
	dr.initDict(fXRef);
	if (oldDR.isDict()) {
		CopyDict(&oldDR, &dr, drExcludeKeys);
	}
	// Add font dict
	Object font;
	font.initDict(fXRef);
	// add old fonts
	AddFonts(&font, fBePDFAcroForm->GetFonts());
	// add new fonts
	AddFonts(&font, &fWrittenFonts);
	AddDict(&dr, "Font", &font);
	AddDict(&acroForm, "DR", &dr);
	WriteObject(fBePDFAcroFormRef, &acroForm);
}

void AnnotWriter::UpdateCatalog()
{
	// Return if BePDFAcroForm has not been written or ref exists already in Catalog
	if (is_empty_ref(fBePDFAcroFormRef) || !is_empty_ref(fBePDFAcroForm->GetRef()))
		return;
	// Copy catalog and add ref to new BePDFAcroForm
	Ref root;
	Object oldCatalogRef;
	Object oldCatalog;
	Object catalog;
	root.num = fXRef->getRootNum();
	root.gen = fXRef->getRootGen();
	oldCatalogRef.initRef(root.num, root.gen);
	oldCatalogRef.fetch(fXRef, &oldCatalog);
	catalog.initDict(fXRef);
	CopyDict(&oldCatalog, &catalog);
	AddRef(&catalog, "BePDFAcroForm", fBePDFAcroFormRef);
	WriteObject(root, &catalog);
}
