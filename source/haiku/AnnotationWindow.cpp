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

#include <stdio.h>

#include <locale/Catalog.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>

#include "AnnotationWindow.h"
#include "LayoutUtils.h"
#include "TextConversion.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AnnotationWindow"


AnnotationWindow::AnnotationWindow(GlobalSettings* settings, BLooper* looper)
    : BWindow(
          BRect(0, 0, 100, 100), B_TRANSLATE("Annotation"), B_FLOATING_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL, B_AUTO_UPDATE_SIZE_LIMITS),
      fSendNotification(true),
      fLooper(looper),
      fSettings(settings),
      fAnnotation(NULL)
{
	AddCommonFilter(new EscapeMessageFilter(this, B_QUIT_REQUESTED));

	BStringView* titleStatic = new BStringView("titleStatic", B_TRANSLATE("Title:"));
	fLabel = new BStringView("fLabel", "");

	BStringView* dateStatic = new BStringView("dateStatic", B_TRANSLATE("Date:"));
	fDate = new BStringView("fDate", "");

	BPopUpMenu* alignmentInner = new BPopUpMenu("");
	fAlignment = new BMenuField("fAlignment", B_TRANSLATE("Align:"), alignmentInner);

	BPopUpMenu* fontInner = new BPopUpMenu("");
	fFont = new BMenuField("fFont", B_TRANSLATE("Font:"), fontInner);

	BPopUpMenu* sizeInner = new BPopUpMenu("");
	fSize = new BMenuField("fSize", B_TRANSLATE("Size:"), sizeInner);

	fContents = new TextView("fContents");
	BScrollView* scroll = new BScrollView("scroll", fContents, 0, false, true);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
	    .SetInsets(B_USE_WINDOW_INSETS)
	    .AddGrid()
	    .Add(titleStatic, 0, 0)
	    .Add(fLabel, 1, 0)
	    .Add(dateStatic, 0, 1)
	    .Add(fDate, 1, 1)
	    .AddMenuField(fFont, 0, 2)
	    .AddMenuField(fSize, 0, 3)
	    .AddMenuField(fAlignment, 0, 4)
	    .End()
	    .Add(scroll);

	PopulateFontMenu(fFont->Menu());
	PopulateSizeMenu(fSize->Menu());
	PopulateAlignmentMenu(fAlignment->Menu());
	MakeEditable(false);

	MoveTo(settings->GetAnnotationWindowPosition());
	float w, h;
	settings->GetAnnotationWindowSize(w, h);
	ResizeTo(w, h);
	fContents->MakeFocus(true);

	Show();
}

void AnnotationWindow::PopulateFontMenu(BMenu* menu)
{
	BMenuItem* item;
	PDFStandardFonts* stdFonts = NimblePDFAcroForm::GetStandardFonts();
	for (int i = 0; i < stdFonts->CountFonts(); i++) {
		PDFFont* font = stdFonts->FontAt(i);
		BMessage* msg = new BMessage(FONT_SELECTED);
		msg->AddString("font", font->GetName());
		item = new BMenuItem(font->GetName(), msg);
		menu->AddItem(item);
	}
}

void AnnotationWindow::AddSizeItem(BMenu* menu, const char* label, float value)
{
	BMenuItem* item;
	BMessage* msg = new BMessage(SIZE_CHANGED);
	msg->AddFloat("size", value);
	item = new BMenuItem(label, msg);
	menu->AddItem(item);
}

void AnnotationWindow::PopulateSizeMenu(BMenu* menu)
{
	AddSizeItem(menu, B_TRANSLATE("Automatic"), 0.0);
	menu->AddSeparatorItem();
	for (float f = 8; f < 97; f += 1.0) {
		char buf[80];
		sprintf(buf, "%g", f);
		AddSizeItem(menu, buf, f);
	}
}

static char* gAlignment[] = {"left", "centered", "right"};

void AnnotationWindow::PopulateAlignmentMenu(BMenu* menu)
{
	for (uint32 i = 0; i < sizeof(gAlignment) / sizeof(char*); i++) {
		BMessage* msg = new BMessage(ALIGNMENT_CHANGED);
		msg->AddString("alignment", gAlignment[i]);
		BMenuItem* item = new BMenuItem(B_TRANSLATE(gAlignment[i]), msg);
		menu->AddItem(item);
	}
}

void AnnotationWindow::FrameMoved(BPoint point)
{
	fWindowPos = point;
	fSettings->SetAnnotationWindowPosition(point);
	BWindow::FrameMoved(point);
}

void AnnotationWindow::FrameResized(float w, float h)
{
	fSettings->SetAnnotationWindowSize(w, h);
	BWindow::FrameResized(w, h);
}


