#include <dolphin/os.h>

#include "JSystem/JKernel/JKRAram.h"
#include "JSystem/JSupport/JSUStream.h"
#include "JSystem/JSystem.h"
#include "JSystem/JUtility/JUTAssertion.h"
#include "types.h"

// From Pikmin Repo

OSMessage JKRAramStream::sMessageBuffer[4] = { 0 };
OSMessageQueue JKRAramStream::sMessageQueue = { 0 };

JKRAramStream* JKRAramStream::sAramStreamObject = nullptr;
u8* JKRAramStream::transBuffer = nullptr;
u32 JKRAramStream::transSize = (u32)0;
JKRHeap* JKRAramStream::transHeap = nullptr;

/*
 * JKRAramPcs still consumes the legacy fixed-width MRAM/ARAM endpoint ABI.
 * Keep the native pointer visible while checking that boundary explicitly;
 * truncating an arm64 host pointer would turn a valid transfer into an
 * unrelated address.
 */
static bool tryLegacyAddress(const void* pointer, u32* address) {
    const uintptr_t nativeAddress = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t maxLegacyAddress = static_cast<uintptr_t>(~static_cast<u32>(0));

    if (pointer == nullptr || address == nullptr || nativeAddress > maxLegacyAddress) {
        return false;
    }

    *address = static_cast<u32>(nativeAddress);
    return true;
}

static bool tryAlignTransferBuffer(u8* buffer, u8** alignedBuffer) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(buffer);
    const uintptr_t mask = static_cast<uintptr_t>(0x1F);
    const uintptr_t maxAddress = ~static_cast<uintptr_t>(0);

    if (buffer == nullptr || alignedBuffer == nullptr || address > maxAddress - mask) {
        return false;
    }

    *alignedBuffer = reinterpret_cast<u8*>((address + mask) & ~mask);
    return true;
}

JKRAramStream* JKRAramStream::create(s32 param) {
    if (JKRAramStream::sAramStreamObject == nullptr) {
        JKRAramStream::sAramStreamObject = new (JKRGetSystemHeap(), 0) JKRAramStream(param);
        setTransBuffer(nullptr, 0, nullptr);
    }
    return JKRAramStream::sAramStreamObject;
}

JKRAramStream::JKRAramStream(s32 priority) : JKRThread(0x4000, 0x10, priority) {
    OSResumeThread(mThreadRecord);
}

JKRAramStream::~JKRAramStream() {};

void* JKRAramStream::run() {
    OSMessage result;
    OSInitMessageQueue(&JKRAramStream::sMessageQueue, JKRAramStream::sMessageBuffer,
                       ARRAY_COUNT(sMessageBuffer)); // jank cast to void** to satisfy prototype
    while (true) {
        OSReceiveMessage(&JKRAramStream::sMessageQueue, &result, OS_MESSAGE_BLOCK);
        JKRAramStreamCommand* command = static_cast<JKRAramStreamCommand*>(result);
        switch (command->type) {
            case JKRAramStreamCommand::ECT_READ:
                readFromAram();
                break;
            case JKRAramStreamCommand::ECT_WRITE:
                writeToAram(command);
                break;
        }
    }
}

u32 JKRAramStream::readFromAram() {
    return 1;
} // probably a define evaluating to 1

s32 JKRAramStream::writeToAram(JKRAramStreamCommand* command) {
    u32 dstSize = command->mSize;
    u32 offset = command->mOffset;
    u32 writtenLength = 0;
    u32 destination = command->mAddress;
    u8* buffer = command->mTransferBuffer;
    u32 bufferSize = command->mTransferBufferSize;
    JKRHeap* heap = command->mHeap;
    if (buffer) {
        bufferSize = (bufferSize == (u32)0) ? 0x8000 : bufferSize;

        command->mTransferBufferSize = bufferSize;
        command->mAllocatedTransferBuffer = false;
    } else {
        bufferSize = (bufferSize == (u32)0) ? 0x8000 : bufferSize;

        if (heap) {
            buffer = (u8*)JKRAllocFromHeap(heap, bufferSize, -0x20);
            command->mTransferBuffer = buffer;
        } else {
            buffer = (u8*)JKRAllocFromHeap(nullptr, bufferSize, -0x20);
            command->mTransferBuffer = buffer;
        }

        command->mTransferBufferSize = bufferSize;
        command->mAllocatedTransferBuffer = true;
    }

    if (!buffer) {
        if (!heap) {
            JKRGetCurrentHeap()->dump();
        } else {
            heap->dump();
        }
        JPANIC(169, "abort\n");
    }

    if (buffer) {
        u32 bufferAddress;
        if (!tryLegacyAddress(buffer, &bufferAddress)) {
            JPANIC(170, "host transfer buffer address is not representable\n");
            if (command->mAllocatedTransferBuffer) {
                JKRFree(buffer);
                command->mAllocatedTransferBuffer = false;
            }
            OSSendMessage(&command->mMessageQueue, (OSMessage)0, OS_MESSAGE_NOBLOCK);
            return 0;
        }

        command->mStream->seek(offset, JSU_STREAM_SEEK_SET);
        while (dstSize != 0) {
            u32 length = (dstSize > bufferSize) ? bufferSize : dstSize;

            s32 readLength = command->mStream->read(buffer, length);

            JKRAramPcs(0, bufferAddress, destination, length, nullptr);
            dstSize -= length;
            writtenLength += length;
            destination += length;
        }

        if (command->mAllocatedTransferBuffer) {
            JKRFree(buffer);
            command->mAllocatedTransferBuffer = false;
        }
    }

    OSSendMessage(&command->mMessageQueue, (OSMessage)writtenLength, OS_MESSAGE_NOBLOCK);
    return writtenLength;
};

