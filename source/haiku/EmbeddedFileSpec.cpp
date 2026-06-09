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

#include "EmbeddedFileSpec.h"

#include "Annotation.h"

static char* gFileAttachmentFileNameKeys[] = {"Unix", "F", "DOS", "Mac", NULL};

EmbeddedFileSpec::EmbeddedFileSpec()
    : fRef(empty_ref)
{}

EmbeddedFileSpec::EmbeddedFileSpec(EmbeddedFileSpec* copy)
    : fDescription(copy->fDescription.toStr()),
      fFileName(copy->fFileName.toStr()),
      fRef(copy->fRef)
{}

EmbeddedFileSpec::EmbeddedFileSpec(Dict* fileSpec)
{
	SetTo(fileSpec);
}

EmbeddedFileSpec::~EmbeddedFileSpec()
{}

bool EmbeddedFileSpec::SetTo(Dict* fileSpec)
{
	fDescription.clear();
	fFileName.clear();
	fRef = empty_ref;

	if (!fileSpec->is("Filespec")) {
		return false;
	}

	// optional PDF 1.6 description for files in EmbeddedFiles name tree
	Object obj;
	if (!(obj = fileSpec->lookup("Desc")).isNull() && obj.isString()) {
		fDescription.append(obj.getString());
	}

	// mandatory file name
	if (!ReadFileName(fileSpec)) {
		fDescription.clear();
		return false;
	}


	// mandatory ref to embedded file
	if (!ReadEmbeddedFileRef(fileSpec)) {
		fDescription.clear();
		fFileName.clear();
		return false;
	}

	return true;
}

bool EmbeddedFileSpec::ReadFileName(Dict* fileSpec)
{
	for (int i = 0; gFileAttachmentFileNameKeys[i] != NULL; i++) {
		Object obj;
		char* key = gFileAttachmentFileNameKeys[i];
		if (!(obj = fileSpec->lookup(key)).isNull() && obj.isString()) {
			fFileName.append(obj.getString()->c_str());
			return true;
		}
	}
	return false;
}

bool EmbeddedFileSpec::ReadEmbeddedFileRef(Dict* fileSpec)
{
	bool found = false;
	Object obj;
	if (!(obj = fileSpec->lookup("EF")).isNull() && obj.isDict()) {
		for (int i = 0; (!found) && gFileAttachmentFileNameKeys[i] != NULL; i++) {
			const char* key = gFileAttachmentFileNameKeys[i];
			// Is there a reference to a stream?
			// We do not test here if the stream really exists!
			const Object& stream = obj.dictLookupNF(key);
			if (stream.isRef()) {
				fRef = stream.getRef();
				found = true; // leave for loop
			}
		}
	}
	return found;
}

bool EmbeddedFileSpec::IsValid()
{
	return fFileName.size() > 0 && !is_empty_ref(fRef);
}

GooString* EmbeddedFileSpec::GetDescription()
{
	return &fDescription;
}

GooString* EmbeddedFileSpec::GetFileName()
{
	return &fFileName;
}

EmbeddedFileSpec::SaveReturnCode EmbeddedFileSpec::Save(XRef* xref, const char* file)
{
	if (is_empty_ref(fRef)) {
		return kMissingEmbeddedStreamError;
	}

	Object ref = Object(Ref{fRef.num, fRef.gen});
	Object obj = ref.fetch(xref);
	if (!obj.isNull() && obj.isStream()) {
		Stream* stream = obj.getStream();

		FILE* f = fopen(file, "wb");
		if (f == NULL) {
			return kFileOpenError;
		}

		stream->reset();
		SaveReturnCode rc = SaveStream(stream, f);
		stream->close();

		fclose(f);
		return rc;
	}
	return kMissingEmbeddedStreamError;
}

EmbeddedFileSpec::SaveReturnCode EmbeddedFileSpec::SaveStream(Stream* stream, FILE* file)
{
	const int kBufferSize = 4096;
	unsigned char buffer[kBufferSize];
	while (true) {
		// fill buffer
		int length;
		int ch;
		for (length = 0; length < kBufferSize && ((ch = stream->getChar()) != EOF); length++) {
			buffer[length] = (unsigned char)ch;
		}

		// end of stream reached
		if (length == 0) {
			return kOk;
		}

		// write buffer
		if (fwrite(buffer, length, 1, file) != 1) {
			return kFileWriteError;
		}
	}
}
