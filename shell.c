#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define PROMPT_ARGS 100
#define ARG_MAX 10000
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

extern char ** environ;

char * argcmd[PROMPT_ARGS];
char * pipecmd[PROMPT_ARGS];
char * commacmd[PROMPT_ARGS];
int numargs = 0;
char cmdhist[10][PATH_MAX];
int cmdstat[10];
int ipos = 0, fpos = 0;
int sharedfd;
char tempfile[] = "/tmp/myTmpFile";

//SIGINT Handler
void sigintHandler(int signo, siginfo_t *info, void *ucontext) {
	
	printf("\n");
	int c = 10;
	int i;
	if(fpos >= ipos) c = fpos - ipos;
	if(c > 0) {
		if(c < 10) i = ipos;
		else i = ipos - 1;
		for(; c > 0; --c, i = (i + 1) % 10) {
			printf("%s\tSTATUS = %d\n", cmdhist[i], cmdstat[i]);
		}
	}
	remove(tempfile);
	close(sharedfd);
	exit(0);
}

//SIGQUIT Handler
void sigquitHandler(int signo, siginfo_t *info, void *ucontext) {
	
	while(1){
		printf("\nDo you really want to exit? (y/n)\n");
		char ch;
		scanf("%c", &ch);
		if(ch == 'n' || ch == 'N') return;
		if(ch == 'y' || ch == 'Y') {
			remove(tempfile);
			close(sharedfd);
			exit(0);
		}
	}
}

//Tokenize input string on the basis of pipe operators
void strToken(char * cmdline, char ** argv, char * delims, int * num) {

	//Tokenize on the basis of delims
	argv[0] = strtok(cmdline, delims);
	int i = 0;
	do{
		argv[++i] = strtok(NULL, delims);
	} while(argv[i] != NULL);
	*num = i;

	return;
}

//Tokenize input string into arguments on the basis of whitespace
void argsSpace(char * cmdline, char ** argv, int * numargs) {
	
	char tmparg[ARG_MAX];
	
	//Tokenize the commands into arguments
	argv[0] = strtok(cmdline, " ");
	int i = 1;
	do{
		argv[i] = strtok(NULL, " ");
	} while(argv[i++] != NULL);
	*numargs = i - 1;

	//If the argument contains spaces, then it is escaped using the escape sequence \ and combined
	strncpy(tmparg, "\0", ARG_MAX);
	int j = 1;
	for(i = 1; i < *numargs; ++i) {
		
		if(argv[i][strlen(argv[i]) - 1] == '\\') {
			argv[i][strlen(argv[i]) - 1] = ' ';
			strncat(tmparg, argv[i], ARG_MAX);
		
		} else {
			strncat(tmparg, argv[i], ARG_MAX);
			argv[j] = (char *)malloc(strlen(tmparg) + 1);
			strncpy(argv[j++], tmparg, strlen(tmparg) + 1);
			strncpy(tmparg, "\0", ARG_MAX);
		}
	}
	argv[j] = NULL;
	*numargs = j;

	return;
}

void solver(char ** path, char * pathname, char * cmd1, char ** argv, int filefd, int numcomma, int p, int wstatus) {
	
	argsSpace(cmd1, argcmd, &numargs);
	int numchild;
	int pid = fork();

	if(pid == 0) {

		lseek(filefd, 0, SEEK_SET);
		dup2(filefd, STDOUT_FILENO);

		if(argcmd[0][0] == '.') {
			strncpy(pathname, argcmd[0], strlen(argcmd[0]) + 1);
			execve(pathname, argcmd, NULL);
		} else {
			for(int i = 0; i < p; ++i) {
				strncpy(pathname, path[i], strlen(path[i]) + 1);
				strncat(pathname, "/", 2);
				strncat(pathname, argcmd[0], strlen(argcmd[0]) + 1);
				execv(pathname, argcmd);
			}
		}
		perror("execv");
		close(filefd);
		exit(0);
	} else {
		wait(&wstatus);
		printf("PID = %d\tStatus = %d\n", pid, wstatus);
	}

	int i = 0;
	lseek(filefd, 0, SEEK_SET);
	while(numcomma--) {

		argsSpace(argv[i++], argcmd, &numargs);
		pid = fork();
		if(pid == 0) {

			lseek(filefd, 0, SEEK_SET);
			dup2(filefd, STDIN_FILENO);
				
			if(argcmd[0][0] == '.') {
				strncpy(pathname, argcmd[0], strlen(argcmd[0]) + 1);
				execve(pathname, argcmd, NULL);
			} else {
				for(int i = 0; i < p; ++i) {
					strncpy(pathname, path[i], strlen(path[i]) + 1);
					strncat(pathname, "/", 2);
					strncat(pathname, argcmd[0], strlen(argcmd[0]) + 1);
					execv(pathname, argcmd);
				}
			}
			perror("execv");
			close(filefd);
			exit(0);

		} else {
			while(numchild = wait(&wstatus) > 0) {
				printf("PID = %d\tStatus = %d\n", pid, wstatus);
			}
		}
	}
	return;
}

