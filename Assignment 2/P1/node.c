#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_NUM_LEN 10
#define MAX_ARGS 5
#define MAX_STR_LEN 10
#define BACKLOG 5
#define MAX_BUF_SIZE 2048

int myreadfd;
int mywritefd;
int nextreadfd;
int connfd;
int nodeindex;

void merge(int * arr, int * part1, int len1, int * part2, int len2) {

	int len = len1 + len2 + 1;
	len1++;
	len2++;

	for(int i = 1, j = 1, k = 1; i < len; ++i) {
		if(j < len1 && k < len2) arr[i] = (part1[j] > part2[k]) ? part2[k++] : part1[j++];
		else if(j == len1) arr[i] = part2[k++];
		else if(k == len2) arr[i] = part1[j++];
	}

	return;
}

void mergesort(int * arr, int count) {

	int len = count + 1;
	if(count <= 1) {
		return;
	}

	int part1[count / 2 + 1];
	int part2[len - count / 2]; // Send to next node

	for(int i = 1; i < count / 2 + 1; ++i) {
		part1[i] = arr[i];
	}
	part1[0] = count / 2;
	for(int i = 1; i < len - count / 2; ++i) {
		part2[i] = arr[i + count / 2];
	}
	part2[0] = count / 2;

	struct sockaddr_in dest;
	socklen_t addrlen = sizeof(dest);
	getsockname(nextreadfd, (struct sockaddr *) &dest, &addrlen);

	printf("CURRENT NODE = %d\tDEST PORT = %d\n", nodeindex, ntohs(dest.sin_port));
	write(mywritefd, part2, sizeof(part2));
	read(mywritefd, part2, sizeof(part2));
	printf("CURRENT NODE = %d\tSOURCE PORT = %d\n", nodeindex, ntohs(dest.sin_port));

	mergesort(part1, count / 2);

	merge(arr, part1, count / 2, part2, len - count / 2 - 1);

	return;
}

int main(int argc, char * argv[]) {

	// printf("Node PID %d\n", getpid());
	if(argc != MAX_ARGS) {
		printf("Invalid Arguments\n");
		close(myreadfd);
		close(mywritefd);
		return 0;
	}

	nodeindex = atoi(argv[1]);
	myreadfd = atoi(argv[2]);
	mywritefd = atoi(argv[3]);
	nextreadfd = atoi(argv[4]);
	int readdata;
	int readarr[MAX_BUF_SIZE];
	int len;
	int count;
	int isroot = 0;
	int merged = 0;
	struct sockaddr_in reqaddr;
	struct sockaddr_in myaddr;
	struct sockaddr_in sendaddr;
	socklen_t addrlen = sizeof(myaddr);

	getsockname(nextreadfd, (struct sockaddr *) &sendaddr, &addrlen);
	connect(mywritefd, (struct sockaddr *) &sendaddr, addrlen);

	// printf("PID = %d, %d, %d, %d, %d\n", getpid(), myreadfd, mywritefd, prevreadfd, nextreadfd);
	// printf("PID = %d, PORT %d Waiting for Connection....\n", getpid(), ntohs(myaddr.sin_port));

	getsockname(myreadfd, (struct sockaddr *) &myaddr, &addrlen);
	connfd = accept(myreadfd, (struct sockaddr *) &reqaddr, &addrlen);
	if(connfd == -1) {
		printf("PORT %d\t", ntohs(myaddr.sin_port));
		perror("accept");
	}
	
	// printf("Accepted FD = %d, Accepted PORT = %d, Node PORT = %d\n", connfd, ntohs(reqaddr.sin_port), ntohs(myaddr.sin_port));

	while(!merged) {

		readdata = read(connfd, readarr, sizeof(readarr));
		printf("CURRENT NODE = %d\tSOURCE PORT = %d\n", nodeindex, ntohs(reqaddr.sin_port));
		len = readdata / sizeof(int);
		count = len - 1;
		int arr[len];
		for(int i = 0; i < len; ++i) arr[i] = readarr[i];
		// for(int i = 0; i < len; ++i) printf("%d ", arr[i]);
		// printf("\n");
	
		if(arr[0] == 0) isroot = 1;
	
		if(isroot || arr[0] == 1) {
			mergesort(arr, count);
			merged = 1;
		} else {
			arr[0]--;
			printf("CURRENT NODE = %d\tDEST PORT = %d\n", nodeindex, ntohs(sendaddr.sin_port));
			write(mywritefd, arr, sizeof(arr));
			read(mywritefd, arr, sizeof(arr));
			printf("CURRENT NODE = %d\tSOURCE PORT = %d\n", nodeindex, ntohs(sendaddr.sin_port));
		}

		// printf("Sending DATA ");
		// for(int i = 1; i < len; ++i) printf("%d ", arr[i]);
		// printf("\n");
		printf("CURRENT NODE = %d\tDEST PORT = %d\n", nodeindex, ntohs(reqaddr.sin_port));
		write(connfd, arr, sizeof(arr));
	}

	close(myreadfd);
	close(mywritefd);

	return 0;
}