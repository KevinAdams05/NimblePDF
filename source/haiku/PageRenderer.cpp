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

#include <Message.h>

#include <Object.h>
#include <Gfx.h>
#include <PSOutputDev.h>

#include <String.h>
#include <StopWatch.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <Debug.h>

#include "Trace.h"
#include "NimblePDFApplication.h" // for images only
#include "PageRenderer.h"
#include "CachedPage.h"
#include "NimblePDF.h"
#include "Annotation.h"
#include "AnnotationRenderer.h"

#define MEASURE_RENDERING_TIME 0

inline static float RealSize(float x, float zoomDPI)
{
	return zoomDPI / 72 * x;
}

///////////////////////////////////////////////////////////////////////////

static SplashColorPtr getPaperColor()
{
	static SplashColor color;
	color[0] = 255;
	color[1] = 255;
	color[2] = 255;
	return color;
}

void PageRenderer::RedrawCallback(void* data, int left, int top, int right, int bottom, bool composited)
{
	PageRenderer* renderer = static_cast<PageRenderer*>(data);
	BView* view = renderer->fOffscreenView;
	BeSplashOutputDev* outputDev = renderer->fOutputDev;
	outputDev->redraw(left, top, view, left, top, right - left + 1, bottom - top + 1, composited);
	renderer->Notify(UPDATE_MSG);
}

bool PageRenderer::AbortCheckCallback(void* data)
{
	PageRenderer* renderer = static_cast<PageRenderer*>(data);
	return !renderer->fDoRendering;
}

bool PageRenderer::AnnotDisplayDecideCallback(Annot* annot, void* data)
{
	return false;
}

///////////////////////////////////////////////////////////////////////////
PageRenderer::PageRenderer()
    : fOwnerPassword(NULL),
      fUserPassword(NULL),
      fDoc(NULL),
      fOffscreenView(new BView(BRect(0, 0, 100, 100), "", B_FOLLOW_NONE, B_WILL_DRAW | B_SUBPIXEL_PRECISE)),
      fOutputDev(new BeSplashOutputDev(false,
          getPaperColor(),
          true, // incremental
          RedrawCallback,
          this)),
      fLooper(NULL),
      fHandler(NULL),
      fRenderingThread(-1),
      fPage(NULL),
      fBitmap(NULL),
      fNimblePDFAcroForm(NULL)
{
	fOutputDev->startDoc(NULL);
}

///////////////////////////////////////////////////////////////////////////
PageRenderer::~PageRenderer()
{
	delete fOutputDev;
	delete fOffscreenView;
	delete fOwnerPassword;
	delete fUserPassword;
}

///////////////////////////////////////////////////////////////////////////
void PageRenderer::SetDoc(PDFDoc* doc, NimblePDFAcroForm* acroForm)
{
	fDoc = doc;
	fNimblePDFAcroForm = acroForm;
	fAnnotations.SetSize(doc->getNumPages());
	fOutputDev->startDoc(doc);
}

void PageRenderer::SetPassword(BString* owner, BString* user)
{
	delete fOwnerPassword;
	fOwnerPassword = owner ? new BString(*owner) : NULL;
	delete fUserPassword;
	fUserPassword = user ? new BString(*user) : NULL;
}

///////////////////////////////////////////////////////////////////////////
void PageRenderer::StartDoc(color_space colorSpace)
{
	fColorSpace = colorSpace;
	if (fDoc != NULL) {
		// flush font engine / cache
		fOutputDev->startDoc(fDoc);
	}
}


///////////////////////////////////////////////////////////////////////////
void PageRenderer::SetListener(BLooper* looper, BHandler* handler)
{
	fLooper = looper;
	fHandler = handler;
}

///////////////////////////////////////////////////////////////////////////
void PageRenderer::SetPDFPage(int index, bool valid, float width, float height)
{
	if ((index >= 0) && (index <= 1)) {
		fPDFPage[index].valid = valid;
		fPDFPage[index].width = width;
		fPDFPage[index].height = height;
	}
}

///////////////////////////////////////////////////////////////////////////
void PageRenderer::GetSize(int pageNo, float* width, float* height, int32 zoom)
{
	// determine width and height
	*width = ceil(RealSize(fDoc->getPageCropWidth(pageNo), zoom));
	*height = ceil(RealSize(fDoc->getPageCropHeight(pageNo), zoom));

	if ((fDoc->getPageRotate(pageNo) == 90) || (fDoc->getPageRotate(pageNo) == 270)) {
		float h = *width;
		*width = *height;
		*height = h;
	}

	if ((fRotate == 90) || (fRotate == 270)) {
		float h = *width;
		*width = *height;
		*height = h;
	}
}

///////////////////////////////////////////////////////////////////////////
void PageRenderer::ResizeBitmap(float width, float height)
{
	// (re-)create bitmap
	fBitmap = fPage->GetBitmap();
	if (!fBitmap || (fColorSpace != fBitmap->ColorSpace()) || (width > fPage->GetWidth()) || (height > fPage->GetHeight())) {
		delete fBitmap;

		fBitmap = new BBitmap(BRect(0, 0, width, height), fColorSpace, true, false);
		fPage->SetBitmap(fBitmap, width, height);
	} else {
		fPage->SetBitmapSize(width, height);
	}
}

///////////////////////////////////////////////////////////////////////////
// prototype
int32 page_rendering_thread(void* data);

