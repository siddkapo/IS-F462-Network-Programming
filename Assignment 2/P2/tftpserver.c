#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR 5

#define ERR_ND 0
#define ERR_FNF 1
#define ERR_AD 2
#define ERR_FULL 3
#define ERR_INVAL 4
#define ERR_UNKNOWN 5
#define ERR_FAE 6
#define ERR_NOUSR 7

#define DATA_SIZE 512
#define MAX_RETRY 3
#define TIME_OUT 5

typedef struct REQMSG {
	short opcode;
	char filenamemode[DATA_SIZE + 2];
} REQMSG;

typedef struct DATAMSG {
	short opcode;
	short blocknum;
	char data[DATA_SIZE];
} DATAMSG;

typedef struct ACKMSG {
	short opcode;
	short blocknum;
} ACKMSG;

typedef struct ERRORMSG {
	short opcode;
	short errorcode;
	char errormsg[DATA_SIZE];
} ERRORMSG;

typedef struct RECVMSG {
	short opcode;
	char msg[DATA_SIZE + 2];
} RECVMSG;

void SendError(long int sockfd, int errnum, struct sockaddr_in * sndaddr, char * errmsg, socklen_t sndaddrlen) {

	ERRORMSG msg;
	msg.opcode = htons(ERROR); // check
	msg.errorcode = htons(errnum); // check
	switch(errnum) {
		case ERR_ND: strcpy(msg.errormsg, errmsg);
		break;
		case ERR_FNF: strcpy(msg.errormsg, "FILE NOT FOUND\n");
		break;
		case ERR_AD: strcpy(msg.errormsg, "ACCESS DENIED\n");
		break;
		case ERR_FULL: strcpy(msg.errormsg, "ALLOCATION EXCEEDED\n");
		break;
		case ERR_INVAL: strcpy(msg.errormsg, "INVALID TFTP OPERATION\n");
		break;
		case ERR_UNKNOWN: strcpy(msg.errormsg, "UNKNOWN TID\n");
		break;
		case ERR_FAE: strcpy(msg.errormsg, "FILE ALREADY EXISTS\n");
		break;
		case ERR_NOUSR: strcpy(msg.errormsg, "NO SUCH USER\n");
		break;
	}

	ssize_t s = sendto(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *) sndaddr, sndaddrlen);
	if(s < 0) {
		perror("SendError:sendto");
	}

	return;
}

ssize_t SendData(long int sockfd, long int filefd, int repeat, struct sockaddr_in * sndaddr, socklen_t sndaddrlen) {

	ssize_t datalen;

	off_t curr = lseek(filefd, 0, SEEK_CUR);
	off_t start = lseek(filefd, 0, SEEK_SET);
	off_t end = lseek(filefd, 0, SEEK_END);

	if(repeat) {
		if(curr % DATA_SIZE == 0) curr -= DATA_SIZE;
		else curr -= (curr % DATA_SIZE);
	}

	lseek(filefd, curr, SEEK_SET);
	char buf[DATA_SIZE];
	datalen = read(filefd, buf, DATA_SIZE);
	if(datalen < 0) {
		perror("SendData:read");
		SendError(sockfd, ERR_AD, sndaddr, NULL, sndaddrlen);
		return -1;
	}

	DATAMSG msg;
	msg.opcode = htons(DATA);
	msg.blocknum = htons((curr - start) / DATA_SIZE + 1);

	if(curr == end) {
		strncpy(msg.data, "\0", 1);
		datalen = 0;
	} else {
		strncpy(msg.data, buf, datalen);
	}
	datalen += 2 * sizeof(short);

	struct sockaddr_in srcaddr;
	socklen_t addrlen = sizeof(srcaddr);
	getsockname(sockfd, (struct sockaddr *) &srcaddr, &addrlen);
	ssize_t s = sendto(sockfd, &msg, datalen, 0, (struct sockaddr *) sndaddr, sndaddrlen);
	if(s < 0) {
		perror("SendData:sendto");
	}

	return datalen;
}

