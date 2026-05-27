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

#include <locale/Catalog.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <TextControl.h>

#include "LayoutUtils.h"
#include "PasswordWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PasswordWindow"

// remember last settings in class static variable
enum PasswordWindow::PwdKind PasswordWindow::fPwdKind = USER_PASSWORD;

PasswordWindow::PasswordWindow(entry_ref* ref, BRect aRect, BLooper* looper)
    : BWindow(
          aRect, B_TRANSLATE("Enter Password"), B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fLooper = looper;
	fPasswordSent = false;
	fEntry = *ref;

	AddCommonFilter(new EscapeMessageFilter(this, B_QUIT_REQUESTED));

	// center window
	aRect.OffsetBy(aRect.Width() / 2, aRect.Height() / 2);
	float width = 300, height = 45;
	aRect.SetRightBottom(BPoint(aRect.left + width, aRect.top + height));
	aRect.OffsetBy(-aRect.Width() / 2, -aRect.Height() / 2);
	MoveTo(aRect.left, aRect.top);
	ResizeTo(width, height);

	BPopUpMenu* pwdKind = new BPopUpMenu("pwdKind");
	BMenuField* pwdKindField = new BMenuField("pwdKindField", "", pwdKind);
	BMenuItem* item;
	pwdKind->AddItem(item = new BMenuItem(B_TRANSLATE("User password"), new BMessage('user')));
	if (fPwdKind == USER_PASSWORD)
		item->SetMarked(true);
	pwdKind->AddItem(item = new BMenuItem(B_TRANSLATE("Owner password"), new BMessage('ownr')));
	if (fPwdKind == OWNER_PASSWORD)
		item->SetMarked(true);

	fPassword = new BTextControl("fPassword", "", "", NULL);
	fPassword->TextView()->HideTyping(true);

	BButton* button = new BButton("button", B_TRANSLATE("OK"), new BMessage('OK'));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL).SetInsets(B_USE_WINDOW_INSETS).Add(pwdKindField, 0).Add(fPassword, 1).Add(button, 0);

	SetDefaultButton(button);

	fPassword->MakeFocus();
	Show();
}

#include "BepdfApplication.h"

bool PasswordWindow::QuitRequested()
{
	if (!fPasswordSent) {
		gApp->OpenFilePanel();
	}
	return true;
}

void PasswordWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case 'OK': {
		// post message to application to open file with password
		const char* text = fPassword->Text();

		BMessage msg(B_REFS_RECEIVED);
		msg.AddRef("refs", &fEntry);
		msg.AddString(fPwdKind == OWNER_PASSWORD ? "ownerPassword" : "userPassword", text);
		fLooper->PostMessage(&msg, NULL);
		fPasswordSent = true;
		Quit();
		break;
	}
	case 'user':
		fPwdKind = USER_PASSWORD;
		break;
	case 'ownr':
		fPwdKind = OWNER_PASSWORD;
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}
