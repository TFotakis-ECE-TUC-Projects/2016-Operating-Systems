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
int Pipe(pipe_t *pipe) {
	Fid_t fid[2];
	FCB *fcb[2];
	Mutex_Lock(&kernel_mutex);
	if (!FCB_reserve(2, fid, fcb)) {
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
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
	FCB_incref(fcb[0]);
	FCB_incref(fcb[1]);
	Mutex_Unlock(&kernel_mutex);
	return 0;
}
int pipe_read(void *pipeCB, char *buf, unsigned int size) {
	PipeCB *pipecb = (PipeCB *) pipeCB;
	uint count = 0;
	for (count = 0; count < size; count++) {
		while (pipecb->writePos == pipecb->readPos && get_fcb(pipecb->pipe->write) != NULL) {
			MSG("GT DEN PERIMENO???\n");
			Cond_Signal(&pipecb->cvWrite);
			Cond_Wait(&kernel_mutex, &pipecb->cvRead);
		}
		if (get_fcb(pipecb->pipe->write) == NULL && pipecb->readPos == pipecb->writePos) {
			pipe_closeReader(pipecb);
//			MSG("EXO KLEISTO WRITER kai kleino me count=%d kai readPos=%d kai writePos=%d\n",count,pipecb->readPos,pipecb->writePos);
			return 0;
		}
		buf[count] = pipecb->buffer[pipecb->readPos];
		pipecb->readPos = (pipecb->readPos + 1) % BUFFER_SIZE;
	}
	Cond_Signal(&pipecb->cvWrite);
	return count;
}
int pipe_write(void *pipeCB, const char *buf, unsigned int size) {
	PipeCB *pipecb = (PipeCB *) pipeCB;
	uint count;
	for (count = 0; count < size; count++) {
		while ((pipecb->writePos + 1) % BUFFER_SIZE == pipecb->readPos && get_fcb(pipecb->pipe->read) != NULL) {
			Cond_Signal(&pipecb->cvRead);
			Cond_Wait(&kernel_mutex, &pipecb->cvWrite);
		}
		if (get_fcb(pipecb->pipe->read) == NULL) {
//			MSG("CLOSED READER\n");
			pipe_closeWriter(pipecb);
			return -1;
		}
		pipecb->buffer[pipecb->writePos] = buf[count];
		pipecb->writePos = (pipecb->writePos + 1) % BUFFER_SIZE;
	}
	Cond_Signal(&pipecb->cvRead);
	return count;
}
int pipe_closeReader(void *pipeCB) {
//	FCB *fcb = get_fcb(((PipeCB*)pipeCB)->pipe->read);
//	FCB_unreserve(1,&((PipeCB*)pipeCB)->pipe->read, &fcb);
//	free(pipeCB);
//	MSG("KLEINO READER\n");
	return 0;
}
int pipe_closeWriter(void *pipeCB) {
//	FCB *fcb = get_fcb(((PipeCB*)pipeCB)->pipe->write);
//	FCB_unreserve(1,&((PipeCB*)pipeCB)->pipe->write, &fcb);
//	free(pipeCB);
//	MSG("KLEINO WRITER\n");
	return 0;
}
int dummyRead(void *pipeCB, char *buf, unsigned int size) {
	return -1;
}
int dummyWrite(void *pipeCB, const char *buf, unsigned int size) {
	return -1;
}