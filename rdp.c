/*
 * rdp.c
 *
 *  Created on: 2013-07-11
 *      Author: Lucas Vasilakopoulos
 */

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
#include "rdp.h"

#define TIMEOUT 0.05 // 50 milliseconds static RTT

int seqno = 0;
int tot_bytes_sent = 0;
int tot_bytes_recv = 0;
int unq_bytes_sent = 0;
int tot_pkts_sent = 0;
int tot_pkts_recv = 0;
int unq_pkts_sent = 0;
int unq_pkts_recv = 0;
int syn_pkts_sent = 0;
int syn_pkts_recv = 0;
int fin_pkts_sent = 0;
int fin_pkts_recv = 0;
int rst_pkts_sent = 0;
int rst_pkts_recv = 0;
int ack_pkts_sent = 0;
int ack_pkts_recv = 0;

clock_t s_start, c_start, s_end, c_end;

void printLog(int summary, int client, char eventType, rdp_sock *sock,
				rdp_pkt *pkt, double time_elapsed, int unique_bytes)
{
	char ip_src[INET_ADDRSTRLEN];
	char ip_dst[INET_ADDRSTRLEN];
	int src_port;
	int dst_port;
		
	if(summary)
	{
		if(client)
		{
			printf("\n");
			printf("total data bytes received: %d\n", tot_bytes_recv);
			printf("unique data bytes received: %d\n", unique_bytes);
			printf("total data packets received: %d\n", tot_pkts_recv);
			printf("unique data packets received: %d\n", unq_pkts_recv);
			printf("SYN packets received: %d\n", syn_pkts_recv);
			printf("FIN packets received: %d\n", fin_pkts_recv);
			printf("RST packets received: %d\n", rst_pkts_recv);
			printf("ACK packets sent: %d\n", ack_pkts_sent);
			printf("RST packets sent: %d\n", rst_pkts_sent);
			printf("total time duration (second): %0.3f\n", time_elapsed);
			printf("\n");
		}
		else
		{
			printf("\n");
			printf("total data bytes sent: %d\n", tot_bytes_sent);
			printf("unique data bytes sent: %d\n", unq_bytes_sent);
			printf("total data packets sent: %d\n", tot_pkts_sent);
			printf("unique data packets sent: %d\n", unq_pkts_sent);
			printf("SYN packets sent: %d\n", syn_pkts_sent);
			printf("FIN packets sent: %d\n", fin_pkts_sent);
			printf("RST packets sent: %d\n", rst_pkts_sent);
			printf("ACK packets received: %d\n", ack_pkts_recv);
			printf("RST packets received: %d\n", rst_pkts_recv);
			printf("total time duration (second): %0.3f\n", time_elapsed);
			printf("\n");
		}
	}
	else
	{
		time_t t = time(NULL);
		struct tm tim = *localtime(&t);

		struct timeval tv;
		gettimeofday(&tv,NULL);

		if(eventType == 's' || eventType == 'S')
		{		
			inet_ntop(AF_INET, &sock->thisAddress.sin_addr.s_addr,
						ip_src, INET_ADDRSTRLEN);

			src_port = (int)ntohs(sock->thisAddress.sin_port);

			inet_ntop(AF_INET, &sock->address.sin_addr.s_addr,
						ip_dst, INET_ADDRSTRLEN);

			dst_port = (int)ntohs(sock->address.sin_port);
		}
		else
		{
			inet_ntop(AF_INET, &sock->address.sin_addr.s_addr,
						ip_src, INET_ADDRSTRLEN);

			src_port = (int)ntohs(sock->address.sin_port);

			inet_ntop(AF_INET, &sock->thisAddress.sin_addr.s_addr,
						ip_dst, INET_ADDRSTRLEN);

			dst_port = (int)ntohs(sock->thisAddress.sin_port);
		}

 //HH:MM:SS.us event_type sip:spt dip:dpt packet_type seqno/ackno length/window
		printf("%02d:%02d:%02d.%06d %c %s:%d %s:%d %s %d %d\n",
				tim.tm_hour, tim.tm_min, tim.tm_sec, (int)tv.tv_usec,
					eventType, ip_src, src_port, ip_dst, dst_port,
						pkt->header.type, pkt->header.seq, pkt->header.length);
	}
}

/* basic function called to create a UDP socket and assign to RDP */
int RDP_getSock(rdp_sock *sock)
{
	sock->usock = socket(AF_INET, SOCK_DGRAM, 17);

	if(sock->usock == -1)
		return -1;

	sock->connected = 0;
	return 0;
}

