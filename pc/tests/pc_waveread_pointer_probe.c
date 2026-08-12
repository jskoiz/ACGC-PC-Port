#include "jaudio_NES/bx.h"
#include "jaudio_NES/heapctrl.h"
#include "jaudio_NES/waveread.h"
#include "jaudio_NES/connect.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PC_WAVEREAD_REAL
extern u8 NIN_WAVE[];
#endif

static void write_be32(u8* address, u32 value)
{
	address[0] = (u8)(value >> 24);
	address[1] = (u8)(value >> 16);
	address[2] = (u8)(value >> 8);
	address[3] = (u8)value;
}

void Jac_InitHeap(jaheap_* heap)
{
	memset(heap, 0, sizeof(*heap));
}

void Jac_WsConnectTableSet(u32 virtual_id, u32 physical_id)
{
	(void)virtual_id;
	(void)physical_id;
}

BOOL Jac_SceneSet(WaveArchiveBank_* bank, CtrlGroup_* group, u32 id, BOOL set)
{
	assert(bank != NULL);
	assert(group != NULL);
	assert(id == 0);
	assert(set == TRUE);
	assert(bank->waveGroups[0] != NULL);
	assert(group->scenes[0] != NULL);
	return TRUE;
}

static void make_wire_graph(u8* wire)
{
	memset(wire, 0, 0x230);
	write_be32(wire + 0x00, (u32)'WSYS');
	write_be32(wire + 0x04, 0x230);
	write_be32(wire + 0x08, 0x12345678);
	write_be32(wire + 0x10, 0x100);
	write_be32(wire + 0x14, 0x220);

	memcpy(wire + 0x80, "probe.aw", 8);
	write_be32(wire + 0xF0, 1);
	write_be32(wire + 0xF4, 0x20);

	write_be32(wire + 0x100, (u32)'WINF');
	write_be32(wire + 0x104, 1);
	write_be32(wire + 0x108, 0x80);

	write_be32(wire + 0x1A0, (u32)'C-DF');
	write_be32(wire + 0x1A4, 1);
	write_be32(wire + 0x1A8, 0x120);

	write_be32(wire + 0x200, (u32)'SCNE');
	write_be32(wire + 0x20C, 0x1A0);

	write_be32(wire + 0x220, (u32)'WBCT');
	write_be32(wire + 0x224, UINT32_MAX);
	write_be32(wire + 0x228, 1);
	write_be32(wire + 0x22C, 0x200);

	write_be32(wire + 0x120, 0x00010001);
	write_be32(wire + 0x154, UINT32_MAX);
}

int main(void)
{
	u8* wire = (u8*)calloc(1, 0x230);
	CtrlGroup_* group;
	u8 raw_wsys_ptrs[8];

	assert(wire != NULL);
	assert((uintptr_t)wire > UINT32_MAX);
	make_wire_graph(wire);
	memcpy(raw_wsys_ptrs, wire + 0x10, sizeof(raw_wsys_ptrs));

	group = Wave_Test(wire);
	assert(group != NULL);
	assert(group->magic == (int)'WBCT');
	assert(group->_04 == UINT32_MAX);
	assert(group->scenes[0] != NULL);
	assert(group->scenes[0]->magic == (int)'SCNE');
	assert(group->scenes[0]->cdf != NULL);
	assert(group->scenes[0]->cdf->magic == (int)'C-DF');
	assert(group->scenes[0]->cdf->waveIDs[0]->data == (Wave_*)(uintptr_t)UINTPTR_MAX);
	assert((uintptr_t)group > UINT32_MAX);
	assert(memcmp(raw_wsys_ptrs, wire + 0x10, sizeof(raw_wsys_ptrs)) == 0);

	assert(Wavegroup_Regist(wire, 0) == TRUE);
	assert(WaveScene_Set(0, 0) == TRUE);

#ifdef PC_WAVEREAD_REAL
	assert((uintptr_t)NIN_WAVE > UINT32_MAX);
	assert(NIN_WAVE[0x10] == 0x00 && NIN_WAVE[0x11] == 0x00 &&
	       NIN_WAVE[0x12] == 0x01 && NIN_WAVE[0x13] == 0x00);
	assert(NIN_WAVE[0x14] == 0x00 && NIN_WAVE[0x15] == 0x00 &&
	       NIN_WAVE[0x16] == 0x02 && NIN_WAVE[0x17] == 0x20);
	assert(Wavegroup_Regist(NIN_WAVE, 0) == TRUE);
	assert(WaveScene_Set(0, 0) == TRUE);
#endif

	write_be32(wire + 0x10, 0);
	assert(Wave_Test(wire) == NULL);

	printf("pc_waveread_pointer_probe: PASS BE graph, native pointers, raw ownership, null/sentinel\n");
	free(wire);
	return 0;
}
