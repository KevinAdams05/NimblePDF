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

#ifndef HISTORY_H
#define HISTORY_H

#include <be/support/List.h>
#include <be/support/String.h>
#include <be/storage/Entry.h>

class HistoryPosition;
class HistoryFile;

class HistoryEntry {
public:
	HistoryEntry() {};
	virtual ~HistoryEntry() {};
};

class HistoryPosition : public HistoryEntry {
public:
	HistoryPosition(HistoryFile* file, int page, int16 zoom, int32 left, int32 top, float rotation);
	~HistoryPosition();

	HistoryFile* GetFile() const { return fFile; }
	int GetPage() const { return fPage; }
	int16 GetZoom() const { return fZoom; }
	int32 GetLeft() const { return fLeft; }
	int32 GetTop() const { return fTop; }
	float GetRotation() const { return fRotation; }

private:
	HistoryFile* fFile;
	int fPage;
	int16 fZoom;
	int32 fLeft, fTop;
	float fRotation;
};

class HistoryFile {
public:
	HistoryFile(entry_ref ref, const char* ownerPassword, const char* userPassword);
	~HistoryFile();
	entry_ref GetRef() const { return fRef; }
	const char* GetOwnerPassword() const { return fOwnerPassword ? fOwnerPassword->String() : NULL; }
	const char* GetUserPassword() const { return fUserPassword ? fUserPassword->String() : NULL; }

	void IncreaseUseCount() { fUseCount++; }
	void DecreaseUseCount()
	{
		fUseCount--;
		if (fUseCount == 0)
			delete this;
	}
	int32 GetUseCount() const { return fUseCount; }

private:
	entry_ref fRef;
	BString* fOwnerPassword;
	BString* fUserPassword;
	int32 fUseCount;
};

class History {
protected:
	BList fList;
	int32 fCurrent;
	HistoryFile* fFile;

public:
	History();
	~History();
	void MakeEmpty();
	bool Back();
	bool Forward();
	void AddPosition(int page, int16 zoom, int32 left, int32 top, float rotation);
	void SetFile(entry_ref ref, const char* ownerPassword, const char* userPassword);
	HistoryEntry* GetTop();
	int GetElements() { return fList.CountItems(); }
	bool CanGoBack() { return fCurrent > 0 || (fCurrent == 0 && GetElements() == 1); }
	bool CanGoForward() { return fCurrent < GetElements() - 1; }

private:
	void UpdateFile();
	void Add(HistoryEntry* entry);
};

#endif
