/* 
 * tsh - A tiny shell program with job control
 * 
 * < bohao >
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "parser.h"

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */
#define MAXPATH    1024   /* max path length */
#define SHOW_LEN     50   /* max show length of var length */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
int alias_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);
void redirecting(char **argv);
void do_pwd(char **argv);
void do_cd(char **argv);
void do_environ();
void runcmd(struct cmd *cmd);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parse_line(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/* helper funcs */
int find_arg(char** argv, char* str);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
      switch (c) {
        case 'h':             /* print help message */
          usage();
          break;
        case 'v':             /* emit additional diagnostic info */
          verbose = 1;
          break;
        case 'p':             /* don't print a prompt */
          emit_prompt = 0;  /* handy for automatic testing */
          break;
        default:
          usage();
      }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

      /* Read command line */
      if (emit_prompt) {
        printf("%s", prompt);
        fflush(stdout);
      }
      if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
        app_error("fgets error");
      if (feof(stdin)) { /* End of file (ctrl-d) */
        fflush(stdout);
        exit(0);
      }

      /* Evaluate the command line */
      eval(cmdline);
      fflush(stdout);
      fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
  char *argv[MAXARGS]; 
  pid_t pid;
  int bg;
  struct cmd *command;
  sigset_t newMask, oldMask;
  sigemptyset(&newMask);
  sigaddset(&newMask, SIGCHLD);
  sigaddset(&newMask, SIGINT);
  sigaddset(&newMask, SIGTSTP);

  get_tokens(cmdline, argv);
  bg = is_background(argv);
  if (argv[0] == NULL) {
    return;
  }

  // not builtin in
  if (builtin_cmd(argv) == 0 && alias_cmd(argv) == 0) {
    /**
     * block SIGCHLD, SIGINT, SIGTSTP
     * to prevent race conditions of jobs. 
     * for example: the sigchld handler triggered before addjobs.
     */
    sigprocmask(SIG_SETMASK, &newMask, &oldMask);
    pid = fork();  
    if (pid < 0) {
      exit(-1);
    }
    if (pid > 0) {

      if(bg == 0) {
        addjob(&jobs[0], pid, FG, cmdline);
        sigprocmask(SIG_SETMASK, &oldMask, NULL);
        waitfg(pid);
      } else {
        printf("[%d] (%d) %s", nextjid, pid, cmdline);
        addjob(&jobs[0], pid, BG, cmdline);
        sigprocmask(SIG_SETMASK, &oldMask, NULL);
      }
      /* 
       * free argv's dynamic allocated memory
       */
      token_clear(argv);
    }
    else {
      setpgid(0, 0); // send SIGINT to the foreground job
      sigprocmask(SIG_SETMASK, &oldMask, NULL);
      //printf("ve:%s %s\n", argv[0], argv[1]);

      command = parsecmd(argv);
      runcmd(command);
      
    }
  }
  return;
}

void runcmd(struct cmd *cmd) {
  int p[2], r, pr;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0) {
    exit(0);
  }

  switch (cmd->type) {
    default:
      fprintf(stderr, "unknown runcmd\n");
      exit(-1);
    case ' ':
      ecmd = (struct execcmd *)cmd;
      if(ecmd->argv[0] == 0) {
        exit(0);
      }
 
      //printf("d cmd:%s arg:%s\n", ecmd->argv[0], ecmd->argv[1]);
      r = execvp(ecmd->argv[0], ecmd->argv);
      //r = execve(ecmd->argv[0], ecmd->argv, environ);
      if (r < 0) {
        // filename not found
        printf("command %s not found\n", ecmd->argv[0]);
        exit(0);
      }
      break;
    case '<':
    case '>':
      rcmd = (struct redircmd *)cmd;
      //printf("D %s mode=%d fd=%d\n", rcmd->file, rcmd->mode, rcmd->fd);
      close(rcmd->fd);
      open(rcmd->file, rcmd->mode,
           S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
      runcmd(rcmd->cmd);
      break;

    case '|':
      pcmd = (struct pipecmd *)cmd;
      r = pipe(p);
      if (r == -1) {
        fprintf(stderr, "pipe error\n"); 
        exit(-1);
      }

      r = fork();
      // ....
      if (r < 0) {exit(-1);}

      if (r == 0) {
        close(0);
        pr = dup(p[0]);
        if (pr != 0) { 
          exit(-1); 
        }
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->right);
      } else {
        close(1);
        pr = dup(p[1]);
        if (pr != 1) {
          exit(-1);
        }
        close(p[0]);
        close(p[1]);
        runcmd(pcmd->left);
      }
      break;
  }
  // useless
  exit(0);
}