BMessage* AnnotationWindow::FindMarked(BMenu* menu)
{
	BMessage* m = NULL;
	BMenuItem* item;
	item = menu->FindMarked();
	// use first item as default
	if (item == NULL) {
		menu->ItemAt(0);
	}
	if (item) {
		m = item->Message();
	}
	return m;
}

void AnnotationWindow::WriteMessage(BMessage* msg)
{
	msg->AddPointer("annotation", fAnnotation);
	msg->AddString("contents", fContents->Text());

	const char *font, *align;
	float size;
	BMessage* m;
	m = FindMarked(fFont->Menu());
	if (m && m->FindString("font", &font) == B_OK) {
		msg->AddString("font", font);
	}
	m = FindMarked(fSize->Menu());
	if (m && m->FindFloat("size", &size) == B_OK) {
		msg->AddFloat("size", size);
	}
	m = FindMarked(fAlignment->Menu());
	if (m && m->FindString("alignment", &align) == B_OK) {
		msg->AddString("alignment", align);
	}
}

void AnnotationWindow::Notify(uint32 what)
{
	if (fSendNotification) {
		BMessage msg(what);
		WriteMessage(&msg);
		if (what == QUIT_NOTIFY || fAnnotation != NULL) {
			fLooper->PostMessage(&msg);
		}
	}
}

bool AnnotationWindow::QuitRequested()
{
	Notify(QUIT_NOTIFY);
	fSendNotification = false;
	return true;
}

void AnnotationWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case FONT_SELECTED:
	case SIZE_CHANGED:
	case ALIGNMENT_CHANGED:
	case TextView::CHANGED_NOTIFY:
		Notify(CHANGE_NOTIFY);
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}

BMenuItem* AnnotationWindow::FindItem(BMenu* menu, const char* key, const char* name)
{
	BMenuItem* item;
	BMessage* msg;
	const char* string;
	for (int32 i = 0; i < menu->CountItems(); i++) {
		item = menu->ItemAt(i);
		msg = item->Message();
		if (msg && msg->FindString(key, &string) == B_OK) {
			if (strcmp(string, name) == 0)
				return item;
		}
	}
	return menu->ItemAt(0);
}

BMenuItem* AnnotationWindow::FindFontItem(const char* name)
{
	return FindItem(fFont->Menu(), "font", name);
}

BMenuItem* AnnotationWindow::FindSizeItem(float value)
{
	BMenu* menu = fSize->Menu();
	BMenuItem* item;
	BMessage* msg;
	float size;
	for (int32 i = 0; i < menu->CountItems(); i++) {
		item = menu->ItemAt(i);
		msg = item->Message();
		if (msg && msg->FindFloat("size", &size) == B_OK) {
			if (size >= value)
				return item;
		}
	}
	return menu->ItemAt(0);
}

BMenuItem* AnnotationWindow::FindAlignmentItem(const char* name)
{
	return FindItem(fAlignment->Menu(), "alignment", name);
}

void AnnotationWindow::Update(
    Annotation* a, const char* label, const char* date, const char* contents, const char* font, float size, const char* align)
{
	fAnnotation = a;
	if (a) {
		BString title(B_TRANSLATE("Annotation"));
		title << ": ";
		AnnotName nameFinder;
		a->Visit(&nameFinder);
		title << nameFinder.GetResult();
		SetTitle(title.String());
	} else {
		SetTitle(B_TRANSLATE("Annotation"));
	}
	fLabel->SetText(label);
	fDate->SetText(date);
	fContents->SetText(contents);
	if (font != NULL) {
		BMenuItem* item;
		item = FindFontItem(font);
		item->SetMarked(true);
		item = FindSizeItem(size);
		item->SetMarked(true);
		item = FindAlignmentItem(align);
		item->SetMarked(true);
	}
	// MakeEditable() has to be called prior to Update()!
	if (fEditable) {
		bool isFreeTextAnnot = dynamic_cast<FreeTextAnnot*>(a) != NULL;
		EnableFreeTextControls(isFreeTextAnnot);
	}
}

void AnnotationWindow::EnableFreeTextControls(bool enable)
{
	fFont->Menu()->SetEnabled(enable);
	fSize->Menu()->SetEnabled(enable);
	fAlignment->Menu()->SetEnabled(enable);
}

void AnnotationWindow::MakeEditable(bool e)
{
	fEditable = e;
	fContents->MakeEditable(e);
	EnableFreeTextControls(e);
}

void AnnotationWindow::GetContents(Annotation* a, BMessage* msg)
{
	if (fAnnotation == a) {
		WriteMessage(msg);
	}
}
