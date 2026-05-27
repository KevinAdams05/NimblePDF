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

#ifndef ANNOT_WRITER_H
#define ANNOT_WRITER_H

// xpdf
#include <Object.h>
#include <XRef.h>
#include <Catalog.h>
#include <PDFDoc.h>

#include "Annotation.h"
#include "AnnotAppearance.h"

struct XRefItem {
	int offset; // the file offset
	int num;    // the reference number
	int gen;    // the reference generation
	bool used;  // is reference used?
};

class XRefTable {
	enum { INITIAL_INCREMENT = 30, INCREMENT = 10, DEAD_GEN = 65535 };

	XRef* fXRef;         // points to the files xref table
	int fLength;         // allocated entries
	int fSize;           // used entries
	XRefEntry* fEntries; // the updated xref table

	// resize fEntries to at least length elements
	void Resize(int length);
	bool InRange(int num);
	XRefEntry* GetUnusedHead();
	void InsertInUnusedList(int num, XRefEntry* e);
	Ref ActivateUnusedEntry(XRefEntryType type);
	Ref AppendNewRef(XRefEntryType type);

	XRefTable() {}

public:
	XRefTable(XRef* xref);
	~XRefTable();

	XRefEntry* GetXRef(int num);
	int GetSize();
	bool HasChanged(int num);

	void DeleteRef(Ref ref);
	Ref GetNewRef(XRefEntryType type);
	void SetOffset(Ref ref, int offset);

	bool NextGroup(int start, int* num, int* nof);
};

// We CAN write Line and Ink, but adding new annotations of this type is
// not implemented.
class AnnotTester : public AnnotVisitor {
public:
	AnnotTester();
	~AnnotTester();

	void DoText(TextAnnot* a) { fResult = true; }
	void DoLink(LinkAnnot* a) { fResult = false; }
	void DoFreeText(FreeTextAnnot* a) { fResult = true; }
	void DoLine(LineAnnot* a) { fResult = false; }
	void DoSquare(SquareAnnot* a) { fResult = true; }
	void DoCircle(CircleAnnot* a) { fResult = true; }
	void DoHighlight(HighlightAnnot* a) { fResult = true; }
	void DoUnderline(UnderlineAnnot* a) { fResult = true; }
	void DoSquiggly(SquigglyAnnot* a) { fResult = true; }
	void DoStrikeOut(StrikeOutAnnot* a) { fResult = true; }
	void DoStamp(StampAnnot* a) { fResult = false; }
	void DoInk(InkAnnot* a) { fResult = false; }
	void DoPopup(PopupAnnot* a) { fResult = true; }
	void DoFileAttachment(FileAttachmentAnnot* a) { fResult = false; }
	void DoSound(SoundAnnot* a) { fResult = false; }
	void DoMovie(MovieAnnot* a) { fResult = false; }
	void DoWidget(WidgetAnnot* a) { fResult = false; }
	void DoPrinterMark(PrinterMarkAnnot* a) { fResult = false; }
	void DoTrapNet(TrapNetAnnot* a) { fResult = false; }

	bool CanWrite() { return fResult; }

private:
	bool fResult;
};

bool CanWrite(Annotation* annot);

class AnnotWriter : public AnnotVisitor {
	PDFDoc* fDoc;
	AnnotsList fAnnots;
	BePDFAcroForm* fBePDFAcroForm;
	XRef* fXRef;
	XRefTable fXRefTable;
	// changed during pdf generation:
	Ref fPageRef;
	Ref fASRef;
	Ref fInfoRef;
	Ref fBePDFAcroFormRef;
	std::list<PDFFont*> fTemporaryFonts; // not already stored in old PDF file
	std::list<PDFFont*> fWrittenFonts;

	FILE* fFile;
	int fXRefOffset;
	Object fAnnot; // used by UpdateAnnot & visitor
	bool fAnnotValid;

	friend void test_annot_writer(PDFDoc* doc, int page, AnnotsList* list);

	void Write(const char* s);
	void Write(GooString* s);
	void Write(Ref ref);
	void WriteCr();
	void WriteCrLf();
	int Tell();
	void InsertWhiteSpace(Object* o);
	void WriteObject(Object* o);
	void WriteObject(Ref ref, Object* obj, GooString* stream = NULL);
	bool IsInList(char* s, char* list[]);
	void CopyDict(Object* in, Object* out, char* excludeKeys[] = NULL);
	bool WriteXRefTable();

