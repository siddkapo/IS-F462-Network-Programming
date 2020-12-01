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
#define NUM_ARGS 6
#define MAX_STR_LEN 10
#define BACKLOG 5
#define MAX_BUF_SIZE 8192

typedef struct node {
	int data;
	struct node * next;
} node;

// args --> ./node, node index, curr read socket, curr write socket, next read socket

void toString(int n, char * str) {
	free(str);
	int i = 0, a = n, d = 0;
	
	while(a > 0) {
		a /= 10;
		d++;
	}
	if(d == 0) {
		str = (char *)malloc(1);
		str[0] = '0';
		return;
	}
	
	str = (char *)malloc(d);
	for(int i = 0; i < d; ++i) {
		str[d - i - 1] = 48 + n % 10;
		n /= 10;
	}

	return;
}

int main(int argc, char * argv[]) {

	if(argc != 2) {
		printf("Invalid Arguments\n");
		return 0;
	}
	int rootnode = atoi(argv[1]);

	int inputfd = open("data.txt", O_RDONLY);
	int outputfd = creat("output.txt", 0666);

	dup2(inputfd, STDIN_FILENO);

	char data[MAX_NUM_LEN];
	int count = 0;

	node * head = NULL;
	node * last = NULL;
	node * tmp;
	node * prev;

	while(scanf("%s", data) > 0) {
		tmp = (node *)malloc(sizeof(node));
		tmp->data = atoi(data);
		tmp->next = NULL;
		if(head == NULL) head = tmp;
		if(last != NULL) last->next = tmp;
		last = tmp;
		count++;
	}

	last = head;
	int arr[count + 1];
	// printf("INPUT DATA SIZE = %ld\n", sizeof(arr));
	int i = 1;
	while(last != NULL) {
		prev = last;
		arr[i++] = last->data;
		// printf("%d\n", arr[i - 1]);
		last = last->next;
		free(prev);
	}
	arr[0] = 0;

	int readsockfd[count];
	int writesockfd[count];
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	for(int i = 0; i < count; ++i) {
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(0);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		readsockfd[i] = socket(AF_INET, SOCK_STREAM, 0);
		writesockfd[i] = socket(AF_INET, SOCK_STREAM, 0);
		bind(readsockfd[i], (struct sockaddr *) &addr, sizeof(addr));
		getsockname(readsockfd[i], (struct sockaddr *) &addr, &addrlen);
		// printf("Read = %d, Write = %d", readsockfd[i], writesockfd[i]);
		// printf(", Read PORT %d\n", ntohs(addr.sin_port));
		listen(readsockfd[i], BACKLOG);
	}

	int pid;
	int childpid[count];
	char * args[NUM_ARGS];
	args[0] = "./node";
	args[5] = NULL;
	for(int i = 0; i < count; ++i) {
		
		args[1] = (char *)malloc(0);
		args[2] = (char *)malloc(0);
		args[3] = (char *)malloc(0);
		args[4] = (char *)malloc(0);
		
		toString(i + 1, args[1]);
		toString(readsockfd[i], args[2]);
		toString(writesockfd[i], args[3]);
		toString(readsockfd[(i + 1) % count], args[4]);
		
		// printf("i = %d, %s, %s, %s, %s\n", i, args[1], args[2], args[3], args[4]);
		
		pid = fork();
		if(pid == 0) {
			execv("./node", args);
			perror("execv");
			exit(-1);
		}
		
		childpid[i] = pid;
		free(args[1]);
		free(args[2]);
		free(args[3]);
		free(args[4]);
		free(args[5]);
	}

	struct sockaddr_in rootaddr, coordaddr;
	memset(&rootaddr, 0, sizeof(rootaddr));
	rootaddr.sin_family = AF_INET;
	rootaddr.sin_port = htons(0);
	rootaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	addrlen = sizeof(rootaddr);
	
	char * dst;
	int coordsocket = socket(AF_INET, SOCK_STREAM, 0);
	
	getsockname(readsockfd[rootnode], (struct sockaddr *) &rootaddr, &addrlen);
	// printf("Root Node PORT = %d\n", ntohs(rootaddr.sin_port));
	
	connect(coordsocket, (struct sockaddr *) &rootaddr, addrlen);
	getsockname(coordsocket, (struct sockaddr *) &coordaddr, &addrlen);
	// inet_ntop(AF_INET, &coordaddr.sin_addr, dst, addrlen);
	printf("Coordinator PORT = %d\n", ntohs(coordaddr.sin_port));
	// inet_ntop(AF_INET, &rootaddr.sin_addr, dst, addrlen);
	printf("Root Node PORT = %d\n", ntohs(rootaddr.sin_port));

	write(coordsocket, arr, sizeof(arr));

	dup2(outputfd, STDOUT_FILENO);

	int sortedarr[count + 1];
	int readdata = read(coordsocket, sortedarr, sizeof(sortedarr));
	// printf("OUTPUT DATA SIZE = %d\n", readdata);
	for(int i = 1; i < (readdata / sizeof(int)); ++i) printf("%d ", sortedarr[i]);
	
	close(coordsocket);
	close(inputfd);
	close(outputfd);

	while(wait(NULL) > 0);
	
	return 0;
}