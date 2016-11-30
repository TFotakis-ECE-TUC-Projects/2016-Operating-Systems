#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"

file_ops readFuncs = {
		.Open = NULL,
		.Read = pipe_read,
		.Write = dummyWrite,
		.Close = pipe_close
};
file_ops writeFuncs = {
		.Open = NULL,
		.Read = dummyRead,
		.Write = pipe_write,
		.Close = pipe_close
};
int Pipe(pipe_t *pipe) {
	int retValue = 0;
	Fid_t fid[2];
	FCB *fcb[2];
	Mutex_Lock(&kernel_mutex);
	if (!FCB_reserve(2, fid, fcb)) {
		retValue = -1;
		FCB_unreserve(2, fid, fcb);
	} else {
		pipe->read = fid[0];
		pipe->write = fid[1];
		PipeCB *pipeCB = (PipeCB *) xmalloc(sizeof(PipeCB));
		pipeCB->pipe = pipe;
		pipeCB->cvWrite = COND_INIT;
		pipeCB->cvRead = COND_INIT;
		pipeCB->readPos = 0;
		pipeCB->writePos = 0;
		fcb[0]->streamobj = pipeCB;
		fcb[1]->streamobj = pipeCB;
		fcb[0]->streamfunc = &readFuncs;
		fcb[1]->streamfunc = &writeFuncs;
		fcb[0]->refcount++;
		fcb[1]->refcount++;
	}
	Mutex_Unlock(&kernel_mutex);
	return retValue;
}
int pipe_read(void *pipeCB, char *buf, unsigned int size) {
	PipeCB *pipecb = (PipeCB *) pipeCB;
	while (pipecb->writePos == pipecb->readPos) {
		Cond_Wait(&kernel_mutex, &pipecb->cvRead);
	}
	uint count = 0;
	while (pipecb->readPos != pipecb->writePos && count < size) {
		buf[count] = pipecb->buffer[pipecb->readPos];
		pipecb->readPos = (pipecb->readPos + 1) % BUFFER_SIZE;
		count++;
	}
	if (get_fcb(pipecb->pipe->write) == NULL && pipecb->readPos == pipecb->writePos) return 0;
	Cond_Signal(&pipecb->cvWrite);
	return count;
}
int pipe_write(void *pipeCB, const char *buf, unsigned int size) {
	PipeCB *pipecb = (PipeCB *) pipeCB;
	if(get_fcb(pipecb->pipe->read)==NULL) return -1;
	while ((pipecb->writePos + 1) % BUFFER_SIZE == pipecb->readPos) {
		Cond_Wait(&kernel_mutex, &pipecb->cvWrite);
	}
	uint count = 0;
	while ((pipecb->writePos + 1) % BUFFER_SIZE != pipecb->readPos && count < size) {
		pipecb->buffer[pipecb->writePos] = buf[count];
		pipecb->writePos = (pipecb->writePos + 1) % BUFFER_SIZE;
		count++;
	}
	Cond_Signal(&pipecb->cvRead);
	return count;
}
int pipe_close(void *pipeCB) {
	MSG("Edo kanoume pipe close\n");
	return 0;
}
int dummyRead(void *pipeCB, char *buf, unsigned int size) {
	return -1;
}
int dummyWrite(void *pipeCB, const char *buf, unsigned int size) {
	return -1;
}