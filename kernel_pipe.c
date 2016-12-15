#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"

typedef struct pipe_control_block {
	pipe_t *pipe;
	FCB *readerFCB, *writerFCB;
	CondVar cvRead;
	CondVar cvWrite;
	Mutex mx;
	char buffer[BUFFER_SIZE];
	int readPos;
	int writePos;
} PipeCB;
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
	pipeCB->mx = MUTEX_INIT;
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
	Mutex_Lock(&pipecb->mx);
	if (pipecb->writerFCB->refcount == 0 && pipecb->readPos == pipecb->writePos) {
		Mutex_Unlock(&pipecb->mx);
		return 0;
	}
	uint count;
	for (count = 0; count < size; count++, pipecb->readPos = (pipecb->readPos + 1) % BUFFER_SIZE) {
		while (pipecb->writePos == pipecb->readPos && pipecb->writerFCB->refcount != 0) {
			Cond_Broadcast(&pipecb->cvWrite);
//			MSG("pipe read waiting\n");
			Cond_Wait(&pipecb->mx, &pipecb->cvRead);
//			MSG("pipe read wakeup\n");
		}
		if (pipecb->writePos == pipecb->readPos && pipecb->writerFCB->refcount == 0) {
			Mutex_Unlock(&pipecb->mx);
			return count;
		}
		buf[count] = pipecb->buffer[pipecb->readPos];
		Cond_Broadcast(&pipecb->cvWrite);
	}
	Mutex_Unlock(&pipecb->mx);
	Cond_Broadcast(&pipecb->cvWrite);
	return count;
}
int pipe_write(void *pipeCB, const char *buf, unsigned int size) {
	PipeCB *pipecb = (PipeCB *) pipeCB;
	Mutex_Lock(&pipecb->mx);
	uint count;
	for (count = 0; count < size; count++, pipecb->writePos = (pipecb->writePos + 1) % BUFFER_SIZE) {
		while ((pipecb->writePos + 1) % BUFFER_SIZE == pipecb->readPos && pipecb->readerFCB->refcount != 0) {
			Cond_Broadcast(&pipecb->cvRead);
//			MSG("pipe write waiting\n");
			Cond_Wait(&pipecb->mx, &pipecb->cvWrite);
//			MSG("pipe write wakeup\n");
		}
		if (pipecb->readerFCB->refcount == 0) {
			Mutex_Unlock(&pipecb->mx);
			return -1;
		}
		pipecb->buffer[pipecb->writePos] = buf[count];
		Cond_Broadcast(&pipecb->cvRead);
	}
	Mutex_Unlock(&pipecb->mx);
	Cond_Broadcast(&pipecb->cvRead);
	return count;
}
int pipe_closeReader(void *pipeCB) {
	PipeCB *pipecb = (PipeCB *) pipeCB;
//	Mutex_Lock(&pipecb->mx);
	MSG("close reader\n");
	if (pipecb->writerFCB->refcount == 0) {
		MSG("close pipe from reader\n");
//		Mutex_Unlock(&pipecb->mx);
		free(pipeCB);
	} else {
//		Mutex_Unlock(&pipecb->mx);
	}
	Cond_Broadcast(&pipecb->cvWrite);
	return 0;
}
int pipe_closeWriter(void *pipeCB) {
	PipeCB *pipecb = (PipeCB *) pipeCB;
//	Mutex_Lock(&pipecb->mx);
	MSG("close writer\n");
	if (pipecb->readerFCB->refcount == 0) {
		MSG("Close pipe from writer\n");
//		Mutex_Unlock(&pipecb->mx);
		free(pipeCB);
	} else {
//		Mutex_Unlock(&pipecb->mx);
	}
	Cond_Broadcast(&pipecb->cvRead);
	return 0;
}
int dummyRead(void *pipeCB, char *buf, unsigned int size) {
	return -1;
}
int dummyWrite(void *pipeCB, const char *buf, unsigned int size) {
	return -1;
}