/* 
 * parse_line - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parse_line(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
      buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
      buf++;
      delim = strchr(buf, '\'');
    }
    else {
      delim = strchr(buf, ' ');
    }

    while (delim) {
      argv[argc++] = buf;
      *delim = '\0';
      buf = delim + 1;
      while (*buf && (*buf == ' ')) /* ignore spaces */
        buf++;

      if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
      }
      else {
        delim = strchr(buf, ' ');
      }
    }
    argv[argc] = NULL;

    if (argc == 0)  /* ignore blank line */
      return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
      argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
  if (strcmp(argv[0], "quit") == 0) {
    // just exit
    fflush(stdout);
    exit(0);
  } else if (strcmp(argv[0], "jobs") == 0) {
    listjobs(&jobs[0]);
    return 1;
  } else if (strcmp(argv[0], "bg") == 0) {
    do_bgfg(argv);
    return 1;
  } else if (strcmp(argv[0], "fg") == 0) {
    do_bgfg(argv);
    return 1;
  } else if (strcmp(argv[0], "pwd") == 0) {
    do_pwd(argv);
    return 1;
  } else if (strcmp(argv[0], "cd") == 0) {
    do_cd(argv);
    return 1;
  } else if (strcmp(argv[0], "environ") == 0) {
    do_environ();
    return 1;
  }
  return 0;     /* not a builtin command */
}

/**
 * alias - short names for commands
 */
int alias_cmd(char **argv) {
  if (strcmp(argv[0], "clr") == 0) {
    // don't strcpy argv[0] because argv[i] points to a buffer
    argv[0] = "/usr/bin/clear";
  } else if (strcmp(argv[0], "dir") == 0) {
    argv[0] = "/bin/ls";
  }
  return 0;
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
  int x;
  struct job_t *job;

  if (argv[1] == NULL) {
      printf("%s command requires PID or %%jobid argument\n", argv[0]);
      return;
  } 

  /* error handling */
  if (argv[1][0] == '%') { /* fg|bg %jobid */
    x = atoi(argv[1] + 1);
    job = getjobjid(&jobs[0], x);
    if (job == NULL) {
      printf("%%%d: No such job\n", x);
      return;
    }
  } else if ( isdigit(argv[1][0]) ) { /* bg|fg PID */
    x = atoi(argv[1]);
    job = getjobpid(&jobs[0], x);
    if (job == NULL) {
      printf("(%d): No such process\n", x);
      return;
    }
  } else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  if (strcmp(argv[0], "bg") == 0) {
    job->state = BG;
    printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);

    kill(job->pid, SIGCONT);
  } else {
    job->state = FG;
    // send SIGCONT to all foreground processes
    kill(-job->pid, SIGCONT);
    waitfg(job->pid);
  }

  return;
}


/**
 * find str in argv, if failed return -1
 */
int find_arg(char** argv, char* str) {
  int i = 0;
  while (argv[i] != NULL) {
    if (strcmp(argv[i], str) == 0)
      return i;
    i++;
  }
  return -1;
}

/**
 * redirecting IO
 * because the parse_commandline is very poor,
 * "<", ">" should be placed at the end of command
 * like: echo hello < gaga
 */
void redirecting(char **argv){ 
  int inIndex = find_arg(argv, "<");
  int outIndex = find_arg(argv, ">");
  int appIndex = find_arg(argv, ">>");

  if (inIndex >= 0) {
    int in = open(argv[inIndex + 1], O_RDONLY);
    if (in < 0) {
      printf("File %s not exist\n", argv[inIndex + 1]);
      exit(-1);
    }
    dup2(in, STDIN_FILENO);
    close(in);
    argv[inIndex] = NULL;
  }
  if (outIndex >= 0) {
    // O_TRUNC: remove previous results
    int out = open(argv[outIndex + 1], O_WRONLY | O_TRUNC);
    if (out < 0) {
      printf("File %s not exist\n", argv[outIndex + 1]);
      exit(-1);
    }
    dup2(out, STDOUT_FILENO);
    close(out);
    argv[outIndex] = NULL;
  }
  if (appIndex >= 0) {
    int ad = open(argv[appIndex + 1], O_RDWR | O_APPEND);
    if (ad < 0) {
      printf("File %s not exist!\n", argv[appIndex + 1]);
      exit(-1);
    }
    dup2(ad, STDOUT_FILENO);
    close(ad);
    argv[appIndex] = NULL;
  }
}