	Ref GetModDateRef(Ref infoDictRef);
	Ref GetInfoDictRef();
	void CopyInfoDict(Object* dict);
	void GetCurrentDate(GooString* date);
	void WriteModDate(Ref ref);
	void UpdateInfoDict();
	bool WriteFileTrailer();

	/* AcroFrom:
	1. Assign unique short names to fonts
	   1.1. Take names from existing AcroFrom DR assign ref to font (done in BePDFAcroForm constructor)
	   1.2. Assign to remainig fonts names in the form /F%d
	2. Write all new or changed FreeText annotations
	   2.1. Write annotation and assign ref if necessary 
	   2.2. if Font has no ref, write font and assign a new ref (record font)
	3. If fonts have been recorded, update AcroFrom
	   3.1. If form exists copy contents except DR
	   3.2. If form does not exist create new one
	   3.3. Copy DR except Font array
	   3.4. Copy Font array and add new fonts (use info from step 2.2.)
	4. If fonts have been recorded and AcroFrom does not exist in
	   Catalog copy Catalog and add ref to AcroFrom
*/
	void AssignShortFontNames();
	void UnassignShortFontNames();
	void WriteFont(PDFFont* font);
	void AddFonts(Object* dict, std::list<PDFFont*>* fonts);
	void UpdateBePDFAcroForm();
	void UpdateCatalog();

	bool CopyFile(const char* name);

	bool HasRef(Object* dict, const char* key, Ref& ref);
	bool HasAnnotRef(Object* page, Ref& annotRef);
	bool HasEmbeddedContent(Object* page);
	bool CopyContentStream(Object* page);
	bool CopyPage(Object* page, Ref pageRef, Ref annotsArray);
	bool UpdatePage(int pageNo, Annotations* annots, Ref& annotsArray);

	void AddToAnnots(Object* array, Annotation* a);
	bool UpdateAnnotArray(int pageNo, Annotations* annots, Ref annotsArray);

	void AddRef(Object* dict, char* key, Ref r);
	void AddBool(Object* dict, char* key, bool b);
	void AddName(Object* dict, char* key, char* name);
	void AddString(Object* dict, char* key, GooString* string);
	void AddString(Object* dict, char* key, char* string);
	void AddInteger(Object* dict, char* key, int i);
	void AddReal(Object* dict, char* key, double r);
	void AddReal(Object* array, double r);
	void AddRect(Object* dict, char* key, PDFRectangle* rect);
	void AddColor(Object* dict, char* key, GfxRGB* color);
	void AddDict(Object* dict, char* key, Object* d); // does NOT copy d!!!
	void AddAnnotSubtype(char* type);
	void AddAnnotContents(Annotation* a);
	bool HasAppearanceStream(Annotation* a);
	void DoAnnotation(Annotation* a);
	void DoStyledAnnot(StyledAnnot* s);
	void DoMarkupAnnot(MarkupAnnot* m);
	bool WriteAS(Ref& ref, Annotation* annot);
	bool UpdateAnnot(Annotation* annot);

public:
	AnnotWriter(XRef* xref, PDFDoc* doc, AnnotsList* list, BePDFAcroForm* acroForm);
	~AnnotWriter();
	bool WriteTo(const char* name);

	// visitor functionality
	virtual void DoText(TextAnnot* a);
	virtual void DoLink(LinkAnnot* a);
	virtual void DoFreeText(FreeTextAnnot* a);
	virtual void DoLine(LineAnnot* a);
	virtual void DoSquare(SquareAnnot* a);
	virtual void DoCircle(CircleAnnot* a);
	virtual void DoHighlight(HighlightAnnot* a);
	virtual void DoUnderline(UnderlineAnnot* a);
	virtual void DoSquiggly(SquigglyAnnot* a);
	virtual void DoStrikeOut(StrikeOutAnnot* a);
	virtual void DoStamp(StampAnnot* a);
	virtual void DoInk(InkAnnot* a);
	virtual void DoPopup(PopupAnnot* a);
	virtual void DoFileAttachment(FileAttachmentAnnot* a);
	virtual void DoSound(SoundAnnot* a);
	virtual void DoMovie(MovieAnnot* a);
	virtual void DoWidget(WidgetAnnot* a);
	virtual void DoPrinterMark(PrinterMarkAnnot* a);
	virtual void DoTrapNet(TrapNetAnnot* a);
};

#endif
