#include "jaudio_NES/waveread.h"

#include "jaudio_NES/connect.h"
#include "jaudio_NES/heapctrl.h"
#include "jaudio_NES/bx.h"

#ifdef TARGET_PC
#include "pc_bswap.h"
#include <stdlib.h>
#endif

#define WAVEARC_SIZE   (0x100)
#define WAVEGROUP_SIZE (0x100)

static WaveArchiveBank_* wavearc[WAVEARC_SIZE];
static CtrlGroup_* wavegroup[WAVEGROUP_SIZE];
CtrlGroup_* CGRP_ARRAY[16];

#ifdef TARGET_PC
/* Serialized WSYS records remain GameCube wire data on the host. */
typedef struct {
	const u8* base;
	size_t size;
} PCWaveContext;

static WaveArchiveBank_* pc_wave_last_arc_bank;

static u16 pc_wave_read_u16(const u8* address)
{
	u16 value;
	memcpy(&value, address, sizeof(value));
	return pc_bswap16(value);
}

static u32 pc_wave_read_u32(const u8* address)
{
	u32 value;
	memcpy(&value, address, sizeof(value));
	return pc_bswap32(value);
}

static f32 pc_wave_read_f32(const u8* address)
{
	u32 bits = pc_wave_read_u32(address);
	f32 value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static const u8* pc_wave_wire_at(const PCWaveContext* context, u32 offset, size_t size)
{
	if (context == NULL || context->base == NULL || offset == 0 || offset == UINT32_MAX ||
	    (size_t)offset > context->size || size > context->size - (size_t)offset) {
		return NULL;
	}
	return context->base + offset;
}

static void* pc_wave_host_alloc(size_t size)
{
	return calloc(1, size);
}

static BOOL pc_wave_array_size(size_t prefix, u32 count, size_t element_size, size_t* size)
{
	if (count > (SIZE_MAX - prefix) / element_size) {
		return FALSE;
	}
	*size = prefix + (size_t)count * element_size;
	return TRUE;
}

static Wave_* pc_wave_copy_wave(const PCWaveContext*, u32);
static WaveID_* pc_wave_copy_wave_id(const PCWaveContext*, u32);
static Ctrl_* pc_wave_copy_ctrl(const PCWaveContext*, u32);
static SCNE_* pc_wave_copy_scene(const PCWaveContext*, u32);
static WaveArchive_* pc_wave_copy_archive(const PCWaveContext*, u32);

static Wave_* pc_wave_copy_wave(const PCWaveContext* context, u32 offset)
{
	const u8* wire = pc_wave_wire_at(context, offset, 0x28);
	Wave_* wave;
	u32 status_offset;

	if (wire == NULL) {
		return NULL;
	}
	wave = (Wave_*)pc_wave_host_alloc(sizeof(*wave));
	if (wave == NULL) {
		return NULL;
	}
	wave->_00 = wire[0x00];
	wave->compBlockIdx = wire[0x01];
	wave->key = wire[0x02];
	wave->_04 = pc_wave_read_f32(wire + 0x04);
	wave->srcAddress = (int)pc_wave_read_u32(wire + 0x08);
	wave->length = (int)pc_wave_read_u32(wire + 0x0C);
	wave->isLooping = (s32)pc_wave_read_u32(wire + 0x10);
	wave->loopAddress = (s32)pc_wave_read_u32(wire + 0x14);
	wave->loopStartPosition = (s32)pc_wave_read_u32(wire + 0x18);
	wave->_1C = (s32)pc_wave_read_u32(wire + 0x1C);
	wave->loopYN1 = (s16)pc_wave_read_u16(wire + 0x20);
	wave->loopYN2 = (s16)pc_wave_read_u16(wire + 0x22);
	status_offset = pc_wave_read_u32(wire + 0x24);
	if (status_offset == UINT32_MAX) {
		wave->fileLoadStatus = (u32*)(uintptr_t)UINTPTR_MAX;
	} else if (status_offset != 0) {
		const u8* status_wire = pc_wave_wire_at(context, status_offset, sizeof(u32));
		u32* status = NULL;
		if (status_wire != NULL) {
			status = (u32*)pc_wave_host_alloc(sizeof(*status));
			if (status != NULL) {
				*status = pc_wave_read_u32(status_wire);
			}
		}
		wave->fileLoadStatus = status;
	} else {
		wave->fileLoadStatus = NULL;
	}
	return wave;
}

static WaveID_* pc_wave_copy_wave_id(const PCWaveContext* context, u32 offset)
{
	const u8* wire = pc_wave_wire_at(context, offset, 0x38);
	WaveID_* wave;
	u32 data_offset;

	if (wire == NULL) {
		return NULL;
	}
	wave = (WaveID_*)pc_wave_host_alloc(sizeof(*wave));
	if (wave == NULL) {
		return NULL;
	}
	wave->id = pc_wave_read_u32(wire + 0x00);
	wave->heap.memoryType = wire[0x05];
	wave->loadStatus = pc_wave_read_u32(wire + 0x30);
	data_offset = pc_wave_read_u32(wire + 0x34);
	if (data_offset == UINT32_MAX) {
		wave->data = (Wave_*)(uintptr_t)UINTPTR_MAX;
	} else {
		wave->data = pc_wave_copy_wave(context, data_offset);
	}
	return wave;
}

static Ctrl_* pc_wave_copy_ctrl(const PCWaveContext* context, u32 offset)
{
	const u8* wire;
	Ctrl_* ctrl;
	u32 count;
	size_t wire_size;
	size_t host_size;
	u32 i;

	wire = pc_wave_wire_at(context, offset, 0x08);
	if (wire == NULL) {
		return NULL;
	}
	count = pc_wave_read_u32(wire + 0x04);
	if (count > WAVEGROUP_SIZE) {
		return NULL;
	}
	if (!pc_wave_array_size(0x08, count, sizeof(u32), &wire_size) ||
	    pc_wave_wire_at(context, offset, wire_size) == NULL ||
	    !pc_wave_array_size(sizeof(*ctrl) - sizeof(ctrl->waveIDs[0]), count,
	                        sizeof(ctrl->waveIDs[0]), &host_size)) {
		return NULL;
	}
	ctrl = (Ctrl_*)pc_wave_host_alloc(host_size);
	if (ctrl == NULL) {
		return NULL;
	}
	ctrl->magic = (int)pc_wave_read_u32(wire + 0x00);
	ctrl->count = (int)count;
	for (i = 0; i < count; i++) {
		ctrl->waveIDs[i] = pc_wave_copy_wave_id(context, pc_wave_read_u32(wire + 0x08 + i * 4));
	}
	return ctrl;
}

static SCNE_* pc_wave_copy_scene(const PCWaveContext* context, u32 offset)
{
	const u8* wire;
	SCNE_* scene;
	u32 child_count;
	size_t wire_size;
	size_t host_size;
	u32 i;

	wire = pc_wave_wire_at(context, offset, 0x18);
	if (wire == NULL) {
		return NULL;
	}
	child_count = pc_wave_read_u32(wire + 0x08);
	if (child_count > WAVEGROUP_SIZE) {
		return NULL;
	}
	if (!pc_wave_array_size(0x18, child_count, sizeof(u32), &wire_size) ||
	    pc_wave_wire_at(context, offset, wire_size) == NULL ||
	    !pc_wave_array_size(sizeof(*scene) - sizeof(scene->_18[0]), child_count,
	                        sizeof(scene->_18[0]), &host_size)) {
		return NULL;
	}
	scene = (SCNE_*)pc_wave_host_alloc(host_size);
	if (scene == NULL) {
		return NULL;
	}
	scene->magic = (int)pc_wave_read_u32(wire + 0x00);
	scene->_04 = pc_wave_read_u32(wire + 0x04);
	scene->_08 = child_count;
	scene->cdf = pc_wave_copy_ctrl(context, pc_wave_read_u32(wire + 0x0C));
	scene->cex = pc_wave_copy_ctrl(context, pc_wave_read_u32(wire + 0x10));
	scene->cst = pc_wave_copy_ctrl(context, pc_wave_read_u32(wire + 0x14));
	for (i = 0; i < child_count; i++) {
		scene->_18[i] = (int)pc_wave_read_u32(wire + 0x18 + i * 4);
	}
	return scene;
}

static WaveArchive_* pc_wave_copy_archive(const PCWaveContext* context, u32 offset)
{
	const u8* wire;
	WaveArchive_* archive;
	u32 wave_count;
	size_t wire_size;
	size_t host_size;
	u32 i;

	wire = pc_wave_wire_at(context, offset, 0x74);
	if (wire == NULL) {
		return NULL;
	}
	wave_count = pc_wave_read_u32(wire + 0x70);
	if (wave_count > WAVEGROUP_SIZE) {
		return NULL;
	}
	if (!pc_wave_array_size(0x74, wave_count, sizeof(u32), &wire_size) ||
	    pc_wave_wire_at(context, offset, wire_size) == NULL ||
	    !pc_wave_array_size(sizeof(*archive) - sizeof(archive->waves[0]), wave_count,
                        sizeof(archive->waves[0]), &host_size)) {
		return NULL;
	}
	archive = (WaveArchive_*)pc_wave_host_alloc(host_size);
	if (archive == NULL) {
		return NULL;
	}
	memcpy(archive->filePath, wire, sizeof(archive->filePath));
	archive->heap.memoryType = wire[0x41];
	archive->fileLoadStatus = pc_wave_read_u32(wire + 0x6C);
	archive->waveCount = (int)wave_count;
	for (i = 0; i < wave_count; i++) {
		archive->waves[i] = pc_wave_copy_wave(context, pc_wave_read_u32(wire + 0x74 + i * 4));
	}
	return archive;
}

static WaveArchiveBank_* pc_wave_copy_arc_bank(const PCWaveContext* context, u32 offset)
{
	const u8* wire;
	WaveArchiveBank_* bank;
	u32 count;
	size_t wire_size;
	size_t host_size;
	u32 i;

	wire = pc_wave_wire_at(context, offset, 0x08);
	if (wire == NULL) {
		return NULL;
	}
	count = pc_wave_read_u32(wire + 0x04);
	if (count > WAVEARC_SIZE) {
		return NULL;
	}
	if (!pc_wave_array_size(0x08, count, sizeof(u32), &wire_size) ||
	    pc_wave_wire_at(context, offset, wire_size) == NULL ||
	    !pc_wave_array_size(sizeof(*bank) - sizeof(bank->waveGroups[0]), count,
	                        sizeof(bank->waveGroups[0]), &host_size)) {
		return NULL;
	}
	bank = (WaveArchiveBank_*)pc_wave_host_alloc(host_size);
	if (bank == NULL) {
		return NULL;
	}
	bank->magic = (int)pc_wave_read_u32(wire + 0x00);
	bank->count = (int)count;
	for (i = 0; i < count; i++) {
		bank->waveGroups[i] = pc_wave_copy_archive(context, pc_wave_read_u32(wire + 0x08 + i * 4));
	}
	return bank;
}

static CtrlGroup_* pc_wave_copy_group(const PCWaveContext* context, u32 offset)
{
	const u8* wire;
	CtrlGroup_* group;
	u32 count;
	size_t wire_size;
	size_t host_size;
	u32 i;

	wire = pc_wave_wire_at(context, offset, 0x0C);
	if (wire == NULL) {
		return NULL;
	}
	count = pc_wave_read_u32(wire + 0x08);
	if (count > WAVEGROUP_SIZE) {
		return NULL;
	}
	if (!pc_wave_array_size(0x0C, count, sizeof(u32), &wire_size) ||
	    pc_wave_wire_at(context, offset, wire_size) == NULL ||
	    !pc_wave_array_size(sizeof(*group) - sizeof(group->scenes[0]), count,
	                        sizeof(group->scenes[0]), &host_size)) {
		return NULL;
	}
	group = (CtrlGroup_*)pc_wave_host_alloc(host_size);
	if (group == NULL) {
		return NULL;
	}
	group->magic = (int)pc_wave_read_u32(wire + 0x00);
	group->_04 = pc_wave_read_u32(wire + 0x04);
	group->count = (int)count;
	for (i = 0; i < count; i++) {
		group->scenes[i] = pc_wave_copy_scene(context, pc_wave_read_u32(wire + 0x0C + i * 4));
	}
	return group;
}
#endif

#ifndef TARGET_PC
/*
 * --INFO--
 * Address:	8000C200
 * Size:	000038
 */
static void PTconvert(void** pointer, u32 base_address)
{
	if (*pointer == NULL) {
		*pointer = NULL;
		return;
	}
	if (*pointer >= (void*)base_address || *pointer == NULL) {
		return;
	}
	*pointer = *(char**)pointer + base_address;
}
#endif

/*
 * --INFO--
 * Address:	8000C240
 * Size:	0002A0
 */
CtrlGroup_* Wave_Test(u8* data)
{
#ifdef TARGET_PC
	PCWaveContext context;
	WaveArchiveBank_* arcBank;
	CtrlGroup_* group;
	u32 i;
	u32 j;

	pc_wave_last_arc_bank = NULL;
	if (data == NULL) {
		return NULL;
	}
	context.base = data;
	context.size = pc_wave_read_u32(data + 0x04);
	if (context.size < 0x18 || pc_wave_read_u32(data + 0x00) != 'WSYS') {
		return NULL;
	}
	arcBank = pc_wave_copy_arc_bank(&context, pc_wave_read_u32(data + 0x10));
	group = pc_wave_copy_group(&context, pc_wave_read_u32(data + 0x14));
	CGRP_ARRAY[0] = group;

	if (arcBank == NULL || group == NULL || arcBank->magic != 'WINF') {
		return NULL;
	}
	if (group->magic != 'WBCT') {
		return NULL;
	}

	for (i = 0; i < (u32)arcBank->count; i++) {
		WaveArchive_* arc = arcBank->waveGroups[i];
		if (arc == NULL) {
			return NULL;
		}
		Jac_InitHeap(&arc->heap);
		arc->heap.startAddress = 0;
		for (j = 0; j < (u32)arc->waveCount; j++) {
			if (arc->waves[j] == NULL) {
				return NULL;
			}
		}
	}

	for (i = 0; i < (u32)group->count; i++) {
		SCNE_* scene = group->scenes[i];
		if (scene == NULL) {
			return NULL;
		}
		if (scene->cdf != NULL && scene->cdf->magic == 'C-DF') {
			for (j = 0; j < (u32)scene->cdf->count; j++) {
				if (scene->cdf->waveIDs[j] == NULL) {
					return NULL;
				}
				Jac_InitHeap(&scene->cdf->waveIDs[j]->heap);
				scene->cdf->waveIDs[j]->heap.startAddress = 0;
			}
		}
		if (scene->cex != NULL && scene->cex->magic == 'C-EX') {
			for (j = 0; j < (u32)scene->cex->count; j++) {
				if (scene->cex->waveIDs[j] == NULL) {
					return NULL;
				}
				Jac_InitHeap(&scene->cex->waveIDs[j]->heap);
				scene->cex->waveIDs[j]->heap.startAddress = 0;
			}
		}
		if (scene->cst != NULL && scene->cst->magic == 'C-ST') {
			for (j = 0; j < (u32)scene->cst->count; j++) {
				if (scene->cst->waveIDs[j] == NULL) {
					return NULL;
				}
				Jac_InitHeap(&scene->cst->waveIDs[j]->heap);
				scene->cst->waveIDs[j]->heap.startAddress = 0;
			}
		}
	}
	pc_wave_last_arc_bank = arcBank;
	return group;
#else
    u32 base_addr = (u32)data;
	CtrlGroup_* group;
	SCNE_* scene;
	Ctrl_* cst;
	Ctrl_* cdf;
	Ctrl_* cex;
	u32 i;
    u32 j;
	WaveArchiveBank_* arcBank;
	WaveArchive_* arc;

	PTconvert((void**)&((Wsys_*)data)->waveArcBank, base_addr);
	PTconvert((void**)&((Wsys_*)data)->ctrlGroup, base_addr);
	arcBank       = *(WaveArchiveBank_**)(data + 0x10);
	group         = *(CtrlGroup_**)(data + 0x14);
	CGRP_ARRAY[0] = group;

	if (arcBank->magic != 'WINF') {
		return NULL;
	}
	if (group->magic != 'WBCT') {
		return NULL;
	}

	for (i = 0; i < arcBank->count; i++) {
		PTconvert((void**)&arcBank->waveGroups[i], base_addr);
		arc     = arcBank->waveGroups[i];
		Jac_InitHeap(&arc->heap);
		arc->heap.startAddress = 0;

		for (j = 0; j < arc->waveCount; j++) {
			PTconvert((void**)&arc->waves[j], base_addr);
		}
	}

	for (i = 0; i < group->count; i++) {
		PTconvert((void**)&group->scenes[i], base_addr);
		scene = group->scenes[i];
		PTconvert((void**)&scene->cdf, base_addr);
		PTconvert((void**)&scene->cex, base_addr);
		PTconvert((void**)&scene->cst, base_addr);

		cdf = scene->cdf;
		if (cdf && cdf->magic == 'C-DF') {
			for (j = 0; j < cdf->count; j++) {
				PTconvert((void**)&cdf->waveIDs[j], base_addr);
				Jac_InitHeap(&cdf->waveIDs[j]->heap);
				cdf->waveIDs[j]->heap.startAddress = 0;
			}
		}

		cex = scene->cex;
		if (cex && cex->magic == 'C-EX') {
			for (j = 0; j < cex->count; j++) {
				PTconvert((void**)&cex->waveIDs[j], base_addr);
				Jac_InitHeap(&cex->waveIDs[j]->heap);
				cex->waveIDs[j]->heap.startAddress = 0;
			}
		}

		cst = scene->cst;
		if (cst && cst->magic == 'C-ST') {
			for (j = 0; j < cst->count; j++) {
				PTconvert((void**)&cst->waveIDs[j], base_addr);
				Jac_InitHeap(&cst->waveIDs[j]->heap);
				cst->waveIDs[j]->heap.startAddress = 0;
			}
		}
	}
	return CGRP_ARRAY[0];
#endif
}

/*
 * --INFO--
 * Address:	........
 * Size:	000030
 */
void GetSound_Test(u32 id)
{
	// UNUSED FUNCTION
}

/*
 * --INFO--
 * Address:	8000C4E0
 * Size:	000084
 */
BOOL Wavegroup_Regist(void* wsysData, u32 id)
{
#ifdef TARGET_PC
	const u8* data = (const u8*)wsysData;

	if (data == NULL) {
		return FALSE;
	}
	if (pc_wave_read_u32(data + 0x04) < 0x18) {
		return FALSE;
	}
	Jac_WsConnectTableSet(pc_wave_read_u32(data + 0x08), id);
	wavegroup[id] = Wave_Test((u8*)data);
	wavearc[id]   = pc_wave_last_arc_bank;
#else
	Wsys_* wsys = (Wsys_*)wsysData;
	Jac_WsConnectTableSet(wsys->globalID, id);
	wavegroup[id] = Wave_Test((u8*)wsys);
	wavearc[id]   = wsys->waveArcBank;
#endif

	if (wavegroup[id] == NULL) {
		return FALSE;
	}
	wavegroup[id]->_04 = 0;
	return TRUE;
}

/*
 * --INFO--
 * Address:	8000C580
 * Size:	00002C
 */
void Wavegroup_Init()
{
	for (int i = 0; i < WAVEGROUP_SIZE; ++i) {
		wavegroup[i] = NULL;
	}
}

/*
 * --INFO--
 * Address:	8000C5C0
 * Size:	000064
 */
CtrlGroup_* WaveidToWavegroup(u32 param_1, u32 param_2)
{
	u16 virtID = param_1 >> 16;
	u16 index;
	u16* REF_virtID = &virtID;

	if (virtID == 0xFFFF) {
		index = param_2;
	} else {
		index = Jac_WsVirtualToPhysical(virtID);
	}

	return index >= WAVEGROUP_SIZE ? NULL : wavegroup[index];
}

/*
 * --INFO--
 * Address:	8000C640
 * Size:	00008C
 */
static BOOL __WaveScene_Set(u32 waveIndex, u32 ctrlIndex, BOOL doSet)
{
	u32* REF_param_1;
	u32* REF_param_2;

	CtrlGroup_* group;

	REF_param_1 = &waveIndex;
	if (waveIndex >= WAVEGROUP_SIZE) {
		return FALSE;
	}
	if (!(group = wavegroup[waveIndex])) {
		return FALSE;
	}
	REF_param_2 = &ctrlIndex;
	if (ctrlIndex >= group->count) {
		return FALSE;
	}
	return Jac_SceneSet(wavearc[waveIndex], group, ctrlIndex, doSet);
}

/*
 * --INFO--
 * Address:	8000C6E0
 * Size:	000024
 */
BOOL WaveScene_Set(u32 waveIndex, u32 ctrlIndex)
{
	return __WaveScene_Set(waveIndex, ctrlIndex, TRUE);
}

/*
 * --INFO--
 * Address:	8000C720
 * Size:	000024
 */
BOOL WaveScene_Load(u32 waveIndex, u32 ctrlIndex)
{
	return __WaveScene_Set(waveIndex, ctrlIndex, FALSE);
}

/*
 * --INFO--
 * Address:	8000C760
 * Size:	000074
 */
static void __WaveScene_Close(u32 waveIndex, u32 ctrlIndex, BOOL param_3)
{
	u32* REF_param_1;
	u32* REF_param_2;

	CtrlGroup_* group;

	REF_param_1 = &waveIndex;
	if (waveIndex >= WAVEGROUP_SIZE) {
		return;
	}
	if (group = wavegroup[waveIndex]) {
		REF_param_2 = &ctrlIndex;
		if (ctrlIndex < group->count) {
			Jac_SceneClose(wavearc[waveIndex], group, ctrlIndex, param_3);
		}
	}
}

/*
 * --INFO--
 * Address:	8000C7E0
 * Size:	000024
 */
void WaveScene_Close(u32 waveIndex, u32 ctrlIndex)
{
	__WaveScene_Close(waveIndex, ctrlIndex, TRUE);
}

/*
 * --INFO--
 * Address:	8000C820
 * Size:	000024
 */
void WaveScene_Erase(u32 waveIndex, u32 ctrlIndex)
{
	__WaveScene_Close(waveIndex, ctrlIndex, FALSE);
}
