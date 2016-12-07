#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

typedef struct listener_extra_properties {
	rlnode requests;
	CondVar cv;
} ListenerProps;
typedef struct peer_extra_properties {
	pipe_t receiver, transmitter;
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
			Portmap[i] = NULL;
			break;
		}
	}
	return 0;
}
file_ops SocketFuncs = {
		.Open = NULL,
		.Read = NULL,
		.Write = NULL,
		.Close = socket_close
};
typedef struct listener_requests {
	Fid_t fid;
	CondVar cv;
	int isServed;
} Request;
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
	fcb->streamfunc = &SocketFuncs;
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
	Portmap[scb->boundPort] = scb;
	scb->socketType = LISTENER;
	scb->extraProps.listenerProps = (ListenerProps *) xmalloc(sizeof(ListenerProps));
	scb->extraProps.listenerProps->cv = COND_INIT;
	rlnode_new(&scb->extraProps.listenerProps->requests);
	Mutex_Unlock(&kernel_mutex);
	return 0;
}
Fid_t Accept(Fid_t lsock) {
	Mutex_Lock(&kernel_mutex);
	SCB *listenerSCB = get_scb(lsock);
	if (listenerSCB == NULL || listenerSCB->socketType != LISTENER) {
		Mutex_Unlock(&kernel_mutex);
//		Cond_Signal(&request->cv);
//		MSG("Not a listener\n");
		return NOFILE;
	}
	while (is_rlist_empty(&listenerSCB->extraProps.listenerProps->requests) && get_scb(lsock)) {
//		MSG("Waiting for request\n");
		Cond_Wait(&kernel_mutex, &listenerSCB->extraProps.listenerProps->cv);
//		MSG("Waking up\n");
	}
//	MSG("Accepting request\n");
	rlnode *requestNode = rlist_pop_front(&listenerSCB->extraProps.listenerProps->requests);
	Request *request = requestNode->request;
	if (!get_scb(lsock)) {
		Mutex_Unlock(&kernel_mutex);
		Cond_Signal(&request->cv);
//		MSG("Closed listener\n");
		return NOFILE;
	}
//	MSG("rlnode captured\n");
//	MSG("Request captured\n");
	Mutex_Unlock(&kernel_mutex);
	Fid_t peer1fid = Socket(NOPORT);
	if (peer1fid == NOFILE) {
//		MSG("No socket\n");
		Cond_Signal(&request->cv);
		return NOFILE;
	}
	Mutex_Lock(&kernel_mutex);
//	MSG("Peer 1 socket initialized\n");
	SCB *peer1 = get_scb(peer1fid);
	SCB *peer2 = get_scb(request->fid);
	peer1->extraProps.peerProps = (PeerProps *) xmalloc(sizeof(PeerProps));
	peer2->extraProps.peerProps = (PeerProps *) xmalloc(sizeof(PeerProps));
//	MSG("PeerProps initialized\n");
	peer1->extraProps.peerProps->cv = COND_INIT;
	peer2->extraProps.peerProps->cv = COND_INIT;
	pipe_t peer1Pipe, peer2Pipe;
//	MSG("Before pipes\n");
	Mutex_Unlock(&kernel_mutex);
	Fid_t fid[2];
	FCB *fcb[2];
	fid[0] = peer1fid;
	fid[1] = request->fid;
	fcb[0] = get_fcb(peer1fid);
	fcb[1] = get_fcb(request->fid);
	PipeNoReserving(&peer1Pipe, fid, fcb);
	fid[0] = request->fid;
	fid[1] = peer1fid;
	fcb[0] = get_fcb(request->fid);
	fcb[1] = get_fcb(peer1fid);
	PipeNoReserving(&peer2Pipe, fid, fcb);
	Mutex_Lock(&kernel_mutex);
//	MSG("Pipes initialized\n");
	peer1->extraProps.peerProps->transmitter = peer1Pipe;
	peer1->extraProps.peerProps->receiver = peer2Pipe;
	peer2->extraProps.peerProps->transmitter = peer2Pipe;
	peer2->extraProps.peerProps->receiver = peer1Pipe;
	peer1->socketType = PEER;
	peer2->socketType = PEER;
	request->isServed = 1;
	Cond_Signal(&request->cv);
//	MSG("Request served\n");
	Mutex_Unlock(&kernel_mutex);
	return peer1fid;
}
int Connect(Fid_t sock, port_t port, timeout_t timeout) {
	Mutex_Lock(&kernel_mutex);
	SCB *scb = get_scb(sock);
	if (port < 0 || port >= MAX_PORT || Portmap[port] == NULL || Portmap[port]->socketType != LISTENER ||
	    scb->socketType != UNBOUND) {
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
	Cond_Wait(&kernel_mutex, &request->cv);
	Mutex_Unlock(&kernel_mutex);
	return request->isServed - 1;
}
int ShutDown(Fid_t sock, shutdown_mode how) {
	return -1;
}