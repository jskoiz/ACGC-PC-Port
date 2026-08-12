#include <stdio.h>

#include "JSystem/JSupport/JSUStreamEnum.h"
#include "JSystem/JSupport/JSUIosBase.h"

#ifndef SEEK_SET
#error "stdio SEEK_SET must remain available after JSystem headers"
#endif
#ifndef SEEK_CUR
#error "stdio SEEK_CUR must remain available after JSystem headers"
#endif
#ifndef SEEK_END
#error "stdio SEEK_END must remain available after JSystem headers"
#endif
#ifndef EOF
#error "stdio EOF must remain available after JSystem headers"
#endif

static_assert(JSU_STREAM_SEEK_SET == 0, "JSystem SEEK_SET value changed");
static_assert(JSU_STREAM_SEEK_CUR == 1, "JSystem SEEK_CUR value changed");
static_assert(JSU_STREAM_SEEK_END == 2, "JSystem SEEK_END value changed");
static_assert(JSU_IO_GOOD == 0, "JSystem GOOD value changed");
static_assert(JSU_IO_EOF == 1, "JSystem EOF value changed");

int main() {
    JSUIosBase state;
    if (!state.isGood()) {
        return 1;
    }

    state.setState(JSU_IO_EOF);
    if (state.mState != JSU_IO_EOF) {
        return 2;
    }
    state.clrState(JSU_IO_EOF);
    if (!state.isGood()) {
        return 3;
    }

    FILE* stream = tmpfile();
    if (stream == nullptr) {
        return 4;
    }

    if (fseek(stream, 0, SEEK_SET) != 0 ||
        fseek(stream, 0, SEEK_CUR) != 0 ||
        fseek(stream, 0, SEEK_END) != 0 ||
        fgetc(stream) != EOF) {
        fclose(stream);
        return 5;
    }

    fclose(stream);
    return 0;
}