int main(int argc, char * argv[]) {
	
	//Shell Setup
	printf("Shell Started\n");
	printf("Enter commands\n\n");
	printf("PATH_MAX = %d\tARG_MAX=%d\tPAGE_SIZE=%ld\n\n", PATH_MAX, ARG_MAX, PAGE_SIZE);

	//Masking signals
	sigset_t blockmask, defmask, allmask;
	sigfillset(&allmask);
	sigfillset(&blockmask); //Mask with all signals blocked except SIGINT and SIGQUIT
	sigdelset(&blockmask, SIGINT);
	sigdelset(&blockmask, SIGQUIT);
	sigemptyset(&defmask); //Empty Mask
	sigprocmask(SIG_SETMASK, &blockmask, NULL); //Setting the required signal mask

	//Setting Signal Handlers
	struct sigaction sigintAct, sigquitAct;
	//SIGINT Handler being set
	sigintAct.sa_sigaction = sigintHandler;
	sigintAct.sa_mask = allmask;
	sigintAct.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sigintAct, NULL);
	//SIGQUIT Handler being set
	sigquitAct.sa_sigaction = sigquitHandler;
	sigquitAct.sa_mask = allmask;
	sigquitAct.sa_flags = SA_SIGINFO;
	sigaction(SIGQUIT, &sigquitAct, NULL);

	char cmdline[ARG_MAX]; //max string length accepted on prompt is ARG_MAX
	char cmdlinecpy0[ARG_MAX];
	char cmdlinecpy1[ARG_MAX];
	char cmdlinecpy2[ARG_MAX];
	char cmdlinecpy3[ARG_MAX];
	char tmp;

	char pathname[PATH_MAX]; //Pathname of command/executable to be executed

	char * ptr; //For extracting all the values of the PATH environment variable
	char * ptrcpy;
	int p = 1;
	
	char currwd[PATH_MAX]; //Current working directory
	char prevwd[PATH_MAX]; //Previous working directory
	const char * home = getenv("HOME"); //Stores the home directory from HOME environment variable
	const char * username = getenv("USERNAME"); //Username of current user
	char hostname[255]; //Hostname of machine
	gethostname(hostname, 255);

	//Checking how many values are stored in PATH variable
	ptr = getenv("PATH");
	ptrcpy = (char *)malloc(strlen(ptr) + 1);
	strncpy(ptrcpy, ptr, strlen(ptr) + 1);
	strtok(ptrcpy, ":");
	while(strtok(NULL, ":")) {
		p++;
	}
	
	//Extracting and storing all the values of PATH variable
	char * path[p];
	strncpy(ptrcpy, ptr, strlen(ptr) + 1);
	path[0] = strtok(ptrcpy, ":");
	for(int i = 1; i < p; ++i) {
		path[i] = strtok(NULL, ":");
	}
	free(ptrcpy);

	//Setting the current directory to HOME
	strncpy(currwd, home, PATH_MAX);
	strncpy(prevwd, home, PATH_MAX);
	chdir(currwd);
	
	//For taking in commands
	pid_t pid;
	size_t size;
	int wstatus, status;
	int numpipe1, numpipe2, numpipe3;
	int flag;
	int numchild;
	int fdi, fdo;
	int numcomma;
	
	while(1) {

		//Print the current working directory and prompt
		pid = -1;
		numargs = 0;
		status = 0;
		flag = 1;
		getcwd(currwd, PATH_MAX);
		printf("%s@%s:%s$ ", username, hostname, currwd);
		fflush(stdout);
		
		//Read Commands from prompt
		strncpy(cmdline, "\0", ARG_MAX);
		scanf("%[^\n]s", cmdline);
		scanf("%c", &tmp);
		if(strlen(cmdline) == 0) continue;
		strncpy(cmdlinecpy0, cmdline, ARG_MAX);

		for(int i = 0; i < strlen(cmdlinecpy0) - 2; ++i) {
			if(cmdlinecpy0[i] == '|' && cmdlinecpy0[i + 1] == '|' && cmdlinecpy0[i + 2] == '|') {
				cmdlinecpy0[i] = '@'; //Replace ||| with @
				cmdlinecpy0[i + 1] = ' ';
				cmdlinecpy0[i + 2] = ' ';
			} else if(cmdlinecpy0[i] == '|' && cmdlinecpy0[i + 1] == '|' && cmdlinecpy0[i + 2] != '|') {
				cmdlinecpy0[i] = '#'; //Replace || with #
				cmdlinecpy0[i + 1] = ' ';
			}
		}
		strncpy(cmdlinecpy1, cmdlinecpy0, ARG_MAX);
		strncpy(cmdlinecpy2, cmdlinecpy0, ARG_MAX);
		strncpy(cmdlinecpy3, cmdlinecpy0, ARG_MAX);

		//Check for ||| pipes
		sharedfd = open(tempfile, O_CREAT | O_TRUNC | O_RDWR, 0666);
		lseek(sharedfd, 0, SEEK_SET);

		numpipe3 = 0;
		strToken(cmdlinecpy3, pipecmd, "@", &numpipe3);
		if(numpipe3 == 2) {

			strToken(pipecmd[1], commacmd, ",", &numcomma);
			if(numcomma != 3) {
				printf("Invalid number of arguments. Expected 3\n");
			} else {
				solver(path, pathname, pipecmd[0], commacmd, sharedfd, numcomma, p, wstatus);
			}

			strncpy(cmdhist[fpos], cmdline, PATH_MAX);
			cmdstat[fpos] = wstatus;
			fpos++;
			fpos %= 10;
			if(fpos == ipos) ipos++;

			continue;

		} else if(numpipe3 > 2) {
			printf("Invalid |||\n");
			continue;
		}
		close(sharedfd);

		//Checking for || pipes
		sharedfd = open(tempfile, O_CREAT | O_TRUNC | O_RDWR, 0666);
		lseek(sharedfd, 0, SEEK_SET);

		numpipe2 = 0;
		strToken(cmdlinecpy2, pipecmd, "#", &numpipe2);
		if(numpipe2 == 2) {

			strToken(pipecmd[1], commacmd, ",", &numcomma);
			if(numcomma != 2) {
				printf("Invalid number of arguments. Expected 2\n");
			} else {
				solver(path, pathname, pipecmd[0], commacmd, sharedfd, numcomma, p, wstatus);
			}

			strncpy(cmdhist[fpos], cmdline, PATH_MAX);
			cmdstat[fpos] = wstatus;
			fpos++;
			fpos %= 10;
			if(fpos == ipos) ipos++;

			continue;

		} else if(numpipe2 > 1) {
			printf("Invalid ||\n");
			continue;
		}
		close(sharedfd);

		//Tokenize pipes
		strToken(cmdlinecpy1, pipecmd, "|", &numpipe1);
		int pipefd[numpipe1 - 1][2];

		//Running the pipe commands
		for(int i = 0; i < numpipe1; ++i) {

			argsSpace(pipecmd[i], argcmd, &numargs);

			if(i < numpipe1 - 1) pipe(pipefd[i]);

			flag = 1;
			pid = fork();
			if(pid == 0) {

				sigprocmask(SIG_SETMASK, &defmask, NULL);

				if(!strncmp(argcmd[0], "exit", strlen(argcmd[0]) + 1)) {
					flag = 0;
					break;
				} else if(!strncmp(argcmd[0], "cd", strlen(argcmd[0]) + 1)) {
					flag = 0;
					break;
				}

				if(i > 0) {
					close(pipefd[i - 1][1]);
					dup2(pipefd[i - 1][0], STDIN_FILENO);
				}
				if(i < numpipe1 - 1) {
					close(pipefd[i][0]);
					dup2(pipefd[i][1], STDOUT_FILENO);
				}

				int j = 0;
				for(int i = 0; i < numargs; ++i) {
					if(!strncmp(argcmd[i], "<", strlen(argcmd[i]) + 1)) {
						if(i < numargs - 1) {
							fdi = open(argcmd[++i], O_RDONLY);
							dup2(fdi, STDIN_FILENO);
						}
					} else if(!strncmp(argcmd[i], ">", strlen(argcmd[i]) + 1)) {
						if(i < numargs - 1) {
							fdo = open(argcmd[++i], O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
							dup2(fdo, STDOUT_FILENO);
						}
					} else {
						argcmd[j++] = argcmd[i];
					}
				}
				argcmd[j] = NULL;
				numargs = j;

				//execution of given command
				if(argcmd[0][0] == '.') {
					strncpy(pathname, argcmd[0], strlen(argcmd[0]) + 1);
					execve(pathname, argcmd, NULL);
				} else {
					for(int i = 0; i < p; ++i) {
						strncpy(pathname, path[i], strlen(path[i]) + 1);
						strncat(pathname, "/", 2);
						strncat(pathname, argcmd[0], strlen(argcmd[0]) + 1);
						execv(pathname, argcmd);
					}
				}
				perror("execv");
				flag = 0;
				break;
			
			} else {

				//Only WRITE end of each pipe is closed in the parent process
				//so that the pipe is not destroyed after the child exits and
				//can be used by the next child to read the data. After all data
				//is read, parent and reading child receive EOF close the pipe
				if(i < numpipe1 - 1) {
					close(pipefd[i][1]);
				}

				//Waiting for all child processes to execute
				while(numchild = wait(&wstatus) > 0) {
					if(!strncmp(argcmd[0], "exit", strlen(argcmd[0]) + 1)) {
						flag = -1;
					} else if(!strncmp(argcmd[0], "cd", strlen(argcmd[0]) + 1)) {
						flag = -1;
					} else {
						printf("PID = %d\tStatus = %d\n", pid, wstatus);	
					}
				}
			}
		}

		if(!flag) {

			break;

		} else if(flag == 1) {
			
			//Storing the commmand history and its status
			strncpy(cmdhist[fpos], cmdline, PATH_MAX);
			cmdstat[fpos] = wstatus;
			fpos++;
			fpos %= 10;
			if(fpos == ipos) ipos++;
		
		} else {

			//exit command
			if(!strncmp(argcmd[0], "exit", strlen(argcmd[0]) + 1)) {
				break;
			}

			//cd command
			if(!strncmp(argcmd[0], "cd", strlen(argcmd[0]) + 1)) {
				if(argcmd[1] == NULL) {
					strncpy(prevwd, currwd, PATH_MAX);
					strncpy(currwd, home, PATH_MAX);
				} else if(!strncmp(argcmd[1], "-", strlen(argcmd[1]) + 1)) {
					strncpy(currwd, prevwd, PATH_MAX);
					getcwd(prevwd, PATH_MAX);
				} else {
					strncpy(prevwd, currwd, PATH_MAX);
					strncpy(currwd, argcmd[1], PATH_MAX);
					// printf("Moving to : %s\t%ld\n", args[1], strlen(args[1]));
				}
				if((cmdstat[fpos] = chdir(currwd)) < 0) {
					perror("cd");
				}

				//Storing the command history and its status
				strncpy(cmdhist[fpos], cmdline, PATH_MAX);
				fpos++;
				fpos %= 10;
				if(fpos == ipos) ipos++;
				getcwd(currwd, PATH_MAX);
			}
		}
	}

	remove(tempfile);
	close(sharedfd);

	return 0;
}
