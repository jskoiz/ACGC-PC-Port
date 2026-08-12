#include "TwoHeadArena.h"

#ifdef TARGET_PC
#include "acgc/address.h"
#endif

#include "libultra/libultra.h"
#include "types.h"

/* @fabricated */
/*
extern void* THA_getHeadPtr(TwoHeadArena* this) {
  return this->head_p;
}
*/

/* @fabricated */
/*
extern void THA_setHeadPtr(TwoHeadArena* this, void* p) {
  this->head_p = (char*)p;
}
*/

/* @fabricated */
/*
extern void* THA_getTailPtr(TwoHeadArena* this) {
  return this->tail_p;
}
*/

/* @fabricated */
/*
extern void* THA_nextPtrN(TwoHeadArena* this, size_t n) {
  char* next_p = this->head_p;
  this->head_p += n;
  return (void*)next_p;
}
*/

/* @fabricated */
/*
extern void* THA_nextPtr1(TwoHeadArena* this) {
  return THA_nextPtrN(this, 1);
}
*/

/* @fabricated */
/*
extern void* THA_alloc(TwoHeadArena* this, size_t siz) {
  u32 mask;
  if (siz == 8) {
    mask = ~(8 - 1);
  } else if (siz == 2 || siz == 6 || siz == 10 || siz == 14) {
    mask = ~(2 - 1);
  } else if (siz > 15) {
    mask = ~(16 - 1);
  } else {
    mask = ~0;
  }

  this->tail_p = (char*)((((u32)this->tail_p & mask) - siz) & mask);
  return this->tail_p;
}
*/

#ifdef TARGET_PC
static void* tha_alloc_aligned(
    TwoHeadArena* this,
    size_t siz,
    uintptr_t alignment
) {
  AcgcAddressRange range;
  uintptr_t next_tail;

  if (acgc_address_range_make(
          (uintptr_t)this->buf_p, this->size, &range
      ) != ACGC_ADDRESS_OK ||
      acgc_address_tail_alloc(
          &range, (uintptr_t)this->tail_p, siz, alignment, &next_tail
      ) != ACGC_ADDRESS_OK) {
    return NULL;
  }

  this->tail_p = (char*)next_tail;
  return this->tail_p;
}
#endif

extern void* THA_alloc16(TwoHeadArena* this, size_t siz) {
#ifdef TARGET_PC
  return tha_alloc_aligned(this, siz, 16);
#else
  const int mask = ~(16 - 1);
  this->tail_p = (char*)((((u32)this->tail_p & mask) - siz) & mask);
  return this->tail_p;
#endif
}

extern void* THA_allocAlign(TwoHeadArena* this, size_t siz, int mask) {
#ifdef TARGET_PC
  uintptr_t alignment;

  if (acgc_address_alignment_from_mask(
          (uintptr_t)mask, &alignment
      ) != ACGC_ADDRESS_OK) {
    return NULL;
  }
  return tha_alloc_aligned(this, siz, alignment);
#else
  this->tail_p = (char*)((((u32)this->tail_p & mask) - siz) & mask);
  return this->tail_p;
#endif
}

extern int THA_getFreeBytesAlign(TwoHeadArena* this, int mask) {
  return (int)this->tail_p - (mask & (int)(this->head_p + ~mask));
}

extern int THA_getFreeBytes16(TwoHeadArena* this) {
  return THA_getFreeBytesAlign(this, -16);
}

extern int THA_getFreeBytes(TwoHeadArena* this) {
  return THA_getFreeBytesAlign(this, -1);
}

extern int THA_isCrash(TwoHeadArena* this) {
  return THA_getFreeBytes(this) < 0;
}

extern void THA_init(TwoHeadArena* this) {
  this->head_p = this->buf_p;
  this->tail_p = this->buf_p + this->size;
}

extern void THA_ct(TwoHeadArena* this, char* p, size_t n) {
  this->buf_p = p;
  this->size = n;
  THA_init(this);
}

extern void THA_dt(TwoHeadArena* this) {
  bzero(this, sizeof(TwoHeadArena));
}
