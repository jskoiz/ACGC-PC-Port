#include "acgc/boot_hot_start.h"

int acgc_boot_hot_start_step(
    void** entry,
    AcgcBootHotStartInvoke invoke,
    void* context
) {
    if (entry == NULL || *entry == NULL || invoke == NULL) {
        return 0;
    }
    *entry = invoke(*entry, context);
    return *entry != NULL;
}
