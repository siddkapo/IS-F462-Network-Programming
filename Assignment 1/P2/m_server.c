#include "includefile.h"

int DSNUM = 0; // The Last D Server to which a chunk was added. 0 <= DSNUM < NUMD
int NUMD = 0; // Number of D Servers to be started. NUMD >= REPLNUM
int COUNTER = 1; // For generating unique Chunk IDs
int msqidMD; // MsgQID for M and D Servers

void siginthandler(int signum, siginfo_t * info, void * ucontext) {
	printf("Quitting M Server....\n");
	int msqid = ftok(MSGQ_M_PATH, MSGQ_M_PROJ_ID);
	msgctl(msqid, IPC_RMID, NULL);
	exit(EXIT_SUCCESS);
}

int addfile(int filesize, int chunksize, int * pidD, int * msqidD , char * cmd, struct dir * root, struct file * myfile) {

	if(cmd[0] != '/') return -1; // File Path does not start from root
	if(cmd[strlen(cmd) - 1] == '/') return -1; // Given path is a directory

	int numchunks;
	int numdir;
	char filepath[MSG_SIZE];
	char filepathcpy[MSG_SIZE];
	char * dirname;
	struct dir * currdir = root;
	strncpy(filepath, cmd, MSG_SIZE);
	strncpy(filepathcpy, cmd, MSG_SIZE);

	// Calculating number of directories in path
	strtok(filepath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	char pathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(filepath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(pathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	currdir = currdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(currdir->dirname, pathlist[i], MAX_FILENAME)) {
			currdir = currdir->nextdir;
			if(currdir == NULL) return -1; // Directory in path does NOT exist
		}
		currdir = currdir->subdir;
	}
	struct file * currfile = currdir->files;
	struct file * lastfile;
	while(currfile != NULL) {
		if(!strncmp(pathlist[numdir - 1], currfile->filename, MAX_FILENAME)) return -2; // File Already Exists
		lastfile = currfile;
		currfile = currfile->nextfile;
	}

	int arr[NUMD]; // Number of packets received by each D Server
	for(int i = 0; i < NUMD; ++i) arr[i] = 0;

	// Calculating number of chunks
	numchunks = filesize / chunksize;
	if(filesize % chunksize != 0) numchunks++;

	// Creating new chunks
	struct chunkinfo newchunk[numchunks];
	char chunkname[MAX_CHUNKID];
	for(int i = 0; i < numchunks; ++i) {
		
		// GENERATE UNIQUE CHUNKS IDs
		int c = COUNTER++;
		int k = 0;
		while(c != 0) {
			chunkname[k++] = (c % 10) + 48; // Converting counter to string
			c /= 10;
		}
		for(int j = 0; j < k / 2; ++j) {
			char swp = chunkname[j];
			chunkname[j] = chunkname[k - j - 1];
			chunkname[k - j - 1] = swp;
		}
		chunkname[k] = '\0';
		strncpy(newchunk[i].chunkid, chunkname, MAX_CHUNKID);
		
		// Storing the pid and msqid of the respective D Server in which the chunk replicas will be stored
		for(int j = 0; j < REPLNUM; ++j) {
			newchunk[i].pidreplD[j] = pidD[DSNUM];
			newchunk[i].msqidreplD[j] = msqidD[DSNUM];
			arr[DSNUM]++;
			DSNUM++;
			DSNUM %= NUMD;
		}
		newchunk[i].reference = 1;
	}

	// Adding new file to File Path
	struct file newfile;
	strncpy(newfile.filename, pathlist[numdir - 1], MAX_FILENAME);
	newfile.filesize = filesize;
	newfile.chunksize = chunksize;
	newfile.numchunks = numchunks;
	newfile.parentdir = currdir;
	newfile.info = newchunk;
	newfile.nextfile = NULL;
	lastfile->nextfile = &newfile;
	myfile = &newfile;

	// Sending Number of chunks to be received to each D Server
	struct msgbuf sndmsg;
	sndmsg.mtype = getpid();
	sndmsg.ptype = ADDFILE_CMD;
	sndmsg.msqid = msqidMD;
	sndmsg.mint[0] = chunksize;
	for(int i = 0; i < NUMD; ++i) {
		if(arr[i] == 0) continue;
		sndmsg.mint[1] = arr[i];
		int stat = msgsnd(msqidD[i], &sndmsg, PACKET_SIZE, 0);
		if(stat == -1) {
			printf("Message Send FAILED for D Server PID %d\n", pidD[i]);
			perror("msgsnd");
			return -1;
		}
	}

	return numchunks;
}

int adddir(char * cmd, struct dir * root) {


	if(cmd[0] != '/') return -1; // File Path does not start from root
	if(cmd[strlen(cmd) - 1] != '/') return -1; // Given path is not a directory

	int numdir;
	char filepath[MSG_SIZE];
	char filepathcpy[MSG_SIZE];
	char * dirname;
	struct dir * currdir = root;
	strncpy(filepath, cmd, MSG_SIZE);
	strncpy(filepathcpy, cmd, MSG_SIZE);

	// Calculating number of directories in path
	strtok(filepath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	numdir--;
	char pathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(filepath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(pathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	currdir = currdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(currdir->dirname, pathlist[i], MAX_FILENAME)) {
			currdir = currdir->nextdir;
			if(currdir == NULL) return -1; // Directory in path does NOT exist
		}
		currdir = currdir->subdir;
	}

	struct dir * lastdir;
	while(strncmp(currdir->dirname, pathlist[numdir - 1], MAX_FILENAME)) {
		lastdir = currdir;
		currdir = currdir->nextdir;
	}
	if(currdir != NULL) return -2;

	struct dir newdir;
	lastdir->nextdir = &newdir;
	newdir.nextdir = NULL;
	newdir.subdir = NULL;
	newdir.files = NULL;
	newdir.parentdir = lastdir->parentdir;
	strncpy(newdir.dirname, pathlist[numdir - 1], MAX_FILENAME);

	return 0;
}

int copyfile(int * pidD, int * msqidD, char * src, char * dest, struct dir * root) {

	if(src[0] != '/') return -1; // File Path does not start from root
	if(src[strlen(src) - 1] == '/') return -1; // Given path is a directory
	if(dest[0] != '/') return -1; // File Path does not start from root
	if(dest[strlen(dest) - 1] == '/') return -1; // Given path is a directory

	char srcpath[MSG_SIZE];
	strncpy(srcpath, src, MSG_SIZE);
	char destpath[MSG_SIZE];
	strncpy(destpath, dest, MSG_SIZE);
	struct dir * srcdir = root;
	struct dir * destdir = root;
	int numdir;
	char * dirname;

	// Calculating number of directories in src path
	strtok(srcpath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	char srcpathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(srcpath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(srcpathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	srcdir = srcdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(srcdir->dirname, srcpathlist[i], MAX_FILENAME)) {
			srcdir = srcdir->nextdir;
			if(srcdir == NULL) return -1; // Directory in path does NOT exist
		}
		srcdir = srcdir->subdir;
	}
	struct file * srcfile = srcdir->files;
	struct file * lastfile;
	while(srcfile != NULL) {
		if(!strncmp(srcpathlist[numdir - 1], srcfile->filename, MAX_FILENAME)) break;
		lastfile = srcfile;
		srcfile = srcfile->nextfile;
	}
	if(srcfile == NULL) return -1;

	// Calculating number of directories in dest path
	strtok(destpath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	char destpathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(destpath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(destpathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	struct file newfile;
	destdir = destdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(destdir->dirname, destpathlist[i], MAX_FILENAME)) {
			destdir = destdir->nextdir;
			if(destdir == NULL) return -1; // Directory in path does NOT exist
		}
		destdir = destdir->subdir;
	}
	struct file * destfile = destdir->files;
	while(destfile != NULL) {
		if(!strncmp(destpathlist[numdir - 1], destfile->filename, MAX_FILENAME)) break;
		lastfile = destfile;
		destfile = destfile->nextfile;
	}
	if(destfile == NULL) {
		lastfile->nextfile = &newfile;
		newfile.nextfile = NULL;
		destfile = &newfile;
		strncpy(destfile->filename, destpathlist[numdir - 1], MAX_FILENAME);
	}
	destfile->filesize = srcfile->filesize;
	destfile->chunksize = srcfile->chunksize;
	destfile->numchunks = srcfile->numchunks;
	destfile->info = srcfile->info;
	for(int i = 0; i < srcfile->numchunks; ++i) {
		destfile->info[i].reference++;
	}

	return 0;
}

int movefile(int * pidD, int * msqidD, char * src, char * dest, struct dir * root) {

	if(src[0] != '/') return -1; // File Path does not start from root
	if(src[strlen(src) - 1] == '/') return -1; // Given path is a directory
	if(dest[0] != '/') return -1; // File Path does not start from root
	if(dest[strlen(dest) - 1] == '/') return -1; // Given path is a directory

	char srcpath[MSG_SIZE];
	strncpy(srcpath, src, MSG_SIZE);
	char destpath[MSG_SIZE];
	strncpy(destpath, dest, MSG_SIZE);
	struct dir * srcdir = root;
	struct dir * destdir = root;
	int numdir;
	char * dirname;

	// Calculating number of directories in src path
	strtok(srcpath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	char srcpathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(srcpath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(srcpathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	srcdir = srcdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(srcdir->dirname, srcpathlist[i], MAX_FILENAME)) {
			srcdir = srcdir->nextdir;
			if(srcdir == NULL) return -1; // Directory in path does NOT exist
		}
		srcdir = srcdir->subdir;
	}
	struct file * srcfile = srcdir->files;
	struct file * lastfile;
	while(srcfile != NULL) {
		if(!strncmp(srcpathlist[numdir - 1], srcfile->filename, MAX_FILENAME)) break;
		lastfile = srcfile;
		srcfile = srcfile->nextfile;
	}
	if(srcfile == NULL) return -1;
	lastfile->nextfile = srcfile->nextfile; // Removing Src file from its present directory

	// Calculating number of directories in dest path
	strtok(destpath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	char destpathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(destpath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(destpathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	struct file newfile;
	destdir = destdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(destdir->dirname, destpathlist[i], MAX_FILENAME)) {
			destdir = destdir->nextdir;
			if(destdir == NULL) return -1; // Directory in path does NOT exist
		}
		destdir = destdir->subdir;
	}
	struct file * destfile = destdir->files;
	while(destfile != NULL) {
		if(!strncmp(destpathlist[numdir - 1], destfile->filename, MAX_FILENAME)) break;
		lastfile = destfile;
		destfile = destfile->nextfile;
	}
	if(destfile == NULL) {
		lastfile->nextfile = &newfile;
		newfile.nextfile = NULL;
		destfile = &newfile;
		strncpy(destfile->filename, destpathlist[numdir - 1], MAX_FILENAME);
	}
	destfile->filesize = srcfile->filesize;
	destfile->chunksize = srcfile->chunksize;
	destfile->numchunks = srcfile->numchunks;
	destfile->info = srcfile->info;

	return 0;
}

int removefile(int * pidD, int * msqidD, char * src, struct dir * root) {

	if(src[0] != '/') return -1; // File Path does not start from root
	if(src[strlen(src) - 1] == '/') return -1; // Given path is a directory

	char srcpath[MSG_SIZE];
	strncpy(srcpath, src, MSG_SIZE);
	struct dir * srcdir = root;
	struct dir * destdir = root;
	int numdir;
	char * dirname;

	// Calculating number of directories in src path
	strtok(srcpath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	char srcpathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(srcpath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(srcpathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	srcdir = srcdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(srcdir->dirname, srcpathlist[i], MAX_FILENAME)) {
			srcdir = srcdir->nextdir;
			if(srcdir == NULL) return -1; // Directory in path does NOT exist
		}
		srcdir = srcdir->subdir;
	}
	struct file * srcfile = srcdir->files;
	struct file * lastfile;
	while(srcfile != NULL) {
		if(!strncmp(srcpathlist[numdir - 1], srcfile->filename, MAX_FILENAME)) break;
		lastfile = srcfile;
		srcfile = srcfile->nextfile;
	}
	if(srcfile == NULL) return -1;

	for(int i = 0; i < srcfile->numchunks; ++i) {
		srcfile->info[i].reference--;
	}

	if(srcfile->info[0].reference > 0) return 0;

	struct msgbuf sndmsg;
	sndmsg.mtype = getpid();
	sndmsg.ptype = RM_CMD;
	sndmsg.msqid = msqidMD;
	for(int i = 0; i < srcfile->numchunks; ++i) {
		strncpy(sndmsg.mtext, srcfile->info[i].chunkid, MAX_FILENAME);
		for(int j = 0; j < REPLNUM; ++j) {
			int stat = msgsnd(srcfile->info[i].msqidreplD[j], &sndmsg, PACKET_SIZE, 0);
			if(stat == -1) {
				perror("msgsnd");
				printf("FAILED to send msg to D Server #%d\n", srcfile->info[i].pidreplD[j]);
				return -1;
			}
		}
	}

	return 0;
}

struct file * syscommand(int * pidD, int * msqidD, char * src, struct dir * root) {

	if(src[0] != '/') return NULL; // File Path does not start from root
	if(src[strlen(src) - 1] == '/') return NULL; // Given path is a directory

	char srcpath[MSG_SIZE];
	strncpy(srcpath, src, MSG_SIZE);
	struct dir * srcdir = root;
	struct dir * destdir = root;
	int numdir;
	char * dirname;

	// Calculating number of directories in src path
	strtok(srcpath, "/");
	numdir = 0;
	while(strtok(NULL, "/") != NULL) {
		numdir++;
	}
	char srcpathlist[numdir][MAX_FILENAME];

	// Storing directory names in the path
	strtok(srcpath, "/");
	dirname = strtok(NULL, "/");
	for(int i = 0; i < numdir; ++i) {
		strncpy(srcpathlist[i], dirname, MAX_FILENAME);
		dirname = strtok(NULL, "/");
	}

	// Navigating to directory
	srcdir = srcdir->subdir;
	for(int i = 0; i < numdir - 1; ++i) {
		while(strncmp(srcdir->dirname, srcpathlist[i], MAX_FILENAME)) {
			srcdir = srcdir->nextdir;
			if(srcdir == NULL) return NULL; // Directory in path does NOT exist
		}
		srcdir = srcdir->subdir;
	}
	struct file * srcfile = srcdir->files;
	struct file * lastfile;
	while(srcfile != NULL) {
		if(!strncmp(srcpathlist[numdir - 1], srcfile->filename, MAX_FILENAME)) break;
		lastfile = srcfile;
		srcfile = srcfile->nextfile;
	}
	if(srcfile == NULL) return NULL;

	return srcfile;
}

int main() {

	while (NUMD < REPLNUM) {
		printf("REPLNUM = %d\n", REPLNUM);
		printf("Enter number of D Servers to enable. Number must be >= REPLNUM : ");
		scanf("%d", &NUMD);
	}

	// Setting up M Server Message Queue for CLients
	key_t key = ftok(MSGQ_M_PATH, MSGQ_M_PROJ_ID);
	int msqidMC = msgget(key, IPC_CREAT | 0666);
	if(msqidMC == -1) {
		perror("msgget");
	}

	// Setting up M Server Message Queue for D Servers
	key = ftok(MSGQ_D_PATH, MSGQ_D_PROJ_ID);
	msqidMD = msgget(key, IPC_CREAT | 0666);
	if(msqidMD == -1) {
		perror("msgget");
	}

	// Start D Servers
	int pidD[NUMD];
	int msqidD[NUMD];
	char * arg[3];
	arg[0] = D_SERVER_EXEC;
	arg[2] = NULL;
	for(int i = 0; i < NUMD; ++i) {

		int tmp = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
		printf("MSQID = %d ", tmp);
		char tmpmsqid[10];
		int k = 0;
		while(tmp != 0) {
			tmpmsqid[k++] = (tmp % 10) + 48;
			tmp /= 10;
		}
		for(int j = 0; j < k / 2; ++j) {
			char swp = tmpmsqid[j];
			tmpmsqid[j] = tmpmsqid[k - j - 1];
			tmpmsqid[k - j - 1] = swp;
		}
		tmpmsqid[k] = '\0';
		arg[1] = tmpmsqid;
		printf("%s\n", arg[1]);

		pidD[i] = fork();
		if(pidD[i] == 0) {
			// TODO Exec D_SERVER
			execv(D_SERVER_EXEC, arg);
			perror("execv");
			exit(EXIT_FAILURE);
		}
	}

	// Setting up Signal Handler
	struct sigaction sa;
	sa.sa_sigaction = siginthandler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sa, NULL);

	// Setting up Directory Structure
	printf("Setting up Directory Structure....\n");
	struct dir root;
	strncpy(root.dirname, "/", 2);
	root.parentdir = NULL;
	root.subdir = NULL;
	root.nextdir = NULL;
	root.files = NULL;

	// Receiving messages from clients
	int stat;
	int msqidC;
	int pidC;
	int ptype;
	int flag;
	int chunksize;
	int filesize;
	char cmd[MSG_SIZE];
	char * cmdstat;
	char * src;
	char * dest;
	struct msgbuf recmsg, sndmsg;
	struct file * myfile;

	while(1) {

		sleep(1);

		stat = msgrcv(msqidMC, &recmsg, PACKET_SIZE, 0, 0);
		if(stat == -1) {
			perror("msgrcv");
			continue;
		}
		pidC = recmsg.mtype;
		msqidC = recmsg.msqid;
		ptype = recmsg.ptype;
		printf("Received Message from %d : %d\n", pidC, ptype);

		sndmsg.mtype = getpid();
		sndmsg.ptype = M_STAT;
		sndmsg.msqid = msqidMC;
		sndmsg.mint[0] = 0;
		flag = 1;
		stat = 0;
		printf("Executing Command....\n");
		switch(ptype) {

			case INIT:
				sndmsg.ptype = ACK;
				break;
			case QUIT:
				printf("Client %d has quit\n", pidC);
				flag = 0;
				break;
			case ADDFILE_CMD: // ADDFILE /src/on/system /dest/on/M/Server
				
				cmdstat = strtok(recmsg.mtext, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				strncpy(cmd, cmdstat, MSG_SIZE);
				filesize = recmsg.mint[0];
				chunksize = recmsg.mint[1];
				stat = addfile(filesize, chunksize, pidD, msqidD, cmd, &root, myfile);
				if(stat == -1) {
					printf("File Path Does NOT Exist\n");
				} else if(stat == -2) {
					printf("File Already Exists\n");
					stat = -1;
				} else {
					printf("File Added Successfully\n");
				}

				break;
			case MKDIR_CMD: // MKDIR /dir/on/M/Server/
				
				cmdstat = strtok(recmsg.mtext, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				
				strncpy(cmd, cmdstat, MSG_SIZE);
				stat = adddir(cmd, &root);
				if(stat == -1) {
					printf("File Path Does NOT Exist\n");
				} else if(stat == -2) {
					printf("Directory Already Exists\n");
					stat = -1;
				} else {
					printf("Directory Added Successfully\n");
				}

				break;
			case CP_CMD: // CP /src/on/M/Server /dest/on/M/Server

				cmdstat = strtok(recmsg.mtext, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				strncpy(src, cmdstat, MSG_SIZE);
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				strncpy(dest, cmdstat, MSG_SIZE);
				stat = copyfile(pidD, msqidD, src, dest, &root);
				if(stat == -1) {
					printf("File Path Does NOT Exist\n");
				} else {
					printf("File Copied Successfully\n");
				}

				break;
			case MV_CMD: // MV /src/on/M/Server /dest/on/M/Server
				
				cmdstat = strtok(recmsg.mtext, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				strncpy(src, cmdstat, MSG_SIZE);
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				strncpy(dest, cmdstat, MSG_SIZE);
				stat = movefile(pidD, msqidD, src, dest, &root);
				if(stat == -1) {
					printf("File Path Does NOT Exist\n");
				} else {
					printf("File Moved Successfully\n");
				}

				break;
			case RM_CMD: // RM /file/on/M/Server

				cmdstat = strtok(recmsg.mtext, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				cmdstat = strtok(NULL, " ");
				if(cmdstat == NULL) {
					printf("INVALID Command\n");
					break;
				}
				
				strncpy(cmd, cmdstat, MSG_SIZE);
				stat = removefile(pidD, msqidD, cmd, &root);
				if(stat == -1) {
					printf("File Path Does NOT Exist\n");
				} else {
					printf("File Removed Successfully\n");
				}

				break;
			case SYS_CMD: // <SYS> /file/on/M/Server
				
				cmdstat = strtok(recmsg.mtext, " ");
				char * lastcmd;

				while(cmdstat != NULL) {
					lastcmd = cmdstat;
					cmdstat = strtok(NULL, " ");
				}
				
				strncpy(cmd, lastcmd, MSG_SIZE);
				myfile = syscommand(pidD, msqidD, cmd, &root);
				if(myfile == NULL) {
					printf("File Path Does NOT Exist\n");
					stat = -1;
				} else {
					printf("Success\n");
					stat = myfile->numchunks;
				}

				break;
			default:
				printf("INVALID Packet Type\n");
				stat = -1;
		}

		if(!flag) continue;

		sndmsg.mint[0] = stat;
		int stat = msgsnd(msqidC, &sndmsg, PACKET_SIZE, 0);
		if(stat == -1) {
			perror("msgsnd");
		}

		if(sndmsg.mint[0] < 1) continue;

		switch(ptype) {

			case ADDFILE_CMD:

				for(int i = 0; i < myfile->numchunks; ++i) {
					sndmsg.ptype = ADDFILE_CMD;
					sndmsg.mint[0] = REPLNUM;
					strncpy(sndmsg.mtext, myfile->info[i].chunkid, MAX_CHUNKID);
					for(int j = 1; j <= REPLNUM; ++j) {
						sndmsg.mint[j] = myfile->info[i].msqidreplD[j - 1];
					}
					for(int j = REPLNUM + 1; j <= 2 * REPLNUM; ++j) {
						sndmsg.mint[j] = myfile->info[i].pidreplD[j - REPLNUM - 1];
					}

					stat = msgsnd(msqidC, &sndmsg, PACKET_SIZE, 0);
					if(stat == -1) {
						perror("msgsnd");
						printf("FAILED to send Chunk Info of Chunk #%d\n", i + 1);
					}
				}

				break;
			case SYS_CMD:

				for(int i = 0; i < myfile->numchunks; ++i) {
					sndmsg.ptype = SYS_CMD;
					sndmsg.mint[0] = REPLNUM;
					strncpy(sndmsg.mtext, myfile->info[i].chunkid, MAX_CHUNKID);
					for(int j = 1; j <= REPLNUM; ++j) {
						sndmsg.mint[j] = myfile->info[i].msqidreplD[j - 1];
					}
					for(int j = REPLNUM + 1; j <= 2 * REPLNUM; ++j) {
						sndmsg.mint[j] = myfile->info[i].pidreplD[j - REPLNUM - 1];
					}

					stat = msgsnd(msqidC, &sndmsg, PACKET_SIZE, 0);
					if(stat == -1) {
						perror("msgsnd");
						printf("FAILED to send Chunk Info of Chunk #%d\n", i + 1);
					}
				}

				break;
			default:
				printf("Something went Wrong!!\n");
				continue;

		}

	}

	return 0;
}