/* basic function to bind address to socket */
int RDP_bind(rdp_sock *sock)
{
	if(bind(sock->usock, (struct sockaddr *)&sock->thisAddress,
			sizeof(struct sockaddr_in)) < 0)
		return -1;

	return 0;
}

/* this will initiate a SYN/ACK handshake; first called by client to send
   http request, then called by server before response data is sent */
int RDP_connect(rdp_sock *sock)
{
	rdp_pkt pkt;

	RDP_makePkt("SYN",NULL,0,&pkt);

	syn_pkts_sent++;

	if(RDP_send(sock, &pkt, sizeof(pkt), 0, 0) >= 0)
	{
		c_start = (double)clock();
		sock->connected = 1;
		return 0;
	}

	return -1;
}

/* RDP_recv() is multipurpose (client parameter is flag dictating which
   functionality the function should use. As the client, the function will
   loop endlessly calling recvfrom() until it receives the FIN packet from
   the server. The server can also call RDP_recv() when it is waiting for an
   ACK packet from the client. As such, the function has branching logic to
   accomodate this. For example, the server will not block on receiving while
   waiting for an ACK, while a client will wait for the FIN and never return
   otherwise. */
int64_t RDP_recv(rdp_sock *sock, char *buff, uint32_t length,
					int client, int log)
{
	int recv_check = 0;
	int64_t num_bytes = -1;
	int64_t bytes = 0;
	char tmp[284];
	socklen_t address_len = sizeof(struct sockaddr_in);

	rdp_pkt snd_pkt;
	memset(&snd_pkt, 0, sizeof(snd_pkt));

	bzero(buff, length);

	while(1)
	{
		// receive buffer from socket
        bytes = recvfrom(sock->usock, tmp, sizeof(tmp), 0,
				(struct sockaddr *)&sock->address, &address_len);
        
        // cast buffer as rdp_pkt to get info
		rdp_pkt *pkt = (rdp_pkt *)tmp;

		// if packet received is FIN, the given transaction is done
        if(strncmp(pkt->header.type,"FIN",3) == 0
				&& pkt->header.seq == seqno)
		{
			printLog(0, 0, 'r', sock, pkt, 0, 0);
			RDP_makePkt("ACK",NULL,0,&snd_pkt);
			RDP_send(sock, &snd_pkt, 0, client, log);
			printLog(0, 0, 's', sock, &snd_pkt, 0, 0);
			ack_pkts_sent++;
			fin_pkts_recv++;
			c_end = (double)clock();

			if(log)
			{
				printLog(1, 1, 0, NULL, NULL,
							(c_end-c_start)/(double)CLOCKS_PER_SEC, num_bytes);
			}

			c_start = (double)clock();

			if(client)
				seqno = !seqno;

			return num_bytes;
		}

		if (bytes < 0)
		{
			// if client, keep receiving until FIN, otherwise return
            if(client)
				continue;
			else
				return num_bytes;
		}
		else if (bytes == 0)
		{
			return num_bytes;
		}
		else
		{
			if(strncmp(pkt->header.type,"DAT",3) == 0)
			{
				tot_bytes_recv += pkt->header.length;
				tot_pkts_recv++;
			}

            // client: if the packet is correct sequence, send ACK and continue
            if(client)
			{
				if(pkt->header.seq == seqno)
				{
					RDP_makePkt("ACK",NULL,0,&snd_pkt);
					RDP_send(sock, &snd_pkt, 0, client, log);

					if(recv_check == 0)
						printLog(0, 0, 'r', sock, pkt, 0, 0);
					else
						recv_check = 0;

					printLog(0, 0, 's', sock, &snd_pkt, 0, 0);
					seqno = !seqno;
					unq_pkts_recv++; // client
					ack_pkts_sent++; // client
				}
				else
				{
					printLog(0, 0, 'R', sock, pkt, 0, 0);
					recv_check = 1;
					continue;
				}
			}
			else
			{
				// server: if SYN packet, send ACK and return
                if(strncmp(pkt->header.type,"SYN",3) == 0)
				{
					RDP_makePkt("ACK",NULL,0,&snd_pkt);
					printLog(0, 0, 'r', sock, pkt, 0, 0);
					printLog(0, 0, 's', sock, &snd_pkt, 0, 0);
					RDP_send(sock, &snd_pkt, 0, client, log);
					syn_pkts_recv++; // server
					ack_pkts_sent++; // server
					s_start = (double)clock();

					return 0;
				}
			}

			if(num_bytes == -1)
				num_bytes = 0 + pkt->header.length;
			else
				num_bytes += pkt->header.length;

			if(strncmp(pkt->header.type,"ACK",3) == 0)
				memcpy(buff,tmp,sizeof(rdp_pkt));
			else
			{
				memcpy(buff,pkt->data,pkt->header.length);
				buff+=pkt->header.length;
			}
		}
	}

	return -1;
}

