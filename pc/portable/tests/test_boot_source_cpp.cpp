#include "acgc/boot_source.h"

int main() {
    AcgcBootSourceLimits limits{};
    AcgcBootSourceImages images{};

    acgc_boot_source_limits_default(&limits);
    if (limits.max_dol_size != ACGC_BOOT_SOURCE_MAX_DOL_SIZE) {
        return 1;
    }
    acgc_boot_source_dispose(&images);
    return images.dol_data == nullptr && images.rel_data == nullptr ? 0 : 1;
}
