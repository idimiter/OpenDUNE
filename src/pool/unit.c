/* $Id$ */

/** @file src/pool/unit.c %Unit pool routines. */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "libemu.h"
#include "../global.h"
#include "../house.h"
#include "../script/script.h"
#include "../unit.h"
#include "pool.h"
#include "house.h"
#include "unit.h"

/**
 * Get a Unit from the pool with the indicated index.
 *
 * @param index The index of the Unit to get.
 * @return The Unit.
 */
Unit *Unit_Get_ByIndex(uint16 index)
{
	assert(index < UNIT_INDEX_MAX);
	return (Unit *)&emu_get_memory8(g_global->unitStartPos.s.cs, g_global->unitStartPos.s.ip, index * sizeof(Unit));
}

/**
 * Get a Unit from the pool at the indicated address.
 *
 * @param address The address of the Unit to get.
 * @return The Unit.
 */
Unit *Unit_Get_ByMemory(csip32 address)
{
	assert(g_global->unitStartPos.csip <= address.csip && address.csip < g_global->unitStartPos.csip + sizeof(Unit) * UNIT_INDEX_MAX);
	return (Unit *)&emu_get_memory8(address.s.cs, address.s.ip, 0x0);
}

/**
 * Find the first matching Unit based on the PoolFindStruct filter data.
 *
 * @param find A pointer to a PoolFindStruct which contains filter data and
 *   last known tried index. Calling this functions multiple times with the
 *   same 'find' parameter walks over all possible values matching the filter.
 * @return The Unit, or NULL if nothing matches (anymore).
 */
Unit *Unit_Find(PoolFindStruct *find)
{
	if (find->index >= g_global->unitCount && find->index != 0xFFFF) return NULL;
	find->index++; /* First, we always go to the next index */

	for (; find->index < g_global->unitCount; find->index++) {
		csip32 pos = g_global->unitArray[find->index];
		Unit *u;
		if (pos.csip == 0x0) continue;

		u = Unit_Get_ByMemory(pos);

		if (u->o.flags.s.isNotOnMap && g_global->variable_38BC == 0) continue;
		if (find->houseID != HOUSE_INDEX_INVALID && find->houseID != Unit_GetHouseID(u)) continue;
		if (find->type    != UNIT_INDEX_INVALID  && find->type    != u->o.type)  continue;

		return u;
	}

	return NULL;
}

/**
 * Initialize the Unit array.
 *
 * @param address If non-zero, the new location of the Unit array.
 */
void Unit_Init(csip32 address)
{
	g_global->unitCount = 0;

	if (address.csip != 0x0) {
		/* Try to make the IP empty by moving as much as possible to the CS */
		g_global->unitStartPos.s.cs = address.s.cs + (address.s.ip >> 4);
		g_global->unitStartPos.s.ip = address.s.ip & 0x000F;
	}

	if (g_global->unitStartPos.csip == 0x0) return;

	memset(Unit_Get_ByIndex(0), 0, sizeof(Unit) * UNIT_INDEX_MAX);
}

/**
 * Recount all Units, ignoring the cache array. Also set the unitCount
 *  of all houses to zero.
 */
void Unit_Recount()
{
	uint16 index;
	PoolFindStruct find = { -1, -1, -1 };
	House *h = House_Find(&find);

	while (h != NULL) {
		h->unitCount = 0;
		h = House_Find(&find);
	}

	g_global->unitCount = 0;

	for (index = 0; index < UNIT_INDEX_MAX; index++) {
		Unit *u = Unit_Get_ByIndex(index);
		if (!u->o.flags.s.used) continue;

		h = House_Get_ByIndex(u->o.houseID);
		h->unitCount++;

		g_global->unitArray[g_global->unitCount] = g_global->unitStartPos;
		g_global->unitArray[g_global->unitCount].s.ip += index * sizeof(Unit);
		g_global->unitCount++;
	}
}

/**
 * Allocate a Unit.
 *
 * @param index The index to use, or UNIT_INDEX_INVALID to find an unused index.
 * @param typeID The type of the new Unit.
 * @param houseID The House of the new Unit.
 * @return The Unit allocated, or NULL on failure.
 */
Unit *Unit_Allocate(uint16 index, uint8 type, uint8 houseID)
{
	House *h;
	Unit *u = NULL;

	if (type == 0xFF || houseID == 0xFF) return NULL;
	if (g_global->unitStartPos.csip == 0x0) return NULL;

	h = House_Get_ByIndex(houseID);
	if (h->unitCount >= h->unitCountMax) {
		if (g_unitInfo[type].movementType != MOVEMENT_WINGER && g_unitInfo[type].movementType != MOVEMENT_SLITHER) {
			if (g_global->variable_38BC == 0x00) return NULL;
		}
	}

	if (index == 0 || index == UNIT_INDEX_INVALID) {
		uint16 indexStart = g_unitInfo[type].indexStart;
		uint16 indexEnd   = g_unitInfo[type].indexEnd;

		for (index = indexStart; index <= indexEnd; index++) {
			u = Unit_Get_ByIndex(index);
			if (!u->o.flags.s.used) break;
		}
		if (index > indexEnd) return NULL;
	} else {
		u = Unit_Get_ByIndex(index);
		if (u->o.flags.s.used) return NULL;
	}
	assert(u != NULL);

	h->unitCount++;

	/* Initialize the Unit */
	memset(u, 0, sizeof(Unit));
	u->o.index                   = index;
	u->o.type                    = type;
	u->o.houseID                 = houseID;
	u->o.linkedID                = 0xFF;
	u->o.flags.s.used            = true;
	u->o.flags.s.allocated       = true;
	u->o.flags.s.variable_6_0001 = true;
	u->o.script.delay      = 0;
	u->variable_72[0]            = 0xFF;
	if (type == UNIT_SANDWORM) u->amount = 3;

	g_global->unitArray[g_global->unitCount] = g_global->unitStartPos;
	g_global->unitArray[g_global->unitCount].s.ip += index * sizeof(Unit);
	g_global->unitCount++;

	return u;
}

/**
 * Free a Unit.
 *
 * @param address The address of the Unit to free.
 */
void Unit_Free(Unit *u)
{
	csip32 ucsip;
	int i;

	/* XXX -- Temporary, to keep all the emu_calls workable for now */
	ucsip = g_global->unitStartPos;
	ucsip.s.ip += u->o.index * sizeof(Unit);

	u->o.flags.all = 0x0000;

	Script_Reset(&u->o.script, &g_global->scriptUnit);

	/* Walk the array to find the Unit we are removing */
	for (i = 0; i < g_global->unitCount; i++) {
		if (g_global->unitArray[i].csip != ucsip.csip) continue;
		break;
	}
	assert(i < g_global->unitCount); /* We should always find an entry */

	g_global->unitCount--;

	{
		House *h = House_Get_ByIndex(u->o.houseID);
		h->unitCount--;
	}

	/* If needed, close the gap */
	if (i == g_global->unitCount) return;
	memmove(&g_global->unitArray[i], &g_global->unitArray[i + 1], (g_global->unitCount - i) * sizeof(g_global->unitArray[0]));
}
