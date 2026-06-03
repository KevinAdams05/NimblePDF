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

#include "TreeParser.h"

#include "TextConversion.h"

/* TreeParser */
bool TreeParser::ParseEntries(Array* entries)
{
	int len = entries->getLength();
	bool ok = true;
	for (int i = 0; ok && i < len; i++) {
		Object key = entries->get(i);
		if (!key.isNull()) {
			i++;
			if (i < len) {
				Object value = entries->get(i);
				ok = !value.isNull() && DoEntry(&key, &value);
			} else {
				// missing value
				ok = false;
			}
		}
	}
	return ok;
}

bool TreeParser::ParseKids(Array* kids)
{
	int len = kids->getLength();
	Object kid;
	Object sub;
	bool ok = true;
	for (int i = 0; ok && i < len; i++) {
		kid = kids->get(i);
		if (kid.isDict()) {
			if ((sub = kid.dictLookup("Kids")).isArray()) {
				ok = ParseKids(sub.getArray());
			}

			if (ok && (sub = kid.dictLookup(GetEntryKey())).isArray()) {
				ok = ParseEntries(sub.getArray());
			}
		}
	}
	return ok;
}

bool TreeParser::Parse(Object* tree)
{
	if (tree->isDict()) {
		Object o;
		// nodes or leafs
		if ((o = tree->dictLookup("Kids")).isArray()) {
			bool ok = ParseKids(o.getArray());
			return ok;
		} else {

			// leafs
			if ((o = tree->dictLookup(GetEntryKey())).isArray()) {
				bool ok = ParseEntries(o.getArray());
				return ok;
			}
		}
	}
	return false;
}

/* NameTreeParser */
bool NameTreeParser::DoEntry(Object* key, Object* value)
{
	if (key->isString() && key->getString() != NULL) {
		const GooString* string = key->getString();
		BString* utf8 = TextToUtf8(string->c_str(), string->getLength());
		bool ok = utf8 != NULL;
		if (ok) {
			ok = DoName(utf8->String(), value);
		}
		delete utf8;
		return ok;
	}
	// not in PDF spec. but does not hurt either
	if (key->isName()) {
		return DoName(key->getName(), value);
	}
	return false;
}

/* NumberTreeParser */
bool NumberTreeParser::DoEntry(Object* key, Object* value)
{
	if (key->isInt()) {
		return DoNumber(key->getInt(), value);
	}
	return false;
}
