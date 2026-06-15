/*
 * NimblePDF: A native PDF reader.
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include "Document.h"

#include <poppler-document.h>
#include <poppler-image.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>


namespace nimblepdf {


Document::~Document()
{
	delete fDocument;
}


std::unique_ptr<Document>
Document::Open(const std::string& path)
{
	poppler::document* document = poppler::document::load_from_file(path);
	if (document == NULL)
		return NULL;

	// Retry encrypted documents with an empty password (the common case for
	// permission-only encryption). A real password prompt comes later.
	if (document->is_locked()) {
		delete document;
		document = poppler::document::load_from_file(path, "", "");
		if (document == NULL)
			return NULL;
	}

	std::unique_ptr<Document> result(new Document());
	result->fDocument = document;
	result->fPageCount = document->pages();
	result->fLocked = document->is_locked();
	return result;
}


PageSize
Document::SizeOf(int index) const
{
	PageSize size;
	if (index < 0 || index >= fPageCount)
		return size;

	std::unique_ptr<poppler::page> page(fDocument->create_page(index));
	if (!page)
		return size;

	poppler::rectf rectangle = page->page_rect(poppler::media_box);
	size.width = rectangle.width();
	size.height = rectangle.height();
	return size;
}


RenderedPage
Document::Render(int index, double dpi) const
{
	RenderedPage out;
	if (index < 0 || index >= fPageCount)
		return out;

	std::unique_ptr<poppler::page> page(fDocument->create_page(index));
	if (!page)
		return out;

	poppler::page_renderer renderer;
	renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
	renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
	renderer.set_image_format(poppler::image::format_argb32);

	poppler::image image = renderer.render_page(page.get(), dpi, dpi);
	if (!image.is_valid())
		return out;

	out.width = image.width();
	out.height = image.height();
	out.stride = image.bytes_per_row();

	const char* data = image.const_data();
	out.argb.assign(data, data + static_cast<size_t>(out.stride) * out.height);
	return out;
}


}  // namespace nimblepdf
