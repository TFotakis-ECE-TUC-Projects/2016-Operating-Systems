#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

typedef struct socket_control_block SCB;
typedef struct listener_requests {
	Fid_t fid;
	CondVar cv;
	int isServed;
} Request;
typedef struct listener_extra_properties {
	rlnode requests;
	CondVar cv;
} ListenerProps;
typedef struct peer_extra_properties {
	PipeCB *receiver, *transmitter;
	SCB *otherPeer;
	CondVar cv;
} PeerProps;
typedef struct socket_control_block {
	Fid_t fid;
	FCB *fcb;
	SocketType socketType;
	port_t boundPort;
	int refcount;
	union {
		ListenerProps *listenerProps;
		PeerProps *peerProps;
	} extraProps;
} SCB;
SCB *Portmap[MAX_PORT + 1] = {NULL};
int socket_close(void *fid) {
	Fid_t *fd = (Fid_t *) fid;
	for (int i = 0; i <= MAX_PORT; i++) {
		if (Portmap[i] == NULL)continue;
		if (Portmap[i]->fid == *fd) {
			if (Portmap[i]->socketType == LISTENER) {
				while (!is_rlist_empty(&Portmap[i]->extraProps.listenerProps->requests)) {
					rlnode *reqNode = rlist_pop_front(&Portmap[i]->extraProps.listenerProps->requests);
					Cond_Signal(&reqNode->request->cv);
				}
			}
			Portmap[i] = NULL;
			break;
		}
	}
	return 0;
}
int socket_read(void *tmpScb, char *buf, unsigned int size) {
//	MSG("Socket read\n");
	SCB *scb = (SCB *) tmpScb;
	if (scb->socketType != PEER)return 0;
	return pipe_read(scb->extraProps.peerProps->receiver, buf, size);
}
int socket_write(void *tmpScb, const char *buf, unsigned int size) {
//	MSG("Socket write\n");
	SCB *scb = (SCB *) tmpScb;
	if (scb->socketType != PEER)return -1;
	return pipe_write(scb->extraProps.peerProps->transmitter, buf, size);
}
file_ops PeerFuncs = {
		.Open = NULL,
		.Read = socket_read,
		.Write = socket_write,
		.Close = socket_close
};
file_ops UnboundFuncs = {
		.Open = NULL,
		.Read = dummyRead,
		.Write = dummyWrite,
		.Close = socket_close
};
SCB *get_scb(Fid_t sock) {
	if (sock < 0 || sock > MAX_FILEID)return NULL;
	FCB *fcb = get_fcb(sock);
	if (fcb == NULL)return NULL;
	return (SCB *) fcb->streamobj;
}
Fid_t Socket(port_t port) {
	if (port < 0 || port > MAX_PORT)return NOFILE;
	Fid_t fid;
	FCB *fcb;
	Mutex_Lock(&kernel_mutex);
	if (!FCB_reserve(1, &fid, &fcb)) {
		Mutex_Unlock(&kernel_mutex);
		return NOFILE;
	}
	SCB *scb = (SCB *) xmalloc(sizeof(SCB));
	scb->fid = fid;
	scb->fcb = fcb;
	scb->socketType = UNBOUND;
	scb->boundPort = port;
	scb->refcount = 0;
	fcb->streamobj = scb;
	fcb->streamfunc = &UnboundFuncs;
	Mutex_Unlock(&kernel_mutex);
	return fid;
}
int Listen(Fid_t sock) {
	Mutex_Lock(&kernel_mutex);
	SCB *scb = get_scb(sock);
	if (scb == NULL || scb->socketType != UNBOUND || scb->boundPort <= 0 || Portmap[scb->boundPort] != NULL) {
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	scb->socketType = LISTENER;
	scb->extraProps.listenerProps = (ListenerProps *) xmalloc(sizeof(ListenerProps));
	scb->extraProps.listenerProps->cv = COND_INIT;
	rlnode_new(&scb->extraProps.listenerProps->requests);
	Portmap[scb->boundPort] = scb;
	Mutex_Unlock(&kernel_mutex);
	return 0;
}
Fid_t Accept(Fid_t lsock) {
	Mutex_Lock(&kernel_mutex);
	SCB *listenerSCB = get_scb(lsock);
	if (listenerSCB == NULL || listenerSCB->socketType != LISTENER) {
		Mutex_Unlock(&kernel_mutex);
		return NOFILE;
	}
	while (is_rlist_empty(&listenerSCB->extraProps.listenerProps->requests) && get_scb(lsock)) {
		Cond_Wait(&kernel_mutex, &listenerSCB->extraProps.listenerProps->cv);
	}
	rlnode *requestNode = rlist_pop_front(&listenerSCB->extraProps.listenerProps->requests);
	Request *request = requestNode->request;
	if (!get_scb(lsock)) {
		Mutex_Unlock(&kernel_mutex);
		Cond_Signal(&request->cv);
		return NOFILE;
	}
	Mutex_Unlock(&kernel_mutex);
	Fid_t peer1fid = Socket(NOPORT);
	if (peer1fid == NOFILE) {
		Cond_Signal(&request->cv);
		return NOFILE;
	}
	Mutex_Lock(&kernel_mutex);

	SCB *peer1 = get_scb(peer1fid);
	SCB *peer2 = get_scb(request->fid);
	get_fcb(peer1fid)->streamfunc = &PeerFuncs;
	get_fcb(request->fid)->streamfunc = &PeerFuncs;
	peer1->extraProps.peerProps = (PeerProps *) xmalloc(sizeof(PeerProps));
	peer2->extraProps.peerProps = (PeerProps *) xmalloc(sizeof(PeerProps));
	peer1->extraProps.peerProps->cv = COND_INIT;
	peer2->extraProps.peerProps->cv = COND_INIT;
	pipe_t pipe1, pipe2;
//	Mutex_Unlock(&kernel_mutex);

	Fid_t fid[2];
	FCB *fcb[2];
	fid[0] = peer1fid;
	fid[1] = request->fid;
	fcb[0] = get_fcb(peer1fid);
	fcb[1] = get_fcb(request->fid);
//	if (!FCB_reserve(2, fid, fcb)) {
//		Mutex_Unlock(&kernel_mutex);
//		MSG("No more fids in accept\n");
//		return NOFILE;
//	}
	PipeCB *pipeCB1 = PipeNoReserving(&pipe1, fid, fcb);

	fid[0] = request->fid;
	fid[1] = peer1fid;
	fcb[0] = get_fcb(request->fid);
	fcb[1] = get_fcb(peer1fid);
	PipeCB *pipeCB2 = PipeNoReserving(&pipe2, fid, fcb);
//	Mutex_Lock(&kernel_mutex);
	peer1->extraProps.peerProps->transmitter = pipeCB1;
	peer1->extraProps.peerProps->receiver = pipeCB2;
	peer2->extraProps.peerProps->transmitter = pipeCB2;
	peer2->extraProps.peerProps->receiver = pipeCB1;
	peer1->extraProps.peerProps->otherPeer = peer2;
	peer2->extraProps.peerProps->otherPeer = peer1;
	peer1->socketType = PEER;
	peer2->socketType = PEER;
	request->isServed = 1;
	Mutex_Unlock(&kernel_mutex);
	Cond_Signal(&request->cv);
	return peer1fid;
}
int Connect(Fid_t sock, port_t port, timeout_t timeout) {
	Mutex_Lock(&kernel_mutex);
	SCB *scb = get_scb(sock);
	if (port < 0 || port >= MAX_PORT || Portmap[port] == NULL || Portmap[port]->socketType != LISTENER ||
	    scb->socketType != UNBOUND) {
//		MSG("Connect fails \n");
		Mutex_Unlock(&kernel_mutex);
		return -1;
	}
	Request *request = (Request *) xmalloc(sizeof(Request));
	request->cv = COND_INIT;
	request->isServed = 0;
	request->fid = sock;
	rlnode node;
	rlnode_init(&node, request);
	rlist_push_back(&Portmap[port]->extraProps.listenerProps->requests, &node);
	Cond_Signal(&Portmap[port]->extraProps.listenerProps->cv);
//	MSG("Sending request\n");
//	MSG("Connect before condwait\n");
	Cond_Wait(&kernel_mutex, &request->cv);
//	MSG("returned\n");
	Mutex_Unlock(&kernel_mutex);
	return request->isServed - 1;
}
int ShutDown(Fid_t sock, shutdown_mode how) {
	SCB *scb = get_scb(sock);
	switch (how) {
		case SHUTDOWN_READ:
			return pipe_closeReader(scb->extraProps.peerProps->receiver);
		case SHUTDOWN_WRITE:
			return pipe_closeWriter(scb->extraProps.peerProps->transmitter);
		case SHUTDOWN_BOTH:
			return pipe_closeReader(scb->extraProps.peerProps->receiver) +
			       pipe_closeWriter(scb->extraProps.peerProps->transmitter) < 0 ? -1 : 0;
	}
	return -1;
}