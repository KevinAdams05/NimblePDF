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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <kernel/scheduler.h>

#include <locale/Catalog.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>

#include "LayoutUtils.h"
#include "TextConversion.h"
#include "TraceWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TraceWindow"

TraceWindow::TraceWindow(GlobalSettings* settings)
    : BWindow(BRect(0, 0, 100, 100), B_TRANSLATE("Error messages"), B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_AUTO_UPDATE_SIZE_LIMITS),
      fSettings(settings),
      fAutoOpen(settings->GetTraceAutoOpen()),
      fShowStdout(settings->GetTraceShowStdout()),
      fShowStderr(settings->GetTraceShowStderr())
{
	AddCommonFilter(new EscapeMessageFilter(this, HIDE_MSG));

	fOutput = new BTextView("fOutput");
	fOutput->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BScrollView* outScroll = new BScrollView("outScroll", fOutput, 0, false, true);

	BCheckBox* autoOpen = new BCheckBox("autoOpen", B_TRANSLATE("Auto open"), new BMessage(AUTO_OPEN_MSG));
	autoOpen->SetValue(fAutoOpen);

	BCheckBox* floating = new BCheckBox("floating", B_TRANSLATE("Floating"), new BMessage(FLOATING_MSG));
	floating->SetValue(fSettings->GetTraceFloating());

	fStdoutCB = new BCheckBox("fStdoutCB", "stdout", new BMessage(SHOW_STDOUT_MSG));
	fStdoutCB->SetValue(fShowStdout);

	fStderrCB = new BCheckBox("fStderrCB", "stderr", new BMessage(SHOW_STDERR_MSG));
	fStderrCB->SetValue(fShowStderr);

	BButton* clear = new BButton("clear", B_TRANSLATE("Clear"), new BMessage(CLEAR_MSG));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
	    .SetInsets(B_USE_WINDOW_INSETS)
	    .Add(outScroll)
	    .AddGroup(B_HORIZONTAL)
	    .Add(autoOpen)
	    .Add(floating)
	    .Add(fStdoutCB)
	    .Add(fStderrCB)
	    .AddGlue()
	    .Add(clear);

	fOutput->MakeEditable(false);
	fOutput->SetStylable(true);
	fStdoutCB->SetHighColor(0, 0, 255);
	fStderrCB->SetHighColor(255, 0, 0);
	EnableCheckboxes();

	MoveTo(settings->GetTraceWindowPosition());
	float w, h;
	settings->GetTraceWindowSize(w, h);
	ResizeTo(w, h);

	UpdateWindowLookAndFeel();

	Show();
	Lock();
	Hide();
	Unlock();
}

void TraceWindow::EnableCheckboxes()
{
	fStdoutCB->SetEnabled((fShowStdout && fShowStderr) || !fShowStdout);
	fStderrCB->SetEnabled((fShowStdout && fShowStderr) || !fShowStderr);
}

void TraceWindow::UpdateWindowLookAndFeel()
{
	if (fSettings->GetTraceFloating()) {
		SetLook(B_FLOATING_WINDOW_LOOK);
		SetFeel(B_FLOATING_APP_WINDOW_FEEL);
	} else {
		SetLook(B_TITLED_WINDOW_LOOK);
		SetFeel(B_NORMAL_WINDOW_FEEL);
	}
}

void TraceWindow::FrameMoved(BPoint point)
{
	fWindowPos = point;
	fSettings->SetTraceWindowPosition(point);
	BWindow::FrameMoved(point);
}

void TraceWindow::FrameResized(float w, float h)
{
	fSettings->SetTraceWindowSize(w, h);
	BWindow::FrameResized(w, h);
}

bool TraceWindow::QuitRequested()
{
	if (!IsHidden())
		Hide();
	return false;
}

