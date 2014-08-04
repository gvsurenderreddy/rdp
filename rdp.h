/*
 * rdp.h
 *
 *  Created on: 2013-07-02
 *      Author: Lucas Vasilakopoulos
 */

#ifndef RDP_H_
#define RDP_H_

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <errno.h>

#define MSS 512

typedef struct RDP_socket{
	int usock;
	struct sockaddr_in address; // destination
	struct sockaddr_in thisAddress; // source
	char connected;
}rdp_sock;

typedef struct RDP_hdr{
	char magic[7]; // CSC361L
	char type[3];
	uint32_t seq;
	uint32_t ack;
	uint16_t length;
	uint16_t size;
	char blank[4]; // \r\n\r\n;
}rdp_hdr;

typedef struct RDP_pkt{
	rdp_hdr header;
	char data[MSS/2];
}rdp_pkt;

int RDP_getSock(rdp_sock *sock);
int RDP_bind(rdp_sock *sock);
int RDP_connect(rdp_sock *sock);
int RDP_makePkt(char *type,char *data, uint16_t payload, rdp_pkt *pkt);
int64_t RDP_send(rdp_sock *sock, rdp_pkt *pkt, uint32_t length,
		int client, int log);
void printLog(int summary, int client, char eventType, rdp_sock *sock,
				rdp_pkt *pkt, double time_elapsed, int unique_bytes);
unsigned updateRTT(struct timeval *timeSent, unsigned oldRTT);
int64_t RDP_recv(rdp_sock *sock, char *buff, uint32_t length,
		int client, int log);

#endif /* RDP_H_ */
