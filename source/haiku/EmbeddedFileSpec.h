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

#ifndef EMBEDDED_FILE_SPEC_H
#define EMBEDDED_FILE_SPEC_H

// xpdf
#include <goo/GooString.h>
#include <Object.h>
#include <XRef.h>

#include <stdio.h> // for FILE

// PDF Filespec
class EmbeddedFileSpec {
public:
	enum SaveReturnCode {
		kOk,
		kMissingEmbeddedStreamError,
		kFileOpenError,
		kFileWriteError,
	};

private:
	GooString fDescription;
	GooString fFileName;
	Ref fRef;

	bool ReadFileName(Dict* fileSpec);
	bool ReadEmbeddedFileRef(Dict* fileSpec);
	bool Save(Dict* fileSpec, const char* file);
	SaveReturnCode SaveStream(Stream* stream, FILE* file);

public:
	EmbeddedFileSpec();
	EmbeddedFileSpec(EmbeddedFileSpec* copy);
	EmbeddedFileSpec(Dict* fileSpec);
	~EmbeddedFileSpec();

	// Returns true if the EmbeddedFileSpec is valid
	bool IsValid();

	bool SetTo(Dict* fileSpec);
	// Returns description. Can be empty.
	GooString* GetDescription();
	// Returns file name.
	GooString* GetFileName();
	// Save embedded file to file
	SaveReturnCode Save(XRef* xref, const char* file);
};

#endif
