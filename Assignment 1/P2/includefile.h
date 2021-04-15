#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/msg.h>
// #include <openssl/sha.h>

#define MSGQ_M_PATH "./m_server.c"
#define MSGQ_M_PROJ_ID 'M'
#define MSGQ_D_PATH "./d_server.c"
#define MSGQ_D_PROJ_ID 'D'
#define D_SERVER_EXEC "./d_server"
#define MSG_SIZE 1024
#define MAX_FILENAME 100
#define MAX_CHUNKID 20
#define REPLNUM 3

// Defining Packet Types
#define INIT 1
#define QUIT 2
#define ACK 3
#define D_INFO 4
#define M_STAT 5
#define M_RES 6
#define D_STAT 7
#define D_RES 8
#define ADDFILE_CMD 9 // ADDFILE /src/on/system /dest/on/M/Server
#define ADDCHUNK_CMD 10 // chunkname
#define MKDIR_CMD 11 // MKDIR /dir/on/M/Server/
#define CP_CMD 13 // CP /src/on/M/Server /dest/on/M/Server
#define MV_CMD 14 // MV /src/on/M/Server /dest/on/M/Server
#define RM_CMD 15 // RM /file/on/M/Server
#define SYS_CMD 17 // <SYS> /file/on/M/Server

struct msgbuf {
	long mtype;
	int ptype;
	int msqid;
	int mint[MSG_SIZE];
	char mtext[MSG_SIZE];
};

struct chunkinfo {
	char chunkid[MAX_CHUNKID];
	int pidreplD[REPLNUM];
	int msqidreplD[REPLNUM];
	int reference;
};

struct file {
	char filename[MAX_FILENAME];
	int filesize;
	int chunksize;
	int numchunks;
	struct dir * parentdir;
	struct chunkinfo * info;
	struct file * nextfile;
};

struct dir {
	char dirname[MAX_FILENAME];
	struct dir * parentdir;
	struct dir * subdir;
	struct dir * nextdir;
	struct file * files;
};

const size_t PACKET_SIZE = sizeof(struct msgbuf); // - sizeof(long);