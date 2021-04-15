#include "includefile.h"

int main(int argc, char * argv[]) {

	int msqidD = atoi(argv[1]);

	printf("PID = %d MsqidD = %d\n", getpid(), msqidD);

	key_t key = ftok(MSGQ_M_PATH, MSGQ_M_PROJ_ID);
	int msqidM = msgget(key, 0);

	char dirD[MAX_FILENAME];
	int pidD = getpid();

	int k = 0;
	while(pidD != 0) {
		dirD[k++] = (pidD % 10) + 48;
		pidD /= 10;
	}
	for(int j = 0; j < k / 2; ++j) {
		char swp = dirD[j];
		dirD[j] = dirD[k - j - 1];
		dirD[k - j - 1] = swp;
	}
	dirD[k] = '\0';

	int stat = mkdir(dirD, 0777);
	if(stat == -1) {
		perror("mkdir");
		exit(EXIT_FAILURE);
	}
	chdir(dirD);

	struct msgbuf recmsg;
	struct msgbuf sndmsg;

	while(1) {

		sleep(1);

		stat = msgrcv(msqidD, &recmsg, PACKET_SIZE, 0, 0);
		if(stat == -1) {
			perror("msgrcv");
			continue;
		}

		switch(recmsg.ptype) {

			case ADDFILE_CMD:
				break;
			case ADDCHUNK_CMD:
				break;
			case RM_CMD:
				break;
			case SYS_CMD:
				break;
			default:
				break;
		}

	}

	return 0;
}