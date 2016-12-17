#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
file_ops readFuncs = {
        .Open = NULL,
        .Read = pipe_read,
        .Write = dummyWrite,
        .Close = pipe_closeReader
};
file_ops writeFuncs = {
        .Open = NULL,
        .Read = dummyRead,
        .Write = pipe_write,
        .Close = pipe_closeWriter
};
PipeCB *PipeNoReserving(pipe_t *pipe, Fid_t *fid, FCB **fcb) {
    pipe->read = fid[0];
    pipe->write = fid[1];
    PipeCB *pipeCB = (PipeCB *) xmalloc(sizeof(PipeCB));
    pipeCB->pipe = pipe;
    pipeCB->cvWrite = COND_INIT;
    pipeCB->cvRead = COND_INIT;
    pipeCB->readPos = 0;
    pipeCB->writePos = 0;
    pipeCB->readerFCB = fcb[0];
    pipeCB->writerFCB = fcb[1];
    return pipeCB;
}
int Pipe(pipe_t *pipe) {
    Fid_t fid[2];
    FCB *fcb[2];
    Mutex_Lock(&kernel_mutex);
    if (!FCB_reserve(2, fid, fcb)) {
        Mutex_Unlock(&kernel_mutex);
        return -1;
    }
    PipeCB *pipeCB = PipeNoReserving(pipe, fid, fcb);
    fcb[0]->streamobj = pipeCB;
    fcb[1]->streamobj = pipeCB;
    fcb[0]->streamfunc = &readFuncs;
    fcb[1]->streamfunc = &writeFuncs;
    Mutex_Unlock(&kernel_mutex);
    return 0;
}
int pipe_read(void *pipeCB, char *buf, unsigned int size) {
    PipeCB *pipecb = (PipeCB *) pipeCB;
    Mutex_Lock(&kernel_mutex);
    if (pipecb->writerFCB->refcount == 0 && pipecb->readPos == pipecb->writePos) {
        Mutex_Unlock(&kernel_mutex);
        return 0;
    }
    uint count;
    for (count = 0; count < size; count++, pipecb->readPos = (pipecb->readPos + 1) % BUFFER_SIZE) {
        while (pipecb->writePos == pipecb->readPos && pipecb->writerFCB->refcount != 0) {
            Cond_Broadcast(&pipecb->cvWrite);
            Cond_Wait(&kernel_mutex, &pipecb->cvRead);
        }
        if (pipecb->writePos == pipecb->readPos && pipecb->writerFCB->refcount == 0) {
            Mutex_Unlock(&kernel_mutex);
            return count;
        }
        buf[count] = pipecb->buffer[pipecb->readPos];
    }
    Mutex_Unlock(&kernel_mutex);
    Cond_Broadcast(&pipecb->cvWrite);
    return count;
}
int pipe_write(void *pipeCB, const char *buf, unsigned int size) {
    PipeCB *pipecb = (PipeCB *) pipeCB;
    Mutex_Lock(&kernel_mutex);
    uint count;
    for (count = 0; count < size; count++, pipecb->writePos = (pipecb->writePos + 1) % BUFFER_SIZE) {
        while ((pipecb->writePos + 1) % BUFFER_SIZE == pipecb->readPos && pipecb->readerFCB->refcount != 0) {
            Cond_Broadcast(&pipecb->cvRead);
            Cond_Wait(&kernel_mutex, &pipecb->cvWrite);
        }
        if (pipecb->readerFCB->refcount == 0) {
            Mutex_Unlock(&kernel_mutex);
            return -1;
        }
        pipecb->buffer[pipecb->writePos] = buf[count];
    }
    Mutex_Unlock(&kernel_mutex);
    Cond_Broadcast(&pipecb->cvRead);
    return count;
}
int pipe_closeReader(void *pipeCB) {
    PipeCB *pipecb = (PipeCB *) pipeCB;
    if (pipecb->writerFCB->refcount == 0) {
        Cond_Broadcast(&pipecb->cvWrite);
        free(pipecb);
    }
    return 0;
}
int pipe_closeWriter(void *pipeCB) {
    PipeCB *pipecb = (PipeCB *) pipeCB;
    if (pipecb->readerFCB->refcount == 0) {
        Cond_Broadcast(&pipecb->cvRead);
        free(pipecb);
    }
    return 0;
}
int dummyRead(void *pipeCB, char *buf, unsigned int size) {
    return -1;
}
int dummyWrite(void *pipeCB, const char *buf, unsigned int size) {
    return -1;
}