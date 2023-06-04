/* 
 * tsh - A tiny shell program with job control
 * 
 * Guangyu Peng
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

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
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void do_quit(struct job_t *jobs);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
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

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
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

  sigset_t full_mask, empty_mask;
  sigfillset(&full_mask);
  sigemptyset(&empty_mask);
  /* Disable signals */
  sigprocmask(SIG_SETMASK, &full_mask, NULL);

  /* These are the ones you will need to implement */
  Signal(SIGINT,  sigint_handler);   /* ctrl-c */
  Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
  Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

  /* This one provides a clean way to kill the shell */
  Signal(SIGQUIT, sigquit_handler); 

  /* Initialize the job list */
  initjobs(jobs);

  /* Enable signals */
  sigprocmask(SIG_SETMASK, &empty_mask, NULL);

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
      do_quit(jobs);
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
void eval(char *cmdline) {
  /* 解析命令得到参数数组 */
  char *argv[MAXARGS] = {NULL};
  int bg = parseline(cmdline, argv);
  if (argv[0] == NULL) return;      /* 忽略空行命令*/

  /* 处理内置命令 */
  if (builtin_cmd(argv)) return;

  /* 处理本地命令 */
  sigset_t mask, prev_mask;       /* 屏蔽竞争信号SIGINT SIGTSTP SIGCHLD */
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTSTP);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, &prev_mask);

  pid_t pid = fork();             /* 创建子进程 */
  if (pid < 0) unix_error("fork error");
  if (pid == 0) {                 /* 子进程 */
    int retval = setpgid(0, 0);   /* 设置进程组id */
    if (retval < 0) unix_error("setpgid error");
    
    /* 初始化信号掩码 */
    sigemptyset(&prev_mask);
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    /* 加载可执行文件 */
    retval = execve(argv[0], argv, environ);
    if (retval < 0) {
      printf("Command not found: %s", cmdline);
      exit(1);
    }
  }

  int job_state = bg ? BG : FG;   /* 插入子进程任务信息 */
  addjob(jobs, pid, job_state, cmdline);
  struct job_t *job = getjobpid(jobs, pid);
  
  sigprocmask(SIG_SETMASK, &prev_mask, NULL);   /* 解除屏蔽 */

  /* 等待前台进程 */
  if (!bg) waitfg(pid);
  else {
    printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
  }
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) {
  static char array[MAXLINE]; /* holds local copy of command line */
  char *buf = array;          /* ptr that traverses command line */
  char *delim = NULL;         /* points to first space delimiter */
  int argc = 0;               /* number of args */
  int bg = 0;                 /* background job? */

  strcpy(buf, cmdline);
  buf[strlen(buf)-1] = ' ';       /* replace trailing '\n' with space */
  while (*buf && (*buf == ' '))   /* ignore leading spaces */
    buf++;

  /* Build the argv list */
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
 *   it immediately.  
 */
int builtin_cmd(char **argv) {
  if (strcmp(argv[0], "quit") == 0) {
    do_quit(jobs);
    return 1;
  }
  else if (strcmp(argv[0], "jobs") == 0) {
    sigset_t mask, prev_mask;       /* 屏蔽竞争信号SIGCHLD */
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    listjobs(jobs);

    sigprocmask(SIG_SETMASK, &prev_mask, NULL); /* 恢复信号SIGCHLD */
    return 1;
  }
  else if (strcmp(argv[0], "bg") == 0) {
    do_bgfg(argv);
    return 1;
  }
  else if (strcmp(argv[0], "fg") == 0) {
    do_bgfg(argv);
    return 1;
  }
  return 0;     /* not a builtin command */
}

/* 
 * do_quit - send SIGHUP to child processes and exit
 */
void do_quit(struct job_t *jobs) {
  /* 屏蔽信号 */
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTSTP);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGQUIT);
  sigprocmask(SIG_BLOCK, &mask, NULL);

  /* 向所有子进程发送SIGHUP信号 */
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid > 0) {
      kill(jobs[i].pid, SIGCONT);
      kill(jobs[i].pid, SIGHUP);
      printf("[%d]  + %d hangup    %s",
        jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
    }
  }

  /* 退出程序 */
  exit(0);
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
  if (argv[1] == NULL || argv[1][0] == '\0') {
    printf("Usage: %s pid or %s %%jid\n", argv[0], argv[0]);
    return;
  }

  int jid_flag = 0;         /* 1 if the arg is job id ? */
  char *id_ptr = argv[1];
  if (*id_ptr == '%') {
    jid_flag = 1;
    id_ptr++;
  }

  int id = atoi(id_ptr);
  if (id == 0) {
    printf("Bad argument: %s\n", argv[1]);
    return;
  }

  /* 屏蔽竞争信号SIGINT SIGTSTP SIGCHLD */
  sigset_t mask, prev_mask;       
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTSTP);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, &prev_mask);

  /* 查找对应的job */
  struct job_t *job = NULL;
  job = jid_flag ? getjobjid(jobs, id) : getjobpid(jobs, (pid_t)id);
  if (job == NULL) {
    if (jid_flag) {
      printf("No such job: %s\n", argv[1]);
    }
    else {
      printf("No such process: %s\n", argv[1]);
    }

    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return;
  }

  /* 前台或者后台运行 */
  pid_t pgid = getpgid(job->pid);
  kill(-pgid, SIGCONT);           /* 发送SIGCONT信号 */

  int fg_flag = (argv[0][0] == 'f');
  job->state = fg_flag ? FG : BG;

  /* 解除屏蔽 */
  sigprocmask(SIG_SETMASK, &prev_mask, NULL);

  if (fg_flag) waitfg(job->pid);
  else {
    printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
  }
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
  /* 屏蔽SIGCHLD信号 */
  sigset_t mask, prev_mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigprocmask(SIG_BLOCK, &mask, &prev_mask);

  /* 阻塞直到进程不再前台运行 */
  while (fgpid(jobs) == pid) {
    if (verbose)
      printf("waitfg: fgpid=%d\n", fgpid(jobs));
    sigsuspend(&prev_mask);
  }

  /* 恢复SIGCHLD信号 */
  sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *   a child job terminates (becomes a zombie), or stops because it
 *   received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *   available zombie children, but doesn't wait for any other
 *   currently running children to terminate.  
 */
