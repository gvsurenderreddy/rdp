/*------------------------------------------------------------------------------+
| Lucas Vasilakopoulos
+------------------------------------------------------------------------------*/

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

#define P_BUFF 284 // packet size
#define BUFF 10485760 // max receive size is 10mb

int main(int argc, char *argv[])
{
	rdp_sock rsock;
	rdp_pkt pkt;
	char buffer[P_BUFF];
	char *dat_start;
	char *out;
	char *head_pos;
	int data_pos = 0;
	int head_size;

	printf("\n");

	if(argc < 6)
	{
		printf("Too few arguments. Usage:"
				"\n\t./rwsc client_ip client_port server_ip server_port URL\n");
		return -1;
	}

	rsock.thisAddress.sin_family = AF_INET;
	rsock.thisAddress.sin_addr.s_addr = inet_addr(argv[1]);
	rsock.thisAddress.sin_port = htons(atoi(argv[2]));
	rsock.address.sin_family = AF_INET;
	rsock.address.sin_addr.s_addr = inet_addr(argv[3]);
	rsock.address.sin_port = htons(atoi(argv[4]));

	if(RDP_getSock(&rsock) == -1)
	{
		printf("Error creating socket.\n");
		return -1;
	}
	
	fcntl(rsock.usock, F_SETFL, O_NONBLOCK);

	if(RDP_bind(&rsock) == -1)
	{
		printf("Error binding socket.\n");
		return -1;
	}
	
	// request 10MB of memory for the received data
	char *data = malloc(sizeof(char)*BUFF);

	while(RDP_connect(&rsock) == -1);

	char *request = malloc(sizeof(char)*(18+strlen(argv[5])));
	strncpy(request, "GET ", 4);
	strncpy(request+4, argv[5], strlen(argv[5]));
	strncpy((request + 4 + strlen(argv[5])), " HTTP/1.0\r\n\r\n\0", 14);

	if(RDP_makePkt("DAT", request, (17+strlen(argv[5])), &pkt) != -1)
		RDP_send(&rsock, &pkt, BUFF, 0, 0);

	RDP_makePkt("FIN",NULL,0,&pkt);
	RDP_send(&rsock, &pkt, P_BUFF, 0, 0);

	while(RDP_recv(&rsock, buffer, P_BUFF, 0, 0) < 0 );

	free(request);

	if(data == NULL)
	{
		printf("Request for memory failed. Quit.\n");
		free(data);
		return -1;
	}

	int64_t data_bytes;

	data_bytes = RDP_recv(&rsock, data, BUFF, 1, 1);

	dat_start = strstr(data,"\r\n\r\n");

	if(dat_start == NULL)
	{
		free(data);
		return -1;
	}

	out = dat_start+4;
	head_pos = data;

	if(head_pos == NULL)
	{
		free(data);
		return 0;
	}

	// print HTTP header to stdout
    while(head_pos != out)
	{
		printf("%c", (int)*head_pos);
		head_pos++;
		data_pos++;
	}

	// if the request was invalid, return 0. Otherwise, save data to file
    if(strstr(data,"HTTP/1.0 200") == NULL)
	{
		free(data);
		return 0;
	}

	head_size = data_pos;

	FILE *output;

	output = fopen("data", "wb");

	if(output == NULL)
	{
		printf("Failed to open or create \"data\". Quit.\n");
		free(data);
		return -1;
	}

	fwrite(out, sizeof(char), data_bytes-head_size, output);
	fclose(output);

	printf("[Data has been saved to file in working directory]\n\n");

	free(data);
	return 0;
}
