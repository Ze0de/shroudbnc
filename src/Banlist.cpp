/*******************************************************************************
 * shroudBNC - an object-oriented framework for IRC                            *
 * Copyright (C) 2005 Gunnar Beutner                                           *
 *                                                                             *
 * This program is free software; you can redistribute it and/or               *
 * modify it under the terms of the GNU General Public License                 *
 * as published by the Free Software Foundation; either version 2              *
 * of the License, or (at your option) any later version.                      *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 * GNU General Public License for more details.                                *
 *                                                                             *
 * You should have received a copy of the GNU General Public License           *
 * along with this program; if not, write to the Free Software                 *
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. *
 *******************************************************************************/

#include "StdAfx.h"

/**
 * DestroyBan
 *
 * Used by CHashtable to destroy individual bans
 *
 * @param Ban the ban which is going to be destroyed
 */
void DestroyBan(ban_t *Ban) {
	free(Ban->Mask);
	free(Ban->Nick);
	free(Ban);
}

/**
 * CBanlist
 *
 * Constructs an empty banlist
 */
CBanlist::CBanlist() {
	m_Bans = new CHashtable<ban_t *, false, 5>();

	if (m_Bans)
		m_Bans->RegisterValueDestructor(DestroyBan);
}

/**
 * ~CBanlist
 *
 * Destroys a banlist
 */
CBanlist::~CBanlist() {
	delete m_Bans;
}

/**
 * SetBan
 *
 * Creates a new ban
 *
 * @param Mask the banmask
 * @param Nick the nick of the user who set the ban
 * @param TS the timestamp of the ban
 */
bool CBanlist::SetBan(const char *Mask, const char *Nick, time_t TS) {
	ban_t* Obj;

	if (m_Bans == NULL) {
		LOGERROR("could not set ban (%s, %s, %d). internal banlist is not "
			"available.", Mask, Nick, TS);

		return false;
	}

	Obj = (ban_t *)malloc(sizeof(ban_t));

	if (Obj) {
		Obj->Mask = strdup(Mask);
		Obj->Nick = strdup(Nick);
		Obj->TS = TS;

		return m_Bans->Add(Mask, Obj);
	} else
		return false;
}

/**
 * UnsetBan
 *
 * Removes a ban from the banlist.
 *
 * @param Mask the mask of the ban which is going to be removed
 */
bool CBanlist::UnsetBan(const char *Mask) {
	if (Mask != NULL && m_Bans)
		return m_Bans->Remove(Mask);
	else
		return false;
}

/**
 * Iterate
 *
 * Iterates through the banlist
 *
 * @param Skip the index of the ban which is to be returned
 */
const ban_t *CBanlist::Iterate(int Skip) {
	xhash_t<ban_t *> *Obj;

	if (m_Bans == NULL)
		return NULL;

	Obj = m_Bans->Iterate(Skip);

	if (Obj)
		return Obj->Value;
	else
		return NULL;
}

/**
 * GetBan
 *
 * Returns the ban whose banmask matches the given Mask
 *
 * @param Mask the banmask
 */
const ban_t *CBanlist::GetBan(const char *Mask) {
	if (m_Bans == NULL)
		return NULL;
	else
		return m_Bans->Get(Mask);
}