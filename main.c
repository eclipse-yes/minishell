/*
 * zsh - A mini shell program
 * NKU OS Course
 * Shuhao Zhang
 *
 * file: main.c
 */
#include "main.h"

/* Global variables */
extern char **environ; /* defined in libc */
                       /* command line prompt */
char prompt[]      = "\033[01;32m%s@%s\033[00m:\033[01;34m%s\033[00m %s ";
char user[MAXLINE] = "zsh";
char host[MAXLINE] = "kali";
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */
char cur_dir[MAXLINE];      /* store current directory path */
char prev_dir[MAXLINE];
struct job_t jobs[MAXJOBS]; /* The job list */
const char *delim = ";";    /* delimiter for multi-cmdlines */
/* End global variables */

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdlines[MAXLINE]; /* the user inputted string might be multi-cmds (delimiter: ';'). */
    int emit_prompt = 1;    /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    /* dup2(1, 2); */
    /* TODO: implement the function of pipe. by zsh */

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
        case 'h': /* print help message */
            usage();
            break;
        case 'v': /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':            /* don't print a prompt */
            emit_prompt = 0; /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones implemented for handling signals. */
    Signal(SIGINT, sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1)
    {

        /* Read command line */
        if (emit_prompt)
        {
            print_prompt();
            fflush(stdout);
        }
        if ((fgets(cmdlines, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin))
        { /* End of file (ctrl-d) */
            puts("\n\033[1;32mGood bye from zsh!\033[00m");
            fflush(stdout);
            exit(0);
        }

        cmdlines[strlen(cmdlines) - 1] = ' ';
        /* Evaluate the command line */
        /* If cmdlines is just one cmdline. */
        if (!strchr(cmdlines, delim[0]))
        {
            eval(cmdlines);
            continue;
        }
        /* If cmdlines is multi-cmdline. */
        else
        {
            char *cmdline = strtok(cmdlines, delim);
            while (cmdline)
            {
                //printf("[*] cmdline = [%s]\n", cmdline);
                eval(cmdline);
                cmdline = strtok(NULL, delim);
            }
        }

        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
* print_prompt - print prompt information into stdout
*/
void print_prompt(void) {
    getcwd(cur_dir, sizeof(cur_dir));

    /* Get username and hostname */
    struct passwd* username;
    username = getpwuid(getuid());
    strcpy(user, username->pw_name);

    gethostname(host, sizeof(host));

    char* c;
    if (!strcmp(username->pw_name, "root")) {
        c = "\033[01;31m???\033[00m";
    } else {
        c = "\033[01;32m???\033[00m";
    }

    printf(prompt, user, host, cur_dir, c);
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
    int state = UNDEF;
    sigset_t set;
    pid_t pid;

    // ?????????????????????
    if (parseline(cmdline, argv) == 1)
        state = BG;
    else
        state = FG;

    // ????????????????????????
    if (argv[0] == NULL)
        return;

    // Parse the args, if one is environ variable, then change it to its content.
    for (int i = 0; argv[i]; i++)
    {
        if (argv[i][0] == '$') /* environ var is begin with $, like $PATH. */
        {
            //char* var_name = strchr(argv[i], '$') + 1;
            char* var_name = argv[i] + 1;
            /* Manually implement get environ vaible's content. */
            for (int j = 0; environ[j]; j++)
            {
                if (!strncmp(environ[j], var_name, strlen(argv[i])-1))
                {
                    char* temp = strchr(environ[j], '=') + 1;
                    memset(argv[i], 0, strlen(argv[i]));
                    strcpy(argv[i], temp);
                    break;
                }
            }
        }
    }

    // ????????????????????????????????????, ????????????????????????, ?????????if????????????
    if (!builtin_cmd(argv))
    {
        if (sigemptyset(&set) < 0)
            unix_error("sigemptyset error");
        if (sigaddset(&set, SIGINT) < 0 || sigaddset(&set, SIGTSTP) < 0 || sigaddset(&set, SIGCHLD) < 0)
            unix_error("sigaddset error");
        // ???fork?????????SIGCHLD?????????????????????????????????-???????????????
        if (sigprocmask(SIG_BLOCK, &set, NULL) < 0)
            unix_error("sigprocmask error");

        if ((pid = fork()) < 0)
            unix_error("fork error");
        else if (pid == 0)
        {
            /*  ?????????????????????????????????????????????:
                ??????bash??????zsh??????zsh???bash???????????????????????????
                ??????zsh????????????????????????????????????????????????????????????????????????bash???????????????????????????
                ????????????ctrl-c??????bash???????????????????????????????????????SIGINT???
                ????????????ctrl-c??????zsh??????zsh?????????????????????????????????SIGINT??????????????????????????????
                ???????????????????????????:???fork???????????????execve??????????????????????????????setpgid(0,0)???
                ??????????????????????????????????????????????????????????????????ID???????????????PID?????????
                ?????????bash??????????????????????????????????????????zsh?????????
                ????????????ctrl-c??????zsh??????????????????SIGINT?????????????????????????????????????????????
            */
            // ???????????????????????????
            if (sigprocmask(SIG_UNBLOCK, &set, NULL) < 0)
                unix_error("sigprocmask error");
            if (setpgid(0, 0) < 0)
                unix_error("setpgid error");
            if (env_eval(argv[0], argv, environ) < 0)
            {
                printf("%s: command not found\n", argv[0]);
                exit(0);
            }
        }
        // ????????????????????????job?????????????????????????????????????????????
        addjob(jobs, pid, state, cmdline);
        // ???????????????????????? SIGINT SIGTSTP SIGCHLD
        if (sigprocmask(SIG_UNBLOCK, &set, NULL) < 0)
            unix_error("sigprocmask error");

        // ?????????????????????????????????
        if (state == FG)
            waitfg(pid);
        else
            printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
    }
    return;
}

/*
 * env_eval - eval through environ.
 * flag: -1 command not found, 0 command ok, 1 environ setting.
 */
int env_eval(char *pathname, char **argv, char **environ)
{
    int flag = -1; /* -1: command not found, 0: execve succeed. */
    int ind = -1;
    char* envs;
    char* env;
    const char* delim = ":";

    /* find environ[i], the environment variable PATH. */
    for (int i = 0; environ[i] != NULL; i++)
    {
        if (!strncmp(environ[i], "PATH", 4))
        {
            ind = i;
            break;
        }
    }

    /* loop env+pathname to execute. */
    envs = strchr(environ[ind], '=') + 1;
    env = strtok(envs, delim);
    while (env && flag)
    {
        char* old_path = argv[0];
        char temp[MAXLINE] = {0};
        strcat(temp, env);
        strcat(temp, "/");
        strcat(temp, pathname);
        argv[0] = temp;
        env = strtok(NULL, delim);

        if (execve(temp, argv, environ))  /* execute path+pathname, command not found. */
        {
            argv[0] = old_path;  /* do not change the original argv. */
            continue;
        }

        flag = 0;
    }

    /* just use pathname to execute. */
    if (flag)
    {
        if (!execve(pathname, argv, environ))
        {
            flag = 0;
        }
    }

    return flag;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    //buf[strlen(buf) - 1] = ' '; /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'')
    {
        buf++;
        delim = strchr(buf, '\'');
    }
    else
    {
        delim = strchr(buf, ' ');
    }

    while (delim)
    {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'')
        {
            buf++;
            delim = strchr(buf, '\'');
        }
        else
        {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0) /* ignore blank line */
        return 1;

    /* should the job run in the background? if the last char is &, then yes. */
    if ((bg = (*argv[argc - 1] == '&')) != 0)
    {
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
    int argc = count_argv(argv);
    if (!strcmp(argv[0], "exit"))
    {
        puts("\033[1;32mGood bye from zsh!\033[00m");
        fflush(stdout);
        exit(0);
    }
    else if (strchr(argv[0], '='))  /* set environ variable content */
    {
        if (!strchr(argv[0], ' '))
        {
            char* var;
            char* val;

            var = strtok(argv[0], "=");
            val = strtok(NULL, "=");

            //printf("var: %s\n", var);
            //printf("val: %s\n", val);
            setenv(var, val, 1);
        }
    }
    else if (is_pipe(argv))  /* judge whether is a pipe command or not. */
    {
        char buf[MAXLINE] = {0};
        for (int i = 0; argv[i]; i++)  /* combine into cmdline */
        {
            strcat(buf, argv[i]);
            strcat(buf, " ");
        }
        command_pipe(buf);
    }
    else if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg"))
        do_bgfg(argv);
    else if (!strcmp(argv[0], "jobs"))
        listjobs(jobs);
    else if (!strcmp(argv[0], "pwd"))
        pwd(argc, argv);
    else if (!strcmp(argv[0], "cd"))
        cd(argc, argv);
    else
    {
#ifdef DEBUG
        printf("[*] this is a not-builtin command: %s\n", argv[0]);
#endif
        return 0; /* not a builtin command */
    }
#ifdef DEBUG
    printf("[*] this is a builtin command: %s\n", argv[0]);
#endif
    return 1; /* a builtin command */
}

int is_pipe(char** argv)
{
    for (int i = 0; argv[i]; i++)
    {
        if (strchr(argv[i], '|'))
        {
            return 1;
        }
    }
    return 0;
}

int count_argv(char** argv)
{
    int i;
    for (i = 0; argv[i]; i++)
    {
        
    }
    return i;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
    int parsed;
    struct job_t *job;

    // ???????????????fg/bg???????????????
    if (!argv[1])
    {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    // ??????fg/bg???????????????%??????????????????JobID??????????????????PID
    if (argv[1][0] == '%')
    {
        if ((parsed = strtol(&argv[1][1], NULL, 10)) <= 0)
        {
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
            return;
        }
        if ((job = getjobjid(jobs, parsed)) == NULL)
        {
            printf("%%%d: No such job\n", parsed);
            return;
        }
    }
    else
    {
        if ((parsed = strtol(argv[1], NULL, 10)) <= 0)
        {
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
            return;
        }
        if ((job = getjobpid(jobs, parsed)) == NULL)
        {
            printf("(%d): No such process\n", parsed);
            return;
        }
    }

    if (!strcmp(argv[0], "bg"))
    {
        // bg???????????????????????????????????????????????????
        job->state = BG;

        if (kill(-job->pid, SIGCONT) < 0)
            unix_error("kill error");
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    }
    else if (!strcmp(argv[0], "fg"))
    {
        job->state = FG;
        if (kill(-job->pid, SIGCONT) < 0)
            unix_error("kill error");
        // ???????????????????????????????????????????????????zsh??????????????????????????????
        waitfg(job->pid);
    }
    else
    {
        puts("do_bgfg: Internal error (parsed arg is not bg or fg)");
        exit(0);
    }
    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    struct job_t *job = getjobpid(jobs, pid);
    if (!job)
        return;

    // ??????????????????????????????????????????????????????zsh????????????
    while (job->state == FG)
        // ??????sleep??????????????????????????????????????????sigsuspend
        sleep(1);

    if (verbose)
        printf("waitfg: Process (%d) no longer the fg process\n", pid);

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
    int status, jid;
    pid_t pid;
    struct job_t *job;

    if (verbose)
        puts("sigchld_handler: entering");

    /*
    ???????????????????????????????????????
    waitpid ??????3???
        1. 0         : ??????waitpid?????? ?????????????????? **??????** ??????????????????
        2. WNOHANG   : ???????????????????????????????????????0 ???
                ????????????????????????????????????waitpid??????????????????0
        3. WUNTRACED : ???????????????????????????????????????????????????????????????
                ??????????????????????????????waitpid???????????????WIFSTOPPED(status)??????????????????true
    */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {

        // ??????????????????????????????job??????????????????????????????????????????
        if ((job = getjobpid(jobs, pid)) == NULL)
        {
            printf("Lost track of (%d)\n", pid);
            return;
        }

        jid = job->jid;
        // ???????????????????????????????????????????????????????????????
        if (WIFSTOPPED(status))
        {
            printf("Job [%d] (%d) stopped by signal %d\n", jid, job->pid, WSTOPSIG(status));
            job->state = ST;
        }
        // ?????????????????????????????????
        else if (WIFEXITED(status))
        {
            if (deletejob(jobs, pid))
                if (verbose)
                {
                    printf("sigchld_handler: Job [%d] (%d) deleted\n", jid, pid);
                    printf("sigchld_handler: Job [%d] (%d) terminates OK (status %d)\n", jid, pid, WEXITSTATUS(status));
                }
        }
        // ??????????????????????????????????????????????????????????????????SIGKILL
        else
        {
            if (deletejob(jobs, pid))
            {
                if (verbose)
                    printf("sigchld_handler: Job [%d] (%d) deleted\n", jid, pid);
            }
            printf("Job [%d] (%d) terminated by signal %d\n", jid, pid, WTERMSIG(status));
        }
    }

    if (verbose)
        puts("sigchld_handler: exiting");

    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    // if (sig == 2) {  /* Typing Ctrl+C means the user wants to exit shell. (x no, exit current process) */
    //     puts("\nGood byte from zsh!");
    //     exit(0);
    // }

    if (verbose)
        puts("sigint_handler: entering");

    pid_t pid = fgpid(jobs);

    if (pid)
    {
        // ??????SIGINT????????????????????????????????????
        // ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
        // ????????????????????????????????????????????????????????????????????????????????????
        if (kill(-pid, SIGINT) < 0)
            unix_error("kill (sigint) error");
        if (verbose)
        {
            printf("sigint_handler: Job (%d) killed\n", pid);
        }
    }

    if (verbose)
        puts("sigint_handler: exiting");

    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    if (verbose)
        puts("sigstp_handler: entering");

    pid_t pid = fgpid(jobs);
    struct job_t *job = getjobpid(jobs, pid);

    if (pid)
    {
        if (kill(-pid, SIGTSTP) < 0)
            unix_error("kill (tstp) error");
        if (verbose)
        {
            printf("sigstp_handler: Job [%d] (%d) stopped\n", job->jid, pid);
        }
    }

    if (verbose)
        puts("sigstp_handler: exiting");
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job)
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max = 0;

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

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == 0)
        {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose)
            {
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

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == pid)
        {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid)
{
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
        if (jobs[i].pid == pid)
        {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid != 0)
        {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state)
            {
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
 * End job list helper routines
 ******************************/

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: zsh [-hvp]\n");
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
    // ???????????????????????????????????????????????????
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    // ??????????????????????????????
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

/***************************
 * End other helper routines
 **************************/


/***********************
 * More builtin commands
 **********************/

/*
 * get_pwd - Check PWD in environ and store it to global cur_dir.
 */
void get_pwd(void)
{
    char* temp;

    for (int i = 0; environ[i]; i++)
    {
        if (!strncmp(environ[i], "PWD", 3))
        {
            temp = strchr(environ[i], '=') + 1;
            memset(cur_dir, 0, MAXLINE);
            strcpy(cur_dir, temp);
            break;
        }
    }
}

void pwd(int argc, char** argv)
{
    get_pwd();
    // getcwd(cur_dir, sizeof(cur_dir));
    printf("%s\n", cur_dir);
}

void cd(int argc, char** argv) {
    struct passwd *username;
    username = getpwuid(getuid());

    if (argc == 1) {  /* cd: change directory to /home/username */
        if (!strcmp(username->pw_name, "root")) {
            strcpy(prev_dir, cur_dir);
            #ifdef CD_DEBUG
            printf("previous: %s\n", previous);
            #endif
            chdir("/root");
        } else {
            char home[MAXLINE];
            sprintf(home, "/home/%s", username->pw_name);
            strcpy(prev_dir, cur_dir);
            #ifdef CD_DEBUG
            printf("previous: %s\n", previous);
            #endif
            chdir(home);
        }
    }

    if (argc == 2) {
        if (!strcmp(argv[1], "~")) {
            char home[MAXLINE];
            sprintf(home, "/home/%s", username->pw_name);

            strcpy(prev_dir, cur_dir);
            #ifdef CD_DEBUG
            printf("previous: %s\n", previous);
            #endif
            chdir(home);
        } else if (!strcmp(argv[1], "-")) {
            char temp[MAXLINE];
            strcpy(temp, cur_dir);

            #ifdef CD_DEBUG
            printf("previous: %s\n", previous);
            #endif
            chdir(prev_dir);
            strcpy(prev_dir, temp);
        } else {
            #ifdef CD_DEBUG
            printf("previous: %s\n", previous);
            #endif
            chdir(argv[1]);
            strcpy(prev_dir, argv[1]);
        }
    }

    /* Update environ variable PWD. */
    getcwd(cur_dir, sizeof(cur_dir));
    setenv("PWD", cur_dir, 1);
}

int command_pipe(char buf[MAXLINE]) {
    int i, j;
    // ?????????????????????????????????
    for(j = 0; buf[j] != '\0'; j++) {
        if (buf[j] == '|')
            break;
    }

    //printf("[*] this is command_pip, buf: %s\n", buf);

    // ????????????, ??????????????????????????????????????????????????????
    // outputBuf????????????????????????, inputBuf????????????????????????
    char outputBuf[j+1];
    memset(outputBuf, 0x00, j+1);
    char inputBuf[strlen(buf) - j];
    memset(inputBuf, 0x00, strlen(buf) - j);
    for (i = 0; i < j - 1; i++) {
        outputBuf[i] = buf[i];
    }
    outputBuf[j - 1] = ' ';
    for (i = 0; i < strlen(buf) - j - 1; i++) {
        inputBuf[i] = buf[j + 2 + i];
    }

    // printf("[*] |_left: %s, |_right: %s\n", outputBuf, inputBuf);
    // char* temp[MAXARGS];
    // parseline(outputBuf, temp);
    // for (int i = 0; temp[i]; i++)
    //     printf("[!] temp[%d] = %s\n", i, temp[i]);

    // char* temp2[MAXARGS];
    // parseline(inputBuf, temp2);
    // for (int i = 0; temp2[i]; i++)
    //     printf("[!] temp2[%d] = %s\n", i, temp2[i]);

    int pd[2];
    pid_t pid;
    if (pipe(pd) < 0) {
        perror("pipe()");
        exit(1);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork()");
        exit(1);
    }

    if (pid == 0) {                     // ??????????????????
        //printf("[*] (left) entering subprocess\n");

        char* argv[MAXARGS];

        close(pd[0]);                   // ????????????????????????
        dup2(pd[1], STDOUT_FILENO);     // ???????????????????????????????????????
        parseline(outputBuf, argv);

        // for (int i = 0; argv[i]; i++)
        //     printf("[*] (left) argv[0] = %s\n", argv[0]);

        //execvp(argv[0], argv);
        env_eval(argv[0], argv, environ);

        if (pd[1] != STDOUT_FILENO) {
            close(pd[1]);
        }

        //printf("[*] (left) exiting subprocess\n");
    } else {                              // ??????????????????
        //printf("[*] (right) entering process\n");
        /** ????????????
         *  ??????????????????????????????????????????????????????, 
         *  ???????????????wait???????????????????????????????????????
         */
        char* argv[MAXARGS];
        int status;
        waitpid(pid, &status, 0);       // ?????????????????????
        int err = WEXITSTATUS(status);  // ???????????????????????????
        if (err) { 
            printf("Error: %s\n", strerror(err));
        }

        close(pd[1]);                    // ??????????????????????????????
        dup2(pd[0], STDIN_FILENO);       // ?????????????????????????????????????????????
        parseline(inputBuf, argv);

        // for (int i = 0; argv[i]; i++)
        //     printf("[*] (right) argv[0] = %s\n", argv[0]);

        //execvp(argv[0], argv);
        env_eval(argv[0], argv, environ);

        if (pd[0] != STDIN_FILENO) {
            close(pd[0]);
        }

        //sprintf("[*] (right) exiting subprocess\n");
    }
    return 0;
}

/***************************
 * End more builtin commands
 **************************/