/**
 * pwd - show the current working directory.
 */
void do_pwd(char **argv) {
  char ptr[MAXPATH];
  if (getcwd(ptr, sizeof(ptr)) != NULL)
    printf("%s\n", ptr);
}

/**
 * cd - change direcotry
 * change the PWD environment variable
 */
void do_cd(char **argv){
  char ptr[MAXPATH];
  int ret = chdir(argv[1]);
  if (ret != 0) {
    printf("cd: %s: the directory not found.\n", argv[1]);
  } else {
    setenv("PWD", getcwd(ptr, sizeof(ptr)), 1);
    printf("%s\n", getenv("PWD"));
  }
}

/**
 * environ - list all environments
 * if the var's length greater than SHOW_LEN, 
 * only show first SHOW_LEN chars..
 */
void do_environ(){
  char subbuff[MAXLINE];
  int i = 0;
  while(environ[i] != NULL) {
    int len = strlen(environ[i]) ; 
    //printf("[%d]%s\n", len, environ[i]);
    if (len > SHOW_LEN) {
      strncpy(subbuff, environ[i], SHOW_LEN);
      subbuff[SHOW_LEN] = '.';
      subbuff[SHOW_LEN + 1] = '.';
      subbuff[SHOW_LEN + 2] = '.';
      subbuff[SHOW_LEN + 3] = '\0';
      printf("%s\n", subbuff);
    } else {
      printf("%s\n", environ[i]);
    }
    i++; 
  }
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
  while ( fgpid(&jobs[0]) == pid ) {
    sleep(1);
  }
  //printf("waitfg ok\n");
  return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
  // do not wait for any other running children
  pid_t pid;
  int status;
  int signo;

  /**
   * -- WNOHANG
   *  return immediately if none of child in the wait set
   *  has terminated yet
   * -- WUNTRACED
   * suspend execution of the calling process until a process
   * in the wait set becomes either terminated or stopped.
   */
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
    //printf("pid of sigchld = %d\n", pid);

    if ( WIFEXITED(status) ) { 
      // normally exit
      deletejob(&jobs[0], pid);
    } else if ( WIFSTOPPED(status) ) {
      // SIGTSTP
      signo = WSTOPSIG(status);
      if (signo == SIGTSTP) {
        struct job_t* job = getjobpid(&jobs[0], pid);
        printf("Job [%d] (%d) stopped by signal %d\n",
            job->jid, pid, signo);
        // update the Job state to stop
        job->state = ST;
      }
    } else if ( WTERMSIG(status) == SIGINT ) {
      // SIGINT
      printf("Job [%d] (%d) terminated by signal %d\n", 
          pid2jid(pid), pid, SIGINT);
      deletejob(&jobs[0], pid);
    }
  } 

  return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
  // find fg pid by fgpid
  pid_t pid = fgpid(&jobs[0]);
  if (pid == 0) {
    return;
  }
  /**
   * Negative PID values may be  used  to  choose  whole
   * process  groups
   */
  kill(-pid, SIGINT);
  printf("Job [%d] (%d) terminated by signal %d\n", 
      pid2jid(pid), pid, sig);
  deletejob(&jobs[0], pid);
  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
  pid_t pid = fgpid(&jobs[0]);
  //printf("debug: pid= %d\n", pid);
  if (pid == 0) {
    return;
  }
  kill(-pid, SIGTSTP);
  struct job_t* job = getjobpid(&jobs[0], pid);
  printf("Job [%d] (%d) stopped by signal %d\n",
      job->jid, pid, sig);
  // update the Job state to stop
  job->state = ST;
  return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      if (nextjid > MAXJOBS)
        nextjid = 1;
      strcpy(jobs[i].cmdline, cmdline);
      if(verbose){
        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
      }
      return 1;
    }
  }
  printf("Tried to create too many jobs\n");
  return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      clearjob(&jobs[i]);
      nextjid = maxjid(jobs)+1;
      return 1;
    }
  }
  return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].state == FG)
      return jobs[i].pid;
  return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid)
      return &jobs[i];
  return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
  int i;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
      switch (jobs[i].state) {
        case BG: 
          printf("Running ");
          break;
        case FG: 
          printf("Foreground ");
          break;
        case ST: 
          printf("Stopped ");
          break;
        default:
          printf("listjobs: Internal error: job[%d].state=%d ", 
              i, jobs[i].state);
      }
      printf("%s", jobs[i].cmdline);
    }
  }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



