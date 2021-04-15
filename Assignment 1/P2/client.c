#include "includefile.h"

int systemcmd(struct msgbuf * recmsg, char * cmd, int msqidC) {

	struct msgbuf msg;
	char cmdcpy[MSG_SIZE];
	strncpy(cmdcpy, cmd, MSG_SIZE);
	strncpy(msg.mtext, "\0", 1);
	char * curr;
	char * prev = NULL;
	curr = strtok(cmdcpy, " ");

	while(curr != NULL) {
		if(prev != NULL) strncat(msg.mtext, prev, MSG_SIZE);
		prev = curr;
		curr = strtok(NULL, " ");
	}
	strncat(msg.mtext, recmsg->mtext, MSG_SIZE);
	msg.mtype = getpid();
	msg.ptype = SYS_CMD;
	msg.msqid = msqidC;
	// msg.mint[0] = msqidC;

	int numD = recmsg->mint[0];
	if(numD == 0) return -1;
	int msqidD = recmsg->mint[1];
	int stat = msgsnd(msqidD, &msg, PACKET_SIZE, 0);
	if(stat == -1) {
		perror("msgsnd");
		return -1;
	}

	int pidD = recmsg->mint[numD + 1];
	stat = msgrcv(msqidC, &msg, PACKET_SIZE, pidD, 0);
	if(stat == -1) {
		perror("msgrcv");
		return -1;
	}

	stat = write(STDOUT_FILENO, msg.mtext, MSG_SIZE);
	if(stat == -1) {
		perror("write");
		return -1;
	}

	return 0;
}

int addchunk(int msqidC, int filefd, int filesize, int CHUNKSIZE, struct msgbuf * recmsg) {

	struct chunkcontent {
		long mtype;
		int ptype;
		int size;
		int msqid;
		char content[CHUNKSIZE + 1];
		char chunkid[MSG_SIZE];
	};

	struct chunkcontent msg;
	msg.mtype = getpid();
	msg.ptype = ADDCHUNK_CMD;
	msg.msqid = msqidC;
	strncpy(msg.chunkid, recmsg->mtext, MSG_SIZE);
	int readsize = read(filefd, msg.content, CHUNKSIZE);
	if(readsize == -1) {
		perror("read");
		printf("Could not read from file FD = %d\n", filefd);
		return -1;
	}
	msg.content[readsize] = '\0';
	msg.size = readsize + 1;

	int numd = recmsg->mint[0];
	int msqidD, pidD;
	for(int i = 1; i <= numd; ++i) {
		msqidD = recmsg->mint[i];
		pidD = recmsg->mint[i + numd];
		int stat = msgsnd(msqidD, &msg, sizeof(struct chunkcontent), 0);
		if(stat == -1) {
			perror("msgsnd");
			printf("FAILED to send content to D Server #%d\n", pidD);
		}
	}

	return 0;
}