void PageRenderer::Start(CachedPage* page, int pageNo, int zoom, int rotation, thread_id* id, bool editAnnot)
{
	// stop thread
	if (fRenderingThread != -1) {
		Abort();
		Wait();
	}

	fZoom = zoom;
	fPage = page;
	fEditAnnot = editAnnot;
	fPageNo = pageNo;
	fZoom = zoom;
	fRotate = rotation;

	GetSize(pageNo, &fWidth, &fHeight, fZoom);

	SetPDFPage(0, true, fWidth, fHeight);
	ResizeBitmap(fWidth, fHeight);

	fPage->MakeEmpty();
	fPage->SetState(CachedPage::RENDERING);

	// start new thread
	fDoRendering = true;
	fRenderingThread = spawn_thread(page_rendering_thread, "page_rendering_thread", B_NORMAL_PRIORITY, this);
	*id = fRenderingThread;
	resume_thread(fRenderingThread);
}

///////////////////////////////////////////////////////////////////////////
void PageRenderer::Abort()
{
	fDoRendering = false;
}
///////////////////////////////////////////////////////////////////////////
void PageRenderer::Wait()
{
	if (fRenderingThread != -1) {
		status_t status;
		wait_for_thread(fRenderingThread, &status);
		fRenderingThread = -1;
	}
}

///////////////////////////////////////////////////////////////////////////
int32 page_rendering_thread(void* data)
{
	PageRenderer* pr = (PageRenderer*)data;
	pr->Render();
	return 0;
}

void PageRenderer::Draw(int page, int pageNo, float left, float top)
{
#if MEASURE_RENDERING_TIME
	BStopWatch* timer = new BStopWatch("Renderer");
#endif
	// set view to right position and size
	fOffscreenView->MoveTo(left, top);
	fOffscreenView->ResizeTo(fPDFPage[page].width, fPDFPage[page].height);
	// attach view
	fBitmap->AddChild(fOffscreenView);
	// render pdf page

	fDoc->displayPage(fOutputDev,
	    pageNo,
	    fZoom,
	    fZoom, // h/v DPI
	    fRotate,
	    false, // use media box
	    false, // crop
	    true,  // printing
	    AbortCheckCallback,
	    this); // , AnnotDisplayDecideCallback, this
	fOffscreenView->Sync();
	// detach offscreen view
	fOffscreenView->RemoveSelf();
#if MEASURE_RENDERING_TIME
	delete timer;
#endif
}


Annotations* PageRenderer::GetAnnotationsForPage(int pageNo)
{
	int i = pageNo - 1;
	if (fAnnotations.Get(i) == NULL) {
		Object annotsDict = fDoc->getCatalog()->getPage(pageNo)->getAnnotsObject();
		fAnnotations.Set(i, new Annotations(&annotsDict, fNimblePDFAcroForm));
	}
	return fAnnotations.Get(i);
}


Annotations* PageRenderer::GetAnnotations()
{
	return GetAnnotationsForPage(fPageNo);
}


void PageRenderer::DrawAnnotations(BView* view, bool edit)
{
	AnnotationRenderer ar(view, fPage->GetCTM(), fZoom, edit);
	fPage->GetAnnotations()->Iterate(&ar);
}

void PageRenderer::DrawAnnotations()
{
	if (!fEditAnnot) {
		// attach view
		fBitmap->AddChild(fOffscreenView);

		DrawAnnotations(fOffscreenView, false);
		AnnotSorter s;
		fPage->GetAnnotations()->Sort(&s);

		fOffscreenView->Sync();
		// detach offscreen view
		fOffscreenView->RemoveSelf();
	}
}

Links* PageRenderer::CreateLinks(int pageNo)
{
	Page* page = fDoc->getCatalog()->getPage(pageNo);
	// poppler 25.12: Links(Annots*); baseURI handling moved into AnnotLink.
	Links* links = new Links(page->getAnnots());
	return links;
}

void PageRenderer::Render()
{
	gPdfLock->Lock();
	fBitmap->Lock();
	fPage->SetAnnotations(GetAnnotations());
	// attach offscreen view
	fOffscreenView->MoveTo(0, 0);
	fOffscreenView->ResizeTo(fWidth, fHeight);
	fBitmap->AddChild(fOffscreenView);
	// fill page with background color
	fOffscreenView->SetHighColor(255, 255, 255);
	fOffscreenView->FillRect(BRect(0, 0, fWidth, fHeight));
	fOffscreenView->Sync();
	// detach offscreen view
	fOffscreenView->RemoveSelf();
	Draw(0, fPageNo, 0, 0);
	fPage->InitCTM(fOutputDev);
	fPage->SetLinks(CreateLinks(fPageNo));
	fPage->SetText(fOutputDev->acquireText());
	DrawAnnotations();
	fBitmap->Unlock();

	// notify listener
	uint32 what;
	if (!fDoRendering) {
		fPage->SetState(CachedPage::WAITING);
		what = ABORT_MSG;
	} else {
		fPage->SetState(CachedPage::READY);
		what = FINISH_MSG;
	}
	gPdfLock->Unlock();

	Notify(what);
}

void PageRenderer::Notify(uint32 what)
{
	BMessage msg(what);
	msg.AddInt32("nimblepdf:id", fRenderingThread);
	msg.AddPointer("nimblepdf:bitmap", fBitmap);
#ifdef DEBUG
	if (B_OK != fLooper->PostMessage(&msg))
		Trace(LOG_ERR, "Error sending message %4.4s", (char*)&msg.what);
#else
	fLooper->PostMessage(&msg);
#endif
}

void PageRenderer::GetParameter(BMessage* msg, thread_id* id, BBitmap** bitmap)
{
	if (B_OK != msg->FindInt32("nimblepdf:id", id))
		*id = -1;
	if (B_OK != msg->FindPointer("nimblepdf:bitmap", (void**)bitmap))
		*bitmap = NULL;
}
