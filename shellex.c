/* $begin shellmain */
#include "csapp.h"
#include <errno.h>
#define MAXARGS 128
#define MAXJOBS 100

/*job status*/
#define RUNNING 1
#define STOPPED 0
#define COMPLETED 2

struct JOB{			// background job list sturcture
	int exist;
	int status;
	pid_t pid;
	char command[MAXLINE];
	char argv0[MAXLINE];
}job_list[MAXJOBS];		

int pid_n;					// number of background job
pid_t for_pid;				// foreground process id
char fp_command[MAXLINE];	// foreground process command

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv); 
int run_cd(char**argv);
int run_jobs(char**argv);
int run_kill(char**argv);
int run_bg(char**argv);
int run_fg(char**argv);
int piped_execute(char**argv, int*fd_r, int*fd_w);
void job_init(void);
void update_pidn(void);

// SIGINT handler
void sigint_handler(int s){	
	if(for_pid != 0)			// if having foreground child, terminate child
		kill(for_pid, SIGKILL);
	else						// else exit
		exit(0);
	for(int i = 0;i<pid_n;i++){				// erase job list if terminated child exist
		if(job_list[i].pid == for_pid){
			job_list[i].exist = 0;
		}
	}
	for_pid = 0;
	return;
}

// SIGTSTP handler
void sigtstp_handler(int s){
	if(for_pid != 0)				// if having foreground child, stop child
		kill(for_pid, SIGTSTP);
	else{							// else stop self
		kill(getpid(), SIGSTOP);
		return;
	}

	for(int i = 0;i < pid_n; i++){			// change job list status
		if(!job_list[i].pid == for_pid){
			job_list[i].status = STOPPED;
			fprintf(stdout, "[%d]  Stopped\t\t\t%s", i+1, job_list[i].command);
			for_pid = 0;
			return;
		}
	}
	job_list[pid_n].exist = 1;				// if not exist, add job list
	job_list[pid_n].status = STOPPED;
	job_list[pid_n].pid = for_pid;
	strcpy(job_list[pid_n].command, fp_command);
	strcpy(job_list[pid_n].argv0, fp_command);
	fprintf(stdout, "[%d]  Stopped\t\t\t%s", ++pid_n, fp_command);
	for_pid = 0;
	return;
}

// check if child terminated
void check_child(){
	pid_t pid;
	int child_status;
	for (int i = 0; i < pid_n; i++){
		if (!job_list[i].exist)
			continue;
		if (waitpid(job_list[i].pid, &child_status, WNOHANG != 0)){		// check if child terminated
			if (WIFSIGNALED(child_status))		// if child terminated, erase job list
				fprintf(stdout, "[%d] Terminated\t\t%s", i+1, job_list[i].command);
			else
				fprintf(stdout, "[%d] Done\t\t\t%s", i+1, job_list[i].command);
			job_list[i].exist = 0;
		}
	}
	return;
}

/* main */
int main() 
{
    char cmdline[MAXLINE]; /* Command line */
	job_init();								// init variables
	Signal(SIGINT, sigint_handler);			// set sigint_handler
	Signal(SIGTSTP, sigtstp_handler);		// set sigtstp_handler
    while (1) {
	/* Read */
	fprintf(stdout, "CSE4100-SP-P4> ");                   
	fgets(cmdline, MAXLINE, stdin);
	if (feof(stdin))
	    exit(0);

	/* Evaluate */
	eval(cmdline);
	check_child();
    } 
}
/* $end shellmain */
 
