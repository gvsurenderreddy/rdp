/*
 ============================================================================
 Name        : rwss.c
 Author      : Lucas Vasilakopoulos
 ============================================================================
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
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

#define P_BUFF 284 // size of one packet

int seq_num = 0;

int key_check()
{
	int key;
	struct termios def_terminal;
	struct termios raw_terminal;

	/* sets terminal to raw mode */
	tcgetattr(fileno(stdin), &def_terminal);
	memcpy(&raw_terminal, &def_terminal, sizeof(struct termios));

	raw_terminal.c_lflag &= ~(ECHO|ICANON);
	raw_terminal.c_cc[VTIME] = 0;
	raw_terminal.c_cc[VMIN] = 0;

	tcsetattr(fileno(stdin), TCSANOW, &raw_terminal);

	/* read character from stdin (no enter key necessary) */
	key = fgetc(stdin);

	/* revert to default terminal attributes */
	tcsetattr(fileno(stdin), TCSANOW, &def_terminal);

	return key;
}

void error(char* msg)
{
	fprintf(stderr, "%s", msg);
	exit(1);
}

void get_month(int month_num, char* month_name)
{
	switch(month_num)
	{
	case 1:
		strcpy(month_name,"Jan");
		break;
	case 2:
		strcpy(month_name,"Feb");
		break;
	case 3:
		strcpy(month_name,"Mar");
		break;
	case 4:
		strcpy(month_name,"Apr");
		break;
	case 5:
		strcpy(month_name,"May");
		break;
	case 6:
		strcpy(month_name,"Jun");
		break;
	case 7:
		strcpy(month_name,"Jul");
		break;
	case 8:
		strcpy(month_name,"Aug");
		break;
	case 9:
		strcpy(month_name,"Sep");
		break;
	case 10:
		strcpy(month_name,"Oct");
		break;
	case 11:
		strcpy(month_name,"Nov");
		break;
	case 12:
		strcpy(month_name,"Dec");
		break;
	}
}