/*
 * Unused function, made-up contents. Do not take this seriously!
 * While the function exists in the map, this is almost certainly incorrect.
 * Should exist to generate JSURandomInputStream::getAvailable() const
 * afterwards.
 */
JKRAramStreamCommand* JKRAramStream::write_StreamToAram_Async(JSUFileInputStream* stream, JKRAramBlock* addr, u32 size,
                                                              u32 offset) {
    JKRAramStreamCommand* command = new (JKRGetSystemHeap(), -4) JKRAramStreamCommand();
    command->type = JKRAramStreamCommand::ECT_WRITE;
    command->mAddress = addr == nullptr ? 0 : addr->getAddress();
    command->mSize = size;
    command->mStream = stream;
    command->_28 = stream->getAvailable();
    command->mOffset = offset;
    command->mTransferBuffer = transBuffer;
    command->mHeap = transHeap;
    command->mTransferBufferSize = transSize;

    OSInitMessageQueue(&command->mMessageQueue, &command->mMessage, 1);
#ifdef TARGET_PC
    /* Execute synchronously on PC (no worker thread) */
    sAramStreamObject->writeToAram(command);
#else
    OSSendMessage(&sMessageQueue, command, OS_MESSAGE_BLOCK);
#endif
    return command;
}

JKRAramStreamCommand* JKRAramStream::write_StreamToAram_Async(JSUFileInputStream* stream, u32 addr, u32 size,
                                                              u32 offset) {
    JKRAramStreamCommand* command = new (JKRGetSystemHeap(), -4) JKRAramStreamCommand();
    command->type = JKRAramStreamCommand::ECT_WRITE;
    command->mAddress = addr;
    command->mSize = size;
    command->mStream = stream;
    command->_28 = 0;
    command->mOffset = offset;
    command->mTransferBuffer = transBuffer;
    command->mHeap = transHeap;
    command->mTransferBufferSize = transSize;

    OSInitMessageQueue(&command->mMessageQueue, &command->mMessage, 1);
#ifdef TARGET_PC
    /* Execute synchronously on PC (no worker thread) */
    sAramStreamObject->writeToAram(command);
#else
    OSSendMessage(&sMessageQueue, command, OS_MESSAGE_BLOCK);
#endif
    return command;
}

JKRAramStreamCommand* JKRAramStream::sync(JKRAramStreamCommand* command, BOOL isNonBlocking) {
#ifdef TARGET_PC
    /* On PC, the write already happened synchronously - just return the command */
    return command;
#else
    OSMessage msg;
    if (isNonBlocking == FALSE) {
        OSReceiveMessage(&command->mMessageQueue, &msg, OS_MESSAGE_BLOCK);
        if (msg == nullptr) {
            command = nullptr;
            return command;
        } else {
            return command;
        }
    } else {
        BOOL receiveResult = OSReceiveMessage(&command->mMessageQueue, &msg, OS_MESSAGE_NOBLOCK);
        if (receiveResult == FALSE) {
            command = nullptr;
            return command;
        } else if (msg == nullptr) {
            command = nullptr;
            return command;
        } else {
            return command;
        }
    }
#endif
}

void JKRAramStream::setTransBuffer(u8* buffer, u32 bufferSize, JKRHeap* heap) {
    transBuffer = nullptr;
    transSize = 0x8000;
    transHeap = nullptr;

    if (buffer) {
        if (!tryAlignTransferBuffer(buffer, &transBuffer)) {
            JPANIC(171, "transfer buffer address alignment overflow\n");
            transBuffer = nullptr;
        }
    }

    if (bufferSize) {
        transSize = ALIGN_PREV(bufferSize, 0x20);
    }

    if (heap && !buffer) {
        transHeap = heap;
    }
}

JKRAramStreamCommand::JKRAramStreamCommand() {
    mAllocatedTransferBuffer = false;
}