/* when called, this function will create the packet type of your choosing */
int RDP_makePkt(char *type, char *data, uint16_t payload, rdp_pkt *pkt)
{
	memset(pkt, 0, sizeof(*pkt));

	if(strlen(type) == 0)
		return -1;

	if(strncmp(type,"DAT",3) != 0 && strncmp(type,"ACK",3) != 0
			&& strncmp(type,"SYN",3) != 0 && strncmp(type,"FIN",3) != 0
				&& strncmp(type,"RST",3) != 0)
		return -1;

	strncpy(pkt->header.type,type,3);
	strncpy(pkt->header.magic,"CSC361",7);
	strncpy(pkt->header.blank,"\r\n\r\n",4);
	pkt->header.seq = seqno;

	if(strncmp(type,"DAT",3) == 0)
	{
		if(payload <= MSS/2)
			memcpy(pkt->data,data,payload);
		else
			return -1;
	}

	if(strncmp(type,"FIN",3) == 0)
		fin_pkts_sent++; // server

	if(strncmp(type,"ACK",3) == 0)
		pkt->header.ack = 1;

	pkt->header.length = payload;

	return 0;
}

/* RDP_send() is also multipurpose and can be used by the client when sending 
   ACKs, or by the server when sending data to the client. the "client" flag
   again dictates which kind of functionality the function should use */
int64_t RDP_send(rdp_sock *sock, rdp_pkt *pkt, uint32_t length,
					int client, int log)
{
	int64_t bytes;
	int sent_check = 0;
	char send_buff[sizeof(rdp_pkt)];
	char ack_buff[sizeof(rdp_pkt)];

	clock_t timer;

	bzero(send_buff, sizeof(rdp_pkt));
	bzero(ack_buff, sizeof(rdp_pkt));

	// copy the packet into a buffer to be sent over the socket
    memcpy(send_buff,pkt,sizeof(rdp_pkt));

	while(1)
	{
		// send RDP packet to client
        bytes = sendto(sock->usock, send_buff, sizeof(rdp_pkt), 0,
				(struct sockaddr *)&sock->address, sizeof(struct sockaddr_in));

		// if we sent an ACK, we can break
        if(strncmp(pkt->header.type,"ACK",3) == 0)
			break;

		if(bytes > 0)
		{
			// start timeout timer once first data has been received
            timer = (double)clock();

			if(strncmp(pkt->header.type,"DAT",3) == 0)
			{
				tot_bytes_sent += pkt->header.length;
				tot_pkts_sent++;
			}

            if(client)
				return bytes;
		}
		else if(bytes == 0)
		{
			return bytes;
		}
		else
		{
			continue;
		}

		// while we haven't hit the timeout length, keep checking for ACK
        while(((double)clock()-timer)/(double)CLOCKS_PER_SEC < TIMEOUT)
		{
			if(RDP_recv(sock, ack_buff, sizeof(ack_buff), client, log) >= 0)
				break;
		}

		// cast received buffer as packet
        rdp_pkt *ack_pkt = (rdp_pkt *)ack_buff;

		if(sent_check == 0)
			printLog(0, 0, 's', sock, pkt, 0, 0);

		// if we got the right ACK, we're ok to move on
        if((strncmp(ack_pkt->header.type,"ACK",3) == 0)
				&& ack_pkt->header.seq == seqno)
		{
			ack_pkts_recv++;

			if(strncmp(pkt->header.type,"DAT",3) == 0)
			{
				unq_pkts_sent++;
				unq_bytes_sent += pkt->header.length;
			}

			sent_check = 0;
			printLog(0, 0, 'r', sock, ack_pkt, 0, 0);

			break;
		}
		else
		{
			printLog(0, 0, 'S', sock, pkt, 0, 0);
			sent_check = 1;
			continue;
		}
	}

	if((strncmp(pkt->header.type,"FIN",3) == 0)) // server
	{
		s_end = (double)clock();
		seqno = !seqno; // greasy hack for one case

		if(log)
		{
			printLog(1, 0, 0, NULL, NULL,
						(s_end-s_start)/(double)CLOCKS_PER_SEC, 0);
		}

		return bytes;
	}

	if(!client)
		seqno = !seqno;

	return bytes;
}