void sequence(int code, rdp_sock *rsock,
					char* buffer, char* request, char* file_req)
{
	time_t t = time(NULL);
	struct tm tim = *localtime(&t);
	char month[4];
	char* pos;
	char tmp[P_BUFF];
	char tmp2[P_BUFF];
	char serv_info[P_BUFF];
	int pipe_out[2];
	int sav_stdout;

	rdp_pkt pkt;

	strcpy(tmp,&request[0]);
	memset(pipe_out, 0, sizeof(pipe_out));

	get_month(tim.tm_mon+1, month);

	char* ip = inet_ntoa(*(struct in_addr *)&rsock->address.sin_addr);
	int port = (int)ntohs(rsock->address.sin_port);

	while(*file_req != '/')
		file_req++;

	char path[strlen(file_req)+4];
	strcpy(path,"/www");
	strcat(path,file_req);

	switch(code)
	{
	case 200:

		printf("Seq no. %d %d %s %d %02d:%02d:%02d %s:%d %s; "
				"HTTP/1.0 200 OK; %s\n\n",
				seq_num, tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec, ip, port, buffer, path);
		fflush(stdout);	
		RDP_makePkt("DAT", "HTTP/1.0 200 OK\n", 16, &pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		sprintf(serv_info, "Date: %d %s %d %02d:%02d:%02d\n",
				tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec);
		RDP_makePkt("DAT", serv_info, strlen(serv_info),&pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);

		/* this block of piping is a bit nuts. The only way I could find
		 * to get MIME types with some kind of reliability was to use
		 * the "file -i" system command. Unfortunately, there is no version
		 * of system() where you can write the output to a buffer instead.
		 * To solve this, I created a pipe to intercept stdout and instead
		 * write it to a buffer, which I then write to the socket. Afterwards,
		 * reset stdout back to where it was before so the server can keep
		 * printing a log normally. */
		sav_stdout = dup(STDOUT_FILENO);

		if(pipe(pipe_out) != 0)
			error("Error: Failed to pipe stdout to buffer\n");

		dup2(pipe_out[1], STDOUT_FILENO);
		close(pipe_out[1]);
		strcpy(tmp2,"file -i ");
		strcat(tmp2,tmp);
		system(tmp2);
		fflush(stdout);
		read(pipe_out[0], serv_info, P_BUFF);
		dup2(sav_stdout, STDOUT_FILENO);

		if ((pos = strchr(serv_info, '\n')) != NULL)
			*pos = '\0';

		RDP_makePkt("DAT", "Content-Type: ", 14, &pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		RDP_makePkt("DAT", serv_info, strlen(serv_info),&pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		RDP_makePkt("DAT", "\n", 1, &pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		break;

	case 400:

		printf("Seq no. %d %d %s %d %02d:%02d:%02d %s:%d %s; "
				"HTTP/1.0 400 Invalid Request\n\n",
				seq_num, tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec, ip, port, buffer);
		RDP_makePkt("DAT", "HTTP/1.0 400 Invalid Request\n", 29, &pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		sprintf(serv_info, "Date: %d %s %d %02d:%02d:%02d\n",
				tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec);
		RDP_makePkt("DAT", serv_info, strlen(serv_info),&pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		break;

	case 403:

		printf("Seq no. %d %d %s %d %02d:%02d:%02d %s:%d %s; "
				"HTTP/1.0 403 Forbidden\n\n",
				seq_num, tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec, ip, port, buffer);
		RDP_makePkt("DAT", "HTTP/1.0 403 Forbidden\n", 23, &pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		sprintf(serv_info, "Date: %d %s %d %02d:%02d:%02d\n",
				tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec);
		RDP_makePkt("DAT", serv_info, strlen(serv_info),&pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		break;

	case 404:

		printf("Seq no. %d %d %s %d %02d:%02d:%02d %s:%d %s; "
				"HTTP/1.0 404 Not Found\n\n",
				seq_num, tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec, ip, port, buffer);
		RDP_makePkt("DAT", "HTTP/1.0 404 Not Found\n", 23, &pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		sprintf(serv_info, "Date: %d %s %d %02d:%02d:%02d\n",
				tim.tm_year + 1900, month, tim.tm_mday,
				tim.tm_hour, tim.tm_min, tim.tm_sec);
		RDP_makePkt("DAT", serv_info, strlen(serv_info),&pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
		break;
	}

	RDP_makePkt("DAT", "\r\n\r\n", 4, &pkt);
	RDP_send(rsock, &pkt, P_BUFF, 0, 1);

	if(code != 200)
	{
		RDP_makePkt("FIN",NULL,0,&pkt);
		RDP_send(rsock, &pkt, P_BUFF, 0, 1);
	}

	seq_num++;
}

void init_connec(int argc, char* argv[], rdp_sock *rsock)
{
	int portno;
	int on = 1;

	char tmp[P_BUFF];
	char cwd[P_BUFF];

	if (argc < 4)
		error("Error: Too few arguments. Usage:"
				"\n\t./rwss server_ip server_port directory");

	/* find working directory and append desired root directory for server */
	getcwd(cwd, P_BUFF);
	strcpy(tmp, argv[3]);
	strcat(cwd, tmp);

	/* change working directory to that specified */
	if (chdir(cwd) < 0)
		error("Error: Specified directory does not exist\n");

	RDP_getSock(rsock);
	fcntl(rsock->usock, F_SETFL, O_NONBLOCK);

	if (rsock->usock < 0)
		error("Error: Open socket failed\n");

	bzero((char *) &(rsock->thisAddress), sizeof(rsock->thisAddress));
	portno = atoi(argv[2]);
	setsockopt(rsock->usock, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));

	rsock->thisAddress.sin_family = AF_INET;
	rsock->thisAddress.sin_addr.s_addr = inet_addr(argv[1]);
	rsock->thisAddress.sin_port = htons(portno);

	if(RDP_bind(rsock) < 0)
		error("Error: Socket binding failed\n");

	printf("rwss is running on RDP port %s at %s and serving %s\n",
				argv[2], argv[1], argv[3]);
	printf("press 'q' to quit ...\n\n");
}

int parse(char* buffer, char* request)
{
	int i = 0;
	char* delims = " \r\n";
	char* ptr;
	char command[P_BUFF];
	char token[P_BUFF][P_BUFF];

	/* check if nothing or if special character */
	if(strlen(buffer) == 0 || (buffer[0] <= 31))
		return 0;

	memset(token, 0, sizeof token);

	strcpy(command, buffer);
	ptr = strtok(command, delims);
	strcpy(token[i], ptr);

	while(ptr != NULL)
	{
		ptr = strtok(NULL, delims);

		if (ptr != NULL)
			strcpy(token[++i], ptr);
	}

	if((strncasecmp(token[0],"GET",3) == 0))
	{
		if (strncasecmp(token[2],"HTTP/1.0",8) != 0)
		{
			return 0;
		}
		else
		{
			if(strncmp(token[1],"/",1) != 0)
			{
				return 0;
			}
			else if(strstr(token[1],"/.") != NULL)
			{
				return 0;
			}
			else if(strncmp(token[1],"/\0",2) == 0)
			{
				strcpy(request,"index.html");
				return 1;
			}
			else
			{
				strcpy(request,token[1]+1); // omit preceding '/' char
				return 1;
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int fd, bytes;
	char* pos;
	char buffer[sizeof(rdp_pkt)];
	char request[P_BUFF];
	char tmp[P_BUFF];
	rdp_sock rsock;
	rdp_pkt pkt;

	printf("\n");
	init_connec(argc, argv, &rsock);

	// loop until q key is pressed
	while (key_check() != 'q')
	{
		// clear buffers and read request from socket
		bzero(buffer, P_BUFF);
		bzero(request, P_BUFF);

		// wait for SYN from client, store socket information
		bytes = RDP_recv(&rsock, buffer, P_BUFF, 0, 0);

		if (bytes < 0)
			continue;

		/* wait for DAT request from client, then commence server SYN/ACK
		   handshake */
		while(RDP_recv(&rsock, buffer, P_BUFF, 1, 0) <= 0 );
		while(RDP_connect(&rsock) == -1);

		if ((pos = strchr(buffer, '\r')) != NULL ||
				(pos = strchr(buffer, '\n')) != NULL)
			*pos = '\0';

		printf("\n");

		if(parse(buffer, request) == 0)
		{
			sequence(400, &rsock, buffer, request, tmp);
			return 0;
		}

		strcpy(tmp,argv[2]);
		strcat(tmp,"/");
		strcat(tmp,request);

		// check for valid path and permissions
		access(request, R_OK);

		// check for bad permission. If not, try to open it normally
		if (errno != 13)
		{
			fd = open(request, O_RDONLY);

			if (fd < 0)
			{
				sequence(404, &rsock, buffer, request, tmp);
				close(fd);
				return 0;
			}
			else
			{
				sequence(200, &rsock, buffer, request, tmp);
			}
		}
		else
		{
			sequence(403, &rsock, buffer, request, tmp);
			return 0;
		}

		while(1)
		{
			// read file contents into the buffer
			bytes = read(fd, request, MSS/2);

			// checks for EOF. If we're done, send the FIN packet
			if (bytes <= 0)
			{
				RDP_makePkt("FIN",NULL,0,&pkt);
				RDP_send(&rsock, &pkt, P_BUFF, 0, 1);
				break;
			}

			// write contents of buffer to the socket (client)
			RDP_makePkt("DAT",request,bytes,&pkt);
			RDP_send(&rsock, &pkt, P_BUFF, 0, 1);
		}

		close(fd);
		close(rsock.usock);
		break;
	}

	return 0;
}