int main(int argc, char * argv[]) {

	if(argc != 2) {
		printf("Invalid Arguments\n");
		return 0;
	}

	int listenport = atoi(argv[1]);

	printf("Establishing Connection...\n");

	int servsockfd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(listenport);
	bind(servsockfd, (struct sockaddr *) &servaddr, sizeof(servaddr));

	socklen_t addrlen = sizeof(servaddr);
	getsockname(servsockfd, (struct sockaddr *) &servaddr, &addrlen);
	printf("Connected to Port %d\n", ntohs(servaddr.sin_port));
	printf("Server Socket FD = %d\n", servsockfd);

	int len;
	RECVMSG msg;
	struct sockaddr_in clientaddr, newaddr;

	int maxfd = servsockfd + 1;
	long int clientfd[FD_SETSIZE][5]; // Client Socket FD, Requested File FD, Number of blocks, Number of retries (Set to 0 if successful), Time of last msg recv
	for(int i = 0; i < FD_SETSIZE; ++i) clientfd[i][0] = -1;
	int maxi = -1;

	fd_set sockfdset, recvfdset, sndfdset, excpfdset;
	FD_ZERO(&sockfdset);
	FD_SET(servsockfd, &sockfdset);
	int numset;
	int newsockfd;

	struct timeval timeout;

	while(1) {

		printf("\nWaiting for requests...\n");

		addrlen = sizeof(clientaddr);
		recvfdset = sockfdset;
		numset = select(maxfd, &recvfdset, NULL, NULL, NULL);

		if(numset < 0) {
			perror("select");
			continue;
		}

		printf("Received Requests\n");

		for(int i = 0; i <= maxfd; ++i) {

			if(FD_ISSET(i, &recvfdset)) {

				printf("Message from Port %d\n", ntohs(clientaddr.sin_port));
				// printf("Socket FD = %d\t%ld\n", i, clientfd[i][0]);

				clientfd[i][3] = 0;
				addrlen = sizeof(clientaddr);
				recvfrom(i, &msg, sizeof(msg), 0, (struct sockaddr *) &clientaddr, &addrlen);
				printf("MESSAGE OPCODE = %d\n", ntohs(msg.opcode));

				if(i == servsockfd) {
					// NEW CONNECTION

					if(ntohs(msg.opcode) == RRQ) {

						newsockfd = socket(AF_INET, SOCK_DGRAM, 0);
						FD_SET(newsockfd, &sockfdset);
						memset(&newaddr, 0, sizeof(newaddr));
						newaddr.sin_family = AF_INET;
						newaddr.sin_addr.s_addr = htons(INADDR_ANY);
						newaddr.sin_port = htons(0);
						bind(newsockfd, (struct sockaddr *) &newaddr, sizeof(newaddr));
						if(newsockfd >= maxfd) maxfd = newsockfd + 1;

						REQMSG * newmsg = (REQMSG *) &msg;
						char * filename = strtok(newmsg->filenamemode, "\0");
						int j = newsockfd;

						clientfd[j][0] = newsockfd;
						clientfd[j][1] = open(filename, O_RDONLY);
						clientfd[j][3] = 0;

						if(clientfd[j][1] == -1) {
							perror("open");
							SendError(servsockfd, ERR_FNF, &clientaddr, NULL, addrlen);

						} else {

							off_t begin = lseek(clientfd[j][1], 0, SEEK_SET);
							off_t end = lseek(clientfd[j][1], 0, SEEK_END);
							lseek(clientfd[j][1], 0, SEEK_SET);
							clientfd[j][2] = (end - begin) / DATA_SIZE + 1;

							int datalen = SendData(newsockfd, clientfd[j][1], 0, &clientaddr, addrlen);
							gettimeofday(&timeout, NULL);
							clientfd[j][4] = timeout.tv_sec;
						}

					} else {
						// ERROR
						printf("Error Occurred\n");
						SendError(servsockfd, ERR_INVAL, &clientaddr, NULL, addrlen);
					}

				} else {
					// HANDLE MESSAGE
					if(ntohs(msg.opcode) == ACK) {

						ACKMSG * ackmsg = (ACKMSG *) &msg;
						printf("ACK BLOCK NUM = %d\n", ntohs(ackmsg->blocknum));

						if(clientfd[i][2] == ntohs(ackmsg->blocknum)) {

							close(clientfd[i][1]);
							close(clientfd[i][0]);
							clientfd[i][0] = -1;
							FD_CLR(i, &sockfdset);
							printf("CLOSING SOCKET FD %d\n", i);

						} else {

							int datalen = SendData(i, clientfd[i][1], 0, &clientaddr, addrlen);
							gettimeofday(&timeout, NULL);
							clientfd[i][4] = timeout.tv_sec;

						}
						printf("Completed Transmission to Port %d\n", ntohs(clientaddr.sin_port));

					} else if(ntohs(msg.opcode) == ERROR) {

						ERRORMSG * errmsg = (ERRORMSG *) &msg;
						printf("Error Occurred On Port %d: ", ntohs(clientaddr.sin_port));
						switch(ntohs(errmsg->errorcode)) {
							case ERR_ND: printf("NOT DEFINED: ");
							break;
							case ERR_FNF: printf("NOT FOUND: ");
							break;
							case ERR_AD: printf("ACCESS DENIED: ");
							break;
							case ERR_FULL: printf("ALLOCATION FULL: ");
							break;
							case ERR_INVAL: printf("INVALID OPERATION: ");
							break;
							case ERR_UNKNOWN: printf("UNKNOWN TID: ");
							break;
							case ERR_FAE: printf("FILE ALREADY EXISTS: ");
							break;
							case ERR_NOUSR: printf("NO SUCH USER: ");
							break;
							default: printf("INVALID ERROR CODE: ");
						}
						printf("%s\n", errmsg->errormsg);

						close(clientfd[i][0]);
						close(clientfd[i][1]);
						clientfd[i][0] = -1;
						FD_CLR(i, &sockfdset);
						printf("CLOSING SOCKET FD %d\n", i);

					} else {

						printf("Sending Error Message...\n");
						SendError(clientfd[i][0], ERR_INVAL, &clientaddr, NULL, addrlen);
						close(clientfd[i][0]);
						close(clientfd[i][1]);
						clientfd[i][0] = -1;
						FD_CLR(i, &sockfdset);
						printf("CLOSING SOCKET FD %d\n", i);
					}
				}
			} else if(clientfd[i][0] != -1) {
				//HANDLE TIMEOUT
				printf("Checking Timeout...\n");
				printf("Socket FD = %d\n", i);

				if(clientfd[i][3] == MAX_RETRY) {

					clientfd[i][3] = 0;
					SendError(i, ERR_ND, &clientaddr, "CONNECTION TIMEOUT\n", addrlen);
					close(clientfd[i][1]);
					close(clientfd[i][0]);
					clientfd[i][0] = -1;
					FD_CLR(i, &sockfdset);
					printf("CLOSING SOCKET FD %d\n", i);

				} else {

					gettimeofday(&timeout, NULL);
					if(timeout.tv_sec - clientfd[i][4] >= TIME_OUT) {

						printf("Retransmitting...\n");
						int datalen = SendData(i, clientfd[i][1], 1, &clientaddr, addrlen);
						gettimeofday(&timeout, NULL);
						clientfd[i][4] = timeout.tv_sec;
						clientfd[i][3]++;
					}
				}
			}
		}
	}

	return 0;
}