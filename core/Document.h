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
#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <memory>
#include <string>
#include <vector>


namespace poppler {
class document;
}


namespace nimblepdf {

// A page rasterized to a packed ARGB32 buffer. Toolkit-neutral on purpose:
// each frontend wraps this in its own bitmap type (QImage, BBitmap, os::Bitmap).
struct RenderedPage {
	int width = 0;
	int height = 0;
	int stride = 0;  // bytes per row
	std::vector<unsigned char> argb;  // length == stride * height

	bool IsValid() const { return width > 0 && height > 0 && !argb.empty(); }
};


// Page dimensions in PDF points (1/72 inch).
struct PageSize {
	double width = 0.0;
	double height = 0.0;
};


// Wraps a poppler document and rasterizes pages. The std:: types here are the
// poppler-interop boundary [STYLE_GUIDE 9.3/9.5]; they do not leak into UI code.
class Document {
public:
	~Document();

	// Returns NULL if the file cannot be opened (missing, not a PDF, or
	// password-protected with a password we can't satisfy).
	static std::unique_ptr<Document> Open(const std::string& path);

	int PageCount() const { return fPageCount; }
	bool IsLocked() const { return fLocked; }

	PageSize SizeOf(int index) const;

	// Rasterize page index (0-based) at dpi. Empty RenderedPage on failure.
	RenderedPage Render(int index, double dpi) const;

private:
	Document() = default;
	Document(const Document&) = delete;
	Document& operator=(const Document&) = delete;

	poppler::document* fDocument = NULL;
	int fPageCount = 0;
	bool fLocked = false;
};

}  // namespace nimblepdf


#endif  // DOCUMENT_H