void TraceWindow::WriteData(const char* name, int fd, const char* data, int len)
{
	static rgb_color stderr_color = {255, 0, 0, 255};
	static rgb_color stdout_color = {0, 0, 255, 255};

	if (len == 0)
		return;

	if (!((fd == 1 && fShowStdout) || (fd == 2 && fShowStderr)))
		return;

	if (fAutoOpen && IsHidden()) {
		Show();
	}

	//	char buffer[256];
	//	sprintf(buffer, "%s %d\n", name, fd);
	//	fOutput->Insert(fOutput->TextLength(), buffer, strlen(buffer));

	int32 cur = fOutput->TextLength() - 1;
	fOutput->Insert(cur + 1, data, len);
	int32 end = fOutput->TextLength();
	fOutput->SetFontAndColor(cur, end, NULL, 0, (fd == 1) ? &stdout_color : &stderr_color);
	fOutput->ScrollToOffset(end);
	fOutput->Invalidate();
}

void TraceWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
	case AUTO_OPEN_MSG:
		fAutoOpen = IsOn(msg);
		fSettings->SetTraceAutoOpen(fAutoOpen);
		break;
	case SHOW_STDOUT_MSG:
		fShowStdout = IsOn(msg);
		fSettings->SetTraceShowStdout(fShowStdout);
		EnableCheckboxes();
		break;
	case SHOW_STDERR_MSG:
		fShowStderr = IsOn(msg);
		fSettings->SetTraceShowStderr(fShowStderr);
		EnableCheckboxes();
		break;
	case CLEAR_MSG:
		fOutput->SelectAll();
		fOutput->Clear();
		break;
	case FLOATING_MSG:
		fSettings->SetTraceFloating(IsOn(msg));
		UpdateWindowLookAndFeel();
		break;
	case HIDE_MSG:
		if (!IsHidden())
			Hide();
		break;
	default:
		BWindow::MessageReceived(msg);
	}
}


// Implementation of OutputTracer
TraceWindow* OutputTracer::fWindow = NULL;
BLocker OutputTracer::fLock;
int OutputTracer::fTracerCount = 0;

OutputTracer::OutputTracer(int fd, const char* name, GlobalSettings* settings)
    : fDupFd(-1),
      fOutFd(fd),
      fInFd(-1),
      fName(name),
      fSettings(settings),
      fPipeThread(-1)
{
	fName = name;
	fTracerCount++;
	int fildes[2];
#ifdef DEBUG
	fInFd = -1;
	return;
#endif
	if (0 != pipe(fildes)) {
		fInFd = -1;
	} else {
		int readFd = fildes[0];
		int writeFd = fildes[1];

		fDupFd = dup(fOutFd);
		fInFd = readFd;
		dup2(writeFd, fOutFd);
		close(writeFd);

		fPipeThread = spawn_thread(start_thread, name, suggest_thread_priority(B_USER_INPUT_HANDLING), this);
		resume_thread(fPipeThread);
	}
}

OutputTracer::~OutputTracer()
{
	status_t status;
	close(fInFd);
	dup2(fDupFd, fOutFd);
	close(fDupFd);
	wait_for_thread(fPipeThread, &status);
	fLock.Lock();
	fTracerCount--;
	if (fTracerCount == 0 && fWindow) {
		fWindow->Lock();
		fWindow->Quit();
		fWindow = NULL;
	}
	fLock.Unlock();
}

int32 OutputTracer::start_thread(void* data)
{
	((OutputTracer*)data)->Run();
	return 0;
}

TraceWindow* OutputTracer::CreateWindow(GlobalSettings* s)
{
	if (fWindow == NULL) {
		fLock.Lock();
		if (fWindow == NULL) {
			fWindow = new TraceWindow(s);
		}
		fLock.Unlock();
	}
	return fWindow;
}

void OutputTracer::WriteData(const char* data, int len)
{
	fLock.Lock();
	fWindow = CreateWindow(fSettings);
	if (fWindow->Lock()) {
		fWindow->WriteData(fName.String(), fOutFd, data, len);
		fWindow->Unlock();
	}
	fLock.Unlock();
}

void OutputTracer::Run()
{
	const int max = 256;
	char buffer[max];
	int len;

	while (0 < (len = read(fInFd, buffer, max))) {
		WriteData(buffer, len);
	}
}

void OutputTracer::ShowWindow(GlobalSettings* s)
{
	BWindow* w = CreateWindow(s);
	w->Lock();
	if (w->IsHidden())
		w->Show();
	w->Activate();
	w->Unlock();
}