/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
	int child_status;	 /* child status */
    pid_t pid;           /* Process id */
	int i = 0;
	char *pargv[MAXARGS];/* Argument list piped_execute() */
	int fd[MAXARGS][2];	 /* fd for pipe */
	int pip_n = 0;		 /* number of pipe */
	int k;
	char command[MAXLINE];/* tmp string for errer msg */
    
    strcpy(buf, cmdline);
    bg = parseline(buf, argv);	
	
	if (argv[0] == NULL){
		return;   /* Ignore empty lines */
	}
	while(argv[i] != NULL){				// piped check
		for (k = 0; 1; k++, i++){		
			if (argv[i] == NULL){
				pargv[k] = NULL;
				break;
			}
			else if(!strcmp(argv[i], "|")){		// if piped 
				pargv[k] = NULL;
				if (pipe(fd[pip_n]) < 0)		// make pipe
					exit(0);
				if (pip_n == 0)					// execute one instruction
					piped_execute(pargv, NULL, fd[pip_n]);
				else
					piped_execute(pargv, fd[pip_n-1], fd[pip_n]);
				pip_n++;
				i++;
				break;
			}
			else
				pargv[k] = argv[i];
		}
	}
	
	if(pip_n != 0){			// if piped get argv for last execute
		for (k = 0; pargv[k] != NULL; k++){
			argv[k] = pargv[k];
		}
		argv[k] = NULL;
		argv[k+1] = NULL;
	}

    if (!builtin_command(argv)) { 	//quit -> exit(0), & -> ignore, other -> run
		pid = Fork();				//fork
		if (!pid){					// child process
			if (!strcmp(argv[0], "jobs")){
				run_jobs(argv);
				exit(0);
			}
			if (pip_n != 0){		// if piped get fd[0] as stdin
				Close(fd[pip_n-1][1]);
				if(fd[pip_n-1][0] != 0){
					Dup2(fd[pip_n-1][0], 0);
					Close(fd[pip_n-1][0]);
				}
			}
			strcpy(command, argv[0]);
       		if (execvpe(argv[0], argv, environ) < 0) {		// execute
           		fprintf(stderr, "%s: %s\n", command, strerror(errno));
           		exit(0);
        	}	
			exit(0);
		}
		else{
			if(pip_n>0){	// close fd
				Close(fd[pip_n-1][1]);
				Close(fd[pip_n-1][0]);
			}
			if(!bg){		// if not background, wait for child terminated or changed
				for_pid = pid;
				strcpy(fp_command, cmdline);
				Waitpid(pid, &child_status, WUNTRACED);
				check_child();		// check if child terminated
				for_pid = 0;
			}
			else{			// if background, add job_list and continue process
				job_list[pid_n].exist = 1;
				job_list[pid_n].pid = pid;
				job_list[pid_n].status = 1;
				strcpy(job_list[pid_n].command, cmdline);
				strcpy(job_list[pid_n].argv0, argv[0]); 
				fprintf(stdout, "[%d] %d\n", ++pid_n, pid);
			}
		}
    }
	//check_child();	// check if child terminated
	update_pidn();	// check if job list number == 0
    return;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv){
	int child_status;
    if (!strcmp(argv[0], "exit")) /* quit command */
		exit(0);
    if (!strcmp(argv[0], "&"))    /* Ignore singleton & */
		return 1;
	check_child();				// check if child terminated
	if (!strcmp(argv[0], "cd"))	// cd run
		return run_cd(argv);
	if (!strcmp(argv[0], "bg"))		// bg run
		return run_bg(argv);
	if (!strcmp(argv[0], "fg"))		// fg run
		return run_fg(argv);
	if (!strcmp(argv[0], "kill"))	// kill run
		return run_kill(argv);

    return 0;                     /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) 
{
    char *delim;         /* Points to first space delimiter */
    int argc;            /* Number of args */
    int bg;              /* Background job? */
	int arglen;

    buf[strlen(buf)-1] = ' ';  /* Replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* Ignore leading spaces */
		buf++;

    /* Build the argv list */
    argc = 0;
    while ((delim = strchr(buf, ' '))) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* Ignore spaces */
            buf++;
    }

    argv[argc] = NULL;
    
    if (argc == 0)  /* Ignore blank line */
		return 1;

    /* Should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0)
		argv[--argc] = NULL;
	else{
		arglen = strlen(argv[argc-1]);
		if(argv[argc-1][arglen-1] == '&'){
			argv[argc-1][arglen-1] = '\0';
			bg = 1;
		}
	}

    return bg;
}
/* $end parseline */

/* action of 'cd'. Change direction with chdir */
int run_cd(char**argv){
	if (argv[1] == NULL){
		if(chdir(getenv("HOME")))		// change direction to 'HOME'
			fprintf(stderr, "cd: %s\n", strerror(errno));
	}
	else if(argv[2] == NULL){			// if there is an arg
		if(chdir(argv[1]))				// change direction to arg
			fprintf(stderr, "cd: %s: %s\n", argv[1], strerror(errno));
	}
	else{								// too many args error
		fprintf(stderr, "USAGE: cd [dir]\n");
	}
	return 1;
}