int main() {

	int CHUNKSIZE;
	printf("Enter CHUNKSIZE : ");
	scanf("%d", &CHUNKSIZE);

	key_t keyM = ftok(MSGQ_M_PATH, MSGQ_M_PROJ_ID);
	key_t keyC = ftok(IPC_PRIVATE, 1);

	int msqidC = msgget(keyC, IPC_CREAT | 0666);
	int msqidM = msgget(keyM, 0);
	if(msqidM == -1) {
		printf("Server M not running\n");
		perror("msgget");
		msgctl(msqidC, IPC_RMID, NULL);
		exit(EXIT_FAILURE);
	}

	struct msgbuf sndpacket, recpacket;
	int stat;

	// Sending message to M server informing it about Client's MsgQID and CHUNKSIZE
	printf("Sending Client MsgQID and CHUNKSIZE to M server....\n");
	sndpacket.mtype = getpid();
	sndpacket.ptype = INIT;
	sndpacket.msqid = msqidC;
	sndpacket.mint[0] = CHUNKSIZE;
	strncpy(sndpacket.mtext, "\0", 2);
	stat = msgsnd(msqidM, &sndpacket, PACKET_SIZE, 0);
	if(stat == -1) {
		perror("msgsnd");
		msgctl(msqidC, IPC_RMID, NULL);
		exit(EXIT_FAILURE);
	}

	// Receiving Acknowledgement in form of ACK from M Server by getting PID
	printf("Receiving Acknowledgement and PID of M Server....\n");
	stat = msgrcv(msqidC, &recpacket, PACKET_SIZE, 0, 0);
	if(stat == -1) {
		perror("msgrcv");
		msgctl(msqidC, IPC_RMID, NULL);
		exit(EXIT_FAILURE);
	}
	long pidM = recpacket.mtype;

	// sleep(1);

	// Reading commands from command line
	printf("Enter Commands\n\n");
	char cmdline[MSG_SIZE];
	char cmdlinecpy[MSG_SIZE];
	char cmdtoken[MSG_SIZE];
	char * reset = cmdlinecpy;
	int cmdtype;
	int numrec;
	int chunks;
	int filefd;
	int filesize;
	char tmp;
	int * msqidD = (int *)malloc(0);
	pid_t * pidD = (pid_t *)malloc(0);

	scanf("%s", cmdline);

	while(1) {

		sleep(1);

		// Enter Commands
		printf("$ ");
		scanf("%[^\n]s", cmdline);
		scanf("%c", &tmp);
		strncpy(cmdlinecpy, cmdline, MSG_SIZE);
		printf("Command Entered : %s\n", cmdlinecpy);

		strncpy(cmdtoken, strtok(cmdlinecpy, " "), MSG_SIZE);

		if(strlen(cmdline) == 0) continue;
		
		// Copying Command line string into message and sending message to M Server
		strncpy(sndpacket.mtext, cmdline, MSG_SIZE);
		sndpacket.mtype = getpid();
		sndpacket.msqid = msqidC;

		if(!strncmp(cmdtoken, "exit", MSG_SIZE)) { // Does not receive any acknowledgement
			printf("Client Exiting....\n");
			sndpacket.ptype = QUIT;
			stat = msgsnd(msqidM, &sndpacket, PACKET_SIZE, 0);
			if(stat == -1) {
				perror("msgsnd");
				printf("Exit Failed\n");
				continue;
			}
			break;

		} else if(!strncmp(cmdtoken, "ADDFILE", MSG_SIZE)) { // Receives M_STAT signal with >=0 = number of ADDFILE_CMD packets incoming, -1 = FAIL
			sndpacket.ptype = ADDFILE_CMD;
			strncpy(cmdtoken, strtok(NULL, " "), MSG_SIZE);
			filefd = open(cmdtoken, O_RDONLY);
			filesize = lseek(filefd, SEEK_END, 0) - lseek(filefd, SEEK_SET, 0);
			close(filefd);
			sndpacket.mint[0] = filesize;
			sndpacket.mint[1] = CHUNKSIZE;

		} else if(!strncmp(cmdtoken, "RM", MSG_SIZE)) { // Receives M_STAT signal with 0 = SUCCESS, -1 = FAIL
			sndpacket.ptype = RM_CMD;
		} else if(!strncmp(cmdtoken, "CP", MSG_SIZE)) { // Receives M_STAT signal with 0 = SUCCESS, -1 = FAIL
			sndpacket.ptype = CP_CMD;
		} else if(!strncmp(cmdtoken, "MV", MSG_SIZE)) { // Receives M_STAT signal with 0 = SUCCESS, -1 = FAIL
			sndpacket.ptype = MV_CMD;
		} else if(!strncmp(cmdtoken, "MKDIR", MSG_SIZE)) { // Receives M_STAT signal with 0 = SUCCESS, -1 = FAIL
			sndpacket.ptype = MKDIR_CMD;
		} else { // Receives M_STAT signal with >=0 = number of SYS_CMD packets incoming, -1 = FAIL
			sndpacket.ptype = SYS_CMD;
		}

		// Checking and Setting Command Type
		cmdtype = sndpacket.ptype;

		// Sending Message to M Server
		stat = msgsnd(msqidM, &sndpacket, PACKET_SIZE, 0);
		if(stat == -1) {
			perror("msgsnd");
			continue;
		}

		// Receiving Message from M Server
		stat = msgrcv(msqidC, &recpacket, PACKET_SIZE, pidM, 0);
		if(stat == -1) {
			perror("msgrcv");
			continue;
		}

		// Checking Message Type
		if(recpacket.ptype == M_STAT) {
			stat = recpacket.mint[0];
			if(stat == -1) {
				printf("ERROR : Request to M Server Failed\n");
			} else {
				printf("Success %d\n", stat);
			}
		} else {
			printf("Received Incorrect Packet from M Server\n");
			continue;
		}

		// Receive numrec # of packets from M Server. If numrec = 0, Client did not have any messages to receive but operation is SUCCESS
		numrec = stat;
		struct msgbuf recpacketM[numrec];
		for(int i = 1; i <= numrec; ++i) {
			stat = msgrcv(msqidC, &recpacketM[i - 1], PACKET_SIZE, pidM, 0);
			if(stat == -1) {
				perror("msgrcv");
				printf("Failed to receive packet #%d from M Server\n", i);
			}
			stat = recpacketM[i - 1].ptype;
		}

		if(stat == ADDFILE_CMD) {

			strtok(cmdlinecpy, " ");
			strncpy(cmdtoken, strtok(NULL, " "), MSG_SIZE);
			filefd = open(cmdtoken, O_RDONLY);
			if(filefd == -1) {
				perror("open");
				printf("File %s not opened\n", cmdtoken);
				continue;
			}

			lseek(filefd, SEEK_SET, 0);
			for(int i = 0; i < numrec; ++i) {
				stat = addchunk(msqidC, filefd, filesize, CHUNKSIZE, &recpacketM[i]);
				if(stat == -1) {
					printf("ADDCHUNK failed for Chunk #%d\n", i + 1);
				}
			}

			close(filefd);

		} else if(stat == SYS_CMD) {

			for(int i = 0; i < numrec; ++i) {
				stat = systemcmd(&recpacketM[i], cmdline, msqidC);
				if(stat == -1) {
					printf("Command FAILED on D Server #%d\n", i + 1);
					break;
				}
			}

		}
	}

	msgctl(msqidC, IPC_RMID, NULL);
	return 0;
}