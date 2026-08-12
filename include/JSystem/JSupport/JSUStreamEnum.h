#ifndef JSUSTREAMENUM_H
#define JSUSTREAMENUM_H

enum JSUStreamSeekFrom {
    JSU_STREAM_SEEK_SET = 0,
    JSU_STREAM_SEEK_CUR = 1,
    JSU_STREAM_SEEK_END = 2
};

enum EIoState {
    JSU_IO_GOOD = 0,
    JSU_IO_EOF = 1
};

#endif