/* execute one instruction in piped */
int piped_execute(char**argv, int* fd_r, int* fd_w){
	int child_status;
    pid_t pid;           /* Process id */
	pid = Fork();		// fork
	if (!pid){
		if (fd_r != NULL){		// if there is piped input
			Close(fd_r[1]);
			if(fd_r[0] != 0){	// make stdin piped input
				Dup2(fd_r[0], 0);
				Close(fd_r[0]);
			}
		}
		Close(fd_w[0]);
		if(fd_w[1] != 1){		// make stdout piped output
			Dup2(fd_w[1], 1);
			Close(fd_w[1]);
		}
		
		if (execvpe(argv[0], argv, environ) < 0) {		//execute
			fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
			exit(0);
		}
		exit(0);
	}
	else{
		if(fd_r!=NULL){	// close fd
			Close(fd_r[0]);
			Close(fd_r[1]);
		}
		Waitpid(pid, &child_status, WUNTRACED); 	// wait for child process
    }
	return 1;
}

/* string to process number */
int getpid_from_arg(char** argv){
	int i = 0;
	int job_n = 0;
	
	if (argv[1] == NULL){
		fprintf(stderr, "%s: need one arguments\n", argv[0]);
		return -1;
	}
	if (argv[1][0] != '%'){		// process number starts with '%'
		fprintf(stderr, "%s: %s: cannot find such job\n", argv[0], argv[1]);
		return -1;
	}
	for (i = 1;argv[1][i] != '\0';i++){		// string to integer
		if (argv[1][i] < '0' || argv[1][i] > '9'){
			fprintf(stderr, "%s: %s: cannot find such job\n", argv[0], argv[1]);
			return -1;
		}
		job_n *= 10;
		job_n += argv[1][i] - '0';
	}
	return job_n - 1;
}

/* action of 'bg'. kill SIGCONT to child */
int run_bg(char**argv){
	int job_n;
	job_n = getpid_from_arg(argv);	// get process id
	if (job_n < 0)
		return 1;
	if (!job_list[job_n].exist){	// find process id in job list
		fprintf(stderr, "bg: %s: cannot find such job\n", argv[1]);
	   	return 1;	
	}
	job_list[job_n].status = RUNNING;	// update job list
	if(kill(job_list[job_n].pid, SIGCONT) < 0)	// push SIGCONT to child
		fprintf(stderr, "bg: %s\n", strerror(errno));
	return 1;
}

/* action of 'fg'. kill SIGCONT to child and wait */
int run_fg(char**argv){
	int job_n;
	int child_status;
	job_n = getpid_from_arg(argv); // get process id

	if (job_n < 0)
		return 1;
	if (!job_list[job_n].exist){	// find process id in job list
		fprintf(stderr, "fg: %s: cannot find such job\n", argv[1]);
		return 1;
	}
	
	if(kill(job_list[job_n].pid, SIGCONT) < 0)	// push SIGCONT to child
		fprintf(stderr, "fg: %s\n", strerror(errno));
	fprintf(stdout, "%s\n", job_list[job_n].argv0);
	for_pid = job_list[job_n].pid;				// update job list and foreground process
	strcpy(fp_command, job_list[job_n].argv0);
	strcat(fp_command, "\n\0");
	job_list[job_n].status = RUNNING;
	Waitpid(job_list[job_n].pid, &child_status, WUNTRACED);	// wait for process terminated or changed
	job_list[job_n].exist = 0;
	return 1;
}

/* action of 'kill'. kill SIGINT to child */
int run_kill(char**argv){
	int job_n;
   	job_n = getpid_from_arg(argv);	// get process id

	if(job_n < 0)
		return 1;
	if (kill(job_list[job_n].pid, SIGINT) < 0)		// push SIGINT to child
		fprintf(stderr, "kill: %s\n", strerror(errno));
	return 1;
}

/* action of 'kill'. print job list */
int run_jobs(char**argv){
	char c[MAXLINE];
	for(int i=0;i<pid_n;i++){
		if(job_list[i].exist != 1)
			continue;
		if(job_list[i].status == RUNNING)
			strcpy(c, "Running");
		else if(job_list[i].status == STOPPED)
			strcpy(c, "Stopped");
		fprintf(stdout, "[%d]  %s\t\t\t%s", i+1, c, job_list[i].command);
	}
	return 1;
}

/* init variables */
void job_init(){
	pid_n = 0;
	for_pid = 0;
	for(int i = 0; i < MAXJOBS;i++){
		job_list[i].exist = 0;
	}
}

/* check job list number 0*/
void update_pidn(){
	for(int i=0; i< pid_n; i++){
		if(job_list[i].exist)
			return;
	}
	pid_n = 0;		// if number of job list 0, init
}