void sigchld_handler(int sig) {
  int olderrno = errno;       /* 保存errno */
  sigset_t mask, prev_mask;   /* 屏蔽信号 */
  sigfillset(&mask);
  sigprocmask(SIG_BLOCK, &mask, &prev_mask);

  /* 处理子进程状态的变化 */
  int stat;
  pid_t pid;
  while ((pid = waitpid(-1, &stat, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    if (verbose) printf("chld: pid=%d\n", pid);
    
    if (WIFEXITED(stat)) {    /* 子进程退出 */
      if (verbose) printf("chld exit\n");
      deletejob(jobs, pid);
    }
    else if (WIFSIGNALED(stat)) {
      if (verbose) printf("chld signal\n");
      int sig = WTERMSIG(stat);
      struct job_t *job = getjobpid(jobs, pid);
      if (job) {
        printf("Job [%d] (%d) terminated by signal %d\n",
          job->jid, job->pid, sig);
      }
      deletejob(jobs, pid);
    }
    else if (WIFSTOPPED(stat)) {    /* 子进程暂停 */
      if (verbose) printf("chld stop\n");
      int sig = WSTOPSIG(stat);
      struct job_t *job = getjobpid(jobs, pid);
      if (job) {
        job->state = ST;
        printf("Job [%d] (%d) stopped by signal %d\n",
          job->jid, job->pid, sig);
      }
    }
    else if (WIFCONTINUED(stat)) {  /* 子进程继续运行 */
      struct job_t *job = getjobpid(jobs, pid);
      if (job) {
        if (job->state == ST) {
          job->state = BG;
        }
      }
    }
  }

  sigprocmask(SIG_SETMASK, &prev_mask, NULL); /* 恢复信号 */
  errno = olderrno;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *   user types ctrl-c at the keyboard.  Catch it and send it along
 *   to the foreground job.  
 */
void sigint_handler(int sig) {
  int olderrno = errno;       /* 保存errno */
  sigset_t mask, prev_mask;   /* 屏蔽信号 */
  sigfillset(&mask);
  sigprocmask(SIG_BLOCK, &mask, &prev_mask);

  /* 查询前台进程 发送SIGINT */
  pid_t pid = fgpid(jobs);
  if (pid > 0) {
    pid_t pgid = getpgid(pid);
    kill(-pgid, SIGINT);
  }

  sigprocmask(SIG_SETMASK, &prev_mask, NULL); /* 恢复信号 */
  errno = olderrno;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *   the user types ctrl-z at the keyboard. Catch it and suspend the
 *   foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
  int olderrno = errno;       /* 保存errno */
  sigset_t mask, prev_mask;   /* 屏蔽信号 */
  sigfillset(&mask);
  sigprocmask(SIG_BLOCK, &mask, &prev_mask);

  /* 查询前台进程 发送SIGTSTP */
  pid_t pid = fgpid(jobs);
  if (pid > 0) {
    pid_t pgid = getpgid(pid);
    kill(-pgid, SIGTSTP);
  }

  sigprocmask(SIG_SETMASK, &prev_mask, NULL); /* 恢复信号 */
  errno = olderrno;
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
int maxjid(struct job_t *jobs) {
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
  int i;
  
  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      // if (nextjid > MAXJOBS)
      //   nextjid = 1;
      strcpy(jobs[i].cmdline, cmdline);
      if(verbose) {
        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
      }
      return 1;
    }
  }
  printf("Tried to create too many jobs\n");
  return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
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
struct job_t *getjobjid(struct job_t *jobs, int jid) {
  int i;

  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
      return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
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
void listjobs(struct job_t *jobs) {
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
void usage(void) {
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
  struct sigaction action;
  struct sigaction old_action;

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
void sigquit_handler(int sig) {
  printf("Terminating after receipt of SIGQUIT signal\n");
  do_quit(jobs);
}
