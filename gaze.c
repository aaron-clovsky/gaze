/*******************************************************************************
 gaze - Another scrollable watch command
 Copyright (c) 2025 Aaron Clovsky

 Based on padview.c by Thomas E. Dickey
********************************************************************************
 Copyright 2019-2022,2024 Thomas E. Dickey
 Copyright 2017 Free Software Foundation, Inc.

 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, distribute with modifications, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 Except as contained in this notice, the name(s) of the above copyright
 holders shall not be used in advertising or otherwise to promote the
 sale, use or other dealings in this Software without prior written
 authorization.
*******************************************************************************/

/*******************************************************************************
Headers
*******************************************************************************/
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <curses.h>

/*******************************************************************************
Macros
*******************************************************************************/
/* Default buffer size: 16MB */
#ifndef DEFAULT_BUFFER_SIZE
    #define DEFAULT_BUFFER_SIZE (16 * 1024 * 1024)
#endif

/* Default interval: two seconds */
#ifndef DEFAULT_INTERVAL
    #define DEFAULT_INTERVAL (2)
#endif

/* Default timeout: five seconds */
#ifndef DEFAULT_TIMEOUT
    #define DEFAULT_TIMEOUT (5)
#endif

/* Key codes */
#define CTRL(x) ((x) & 0x1f)
#define ESCAPE  CTRL('[')

/* Deal with warnings */
#ifdef __GNUC__
    #define FALLTHROUGH __attribute__((__fallthrough__))
    #define UNUSED      __attribute__((unused))
#endif

/*******************************************************************************
Globals
*******************************************************************************/
struct
{
    size_t buffer_size;
    int    interval;
    int    interval_digits;
    int    timeout;
    bool   show_lineno;
    char * cmd;
    int    cols;
    int    lines;
    int    lines_digits;
    int    display_cols;
    time_t cmd_time;
} global = {
    /* buffer_size = */ DEFAULT_BUFFER_SIZE,
    /* interval = */ DEFAULT_INTERVAL,
    /* interval_digits = */ 1,
    /* timeout = */ DEFAULT_TIMEOUT,
    /* show_lineno = */ false,
    /* cmd = */ NULL,
    /* cols = */ 1,
    /* lines = */ 1,
    /* lines_digits = */ 1,
    /* display_cols = */ 1,
    /* cmd_time = */ 0
};

/*******************************************************************************
Error handling
*******************************************************************************/
void exit_failed(int exit_code, const char * format, ...)
{
    va_list args;

    endwin();

    va_start(args, format);

    vprintf(format, args);
    puts("");

    va_end(args);

    exit_curses(exit_code);
}

/*******************************************************************************
Handle signals
*******************************************************************************/
void sig_finish(int sig UNUSED)
{
    endwin();

    exit_curses(1);
}

void sig_nothing(int sig UNUSED) { }

void handle_signals()
{
    struct sigaction sa;
    int              i;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = &sig_finish;

    for (i = SIGHUP; i < SIGTERM; i++)
    {
        if (i == SIGKILL)
        {
            continue;
        }
        else if (i == SIGALRM)
        {
            sa.sa_handler = &sig_nothing;
        }
        else
        {
            sa.sa_handler = &sig_finish;
        }

        if (sigaction(i, &sa, NULL) == -1)
        {
            exit_failed(1, "Error: sigaction(%d): %s", i, strerror(errno));
        }
    }
}

/*******************************************************************************
Timer management
*******************************************************************************/
void set_timer(unsigned int milliseconds)
{
    struct itimerval it_val;

    memset(&it_val, 0, sizeof(it_val));

    it_val.it_interval      = it_val.it_value;
    it_val.it_value.tv_sec  = milliseconds / 1000;
    it_val.it_value.tv_usec = (milliseconds * 1000) % 1000000;

    if (setitimer(ITIMER_REAL, &it_val, NULL) == -1)
    {
        exit_failed(1, "Error: setitimer(): %s", strerror(errno));
    }
}

/*******************************************************************************
Execute command and read results to buffer from pipe
*******************************************************************************/
char * cmd_to_buffer(char * cmd)
{
    char * buffer;
    int    pipefd[2];
    int    pid;

    buffer = NULL; /* Fixes clang warning */

    /* Execute and read from process via pipe */
    pipe(pipefd);

    if (!(pid = fork()))
    {
        char * args[4];
        int    dev_null;

        /* Prepare command */
        args[0] = (char *)"sh";
        args[1] = (char *)"-c";
        args[2] = cmd;
        args[3] = NULL;

        /* Redirect I/O */
        if ((dev_null = open("/dev/null", O_RDONLY)) == -1)
        {
            _Exit(1); /* Avoid atexit() hooks */
        }

        dup2(dev_null, 0);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);

        close(pipefd[0]);
        close(pipefd[1]);
        close(dev_null);

        /* Execute command in shell */
        execvp(args[0], args);
    }
    else
    {
        ssize_t retval;
        size_t  size;

        if (!(buffer = (char *)malloc(global.buffer_size)))
        {
            return NULL;
        }

        close(pipefd[1]);

        /* Read from pipe to buffer (with timeout via SIGALRM + EINTR) */
        set_timer(global.timeout * 1000);

        size = 0;

        do {
            retval =
                read(pipefd[0], &buffer[size], global.buffer_size - size - 1);

            if (retval > 0)
            {
                size += retval;

                if (size == global.buffer_size - 1) break;
            }
        } while (retval > 0);

        buffer[size] = '\0';

        set_timer(0); /* Clear timer */

        /* Show error message on timeout */
        if (retval == -1 && errno == EINTR)
        {
            strncpy(buffer, "\n\n\t\tCOMMAND TIMED OUT", global.buffer_size);

            buffer[global.buffer_size - 1] = '\0';
        }

        /* Cleanup */
        close(pipefd[0]);

        kill(SIGHUP, pid);

        wait(NULL);
    }

    return buffer;
}

/*******************************************************************************
Count characters required to print int
*******************************************************************************/
int count_int_chars(int n)
{
    int digits = (n < 0) ? 2 : 1;

    while ((n /= 10)) digits++;

    return digits;
}

/*******************************************************************************
Create pad from buffer
*******************************************************************************/
WINDOW * newpad_buffer(const char * buffer, int * cols_count, int * lines_count)
{
    WINDOW *     pad;
    int          lines;
    int          tmp;
    int          cols;
    const char * prev;
    const char * s;
    int          i;

    /* Calculate number of lines and columns in buffer */
    lines = 1;
    cols  = 1;
    tmp   = 0;

    for (s = buffer; *s; s++)
    {
        if (*s != '\t')
        {
            tmp++;
        }
        else
        {
            tmp += TABSIZE - (tmp % TABSIZE);
        }

        if (*s == '\n')
        {
            if (cols < tmp)
            {
                cols = tmp;
            }

            tmp = 0;

            lines++;
        }
    }

    if (cols < tmp)
    {
        cols = tmp;
    }

    /* Create pad */
    if (!(pad = newpad(lines, cols)))
    {
        exit_failed(1, "Failed to allocate pad workspace");
    }

    /* Render text */
    s    = buffer;
    prev = buffer;

    for (i = 0; i < lines; i++)
    {
        while (*s && *s != '\n') s++;

        wmove(pad, i, 0);

        waddnstr(pad, prev, (int)(s - prev));

        prev = ++s;
    }

    if (cols_count)
    {
        *cols_count = cols;
    }

    if (lines_count)
    {
        *lines_count = lines;
    }

    return pad;
}

/*******************************************************************************
Run command and create pad from results
*******************************************************************************/
WINDOW * newpad_cmd(char * cmd)
{
    WINDOW * pad;
    char *   buffer;

    if (!(buffer = cmd_to_buffer(cmd)))
    {
        exit_failed(1, "Failed to allocate command output buffer");
    }

    pad = newpad_buffer(buffer, &global.cols, &global.lines);

    global.lines_digits = count_int_chars(global.lines);

    global.display_cols =
        global.cols + ((global.show_lineno) ? global.lines_digits + 1 : 0);

    free(buffer);

    return pad;
}

/*******************************************************************************
Show help popup
*******************************************************************************/
void popup_help()
{
    const char * HELP_MSG =
        "Press <Esc> or q to close this window.\n\n"
        "Commands:\n"
        "  <Esc>,q         - Quit gaze\n"
        "  <F1>,?          - Open this help window\n"
        "  <F5>,r          - Execute command now\n"
        "\n"
        "  <Up>,w          - Scroll up one row\n"
        "  <Down>,s        - Scroll down one row\n"
        "  <Left>,a        - Scroll left one column\n"
        "  <Right>,d       - Scroll right one column\n"
        "  <PageDn>,b      - Scroll to next page\n"
        "  <PageUp>,n      - Scroll to previous page\n"
        "  <Home>,h        - Scroll to top\n"
        "  <End>,e         - Scroll to end\n"
        "  <,z             - Scroll to far left\n"
        "  >,x             - Scroll to far right\n"
        "  0 through 9     - Enter Goto Line Number Mode\n"
        "\n"
        "In Goto Line Number Mode:\n"
        "  0 through 9     - Add digit to line number\n"
        "  <Backspace>     - Delete digit\n"
        "  <Esc>           - Exit mode\n"
        "  <Any other key> - Exit mode and go to line number\n";
    const int X      = 5;
    const int Y      = 1;
    const int WIDTH  = COLS - (X * 2);
    const int HEIGHT = LINES - (Y * 2);
    WINDOW *  help;
    WINDOW *  pad;

    /* Create window and pad */
    if (!(help = newwin(HEIGHT, WIDTH, Y, X)))
    {
        return;
    }

    if (!(pad = newpad_buffer(HELP_MSG, NULL, NULL)))
    {
        delwin(help);

        return;
    }

    /* Enable single valued keys support */
    keypad(pad, true);

    /* User input loop */
    {
        int ch = -1;

        do {
            if (ch != -1)
            {
                beep();
            }

            werase(help);
            box(help, 0, 0);
            wnoutrefresh(help);
            pnoutrefresh(pad, 0, 0, Y + 1, X + 1, HEIGHT, WIDTH);
            doupdate();
        } while ((ch = wgetch(pad)) != -1 && ch != ESCAPE && ch != 'q');
    }

    /* Cleanup */
    delwin(help);
    delwin(pad);
}

/*******************************************************************************
Draw main window
*******************************************************************************/
void draw(WINDOW * pad, int top, int left, const char * cmd, bool lineno)
{
    int          digits;
    const char * cmd_time_str;
    int          cmd_time_str_len;
    int          cmd_len;
    int          len;
    int          i;

    cmd_time_str     = ctime(&global.cmd_time);
    cmd_time_str_len = strlen(cmd_time_str);

#define TAG_LINE_CONST "Every %d seconds: "

    len = (1 + COLS - cmd_time_str_len) -
          (sizeof(TAG_LINE_CONST) - 3 + global.interval_digits);

    cmd_len = strnlen(cmd, len);

    erase();

    mvprintw(0, 0, TAG_LINE_CONST "%.*s", global.interval, cmd_len, cmd);

#undef TAG_LINE_CONST

    for (i = 0; i < len - cmd_len; i++)
    {
        addch(' ');
    }

    printw("%s", cmd_time_str);

    if (lineno)
    {
        digits = global.lines_digits;

        for (i = 1; i < global.lines; i++)
        {
            if (top + i > global.lines)
            {
                break;
            }

            mvprintw(i, 0, "%*d:", digits, top + i);
        }
    }
    else
    {
        digits = -1;
    }

    move(LINES - 1, COLS - 1);
    wnoutrefresh(stdscr);
    pnoutrefresh(pad, top, left, 1, digits + 1, LINES - 1, COLS - 1);
    doupdate();
}

/*******************************************************************************
Print usage
*******************************************************************************/
void usage()
{
    puts("Usage: gaze [options] <command>\n"
         "\n"
         "Options:\n"
         " -h, --help     Show this message\n"
         " -l, --lineno   Number all output lines\n"
         " -n, --interval Set command interval\n"
         " -t, --timeout  Set command timeout\n"
         " -b, --buffer   Set buffer size\n"
         "\n"
         "While running press F1 or '?' for help");

    exit(2);
}

/*******************************************************************************
Parse arguments
*******************************************************************************/
bool parse_long(const char * arg, long * output, char ** endptr)
{
    char * s;

    errno = 0;

    *output = strtol(arg, &s, 10);

    if (errno != 0 || s == arg)
    {
        return false;
    }

    if (endptr)
    {
        *endptr = s;
    }

    return true;
}

bool option(const char * curt, const char * verbose, char * opt, char ** endptr)
{
    if (strcmp(verbose, opt) == 0)
    {
		if (endptr)
		{
			*endptr = (char *)"";
		}

        return true;
    }

    if (!endptr)
    {
        if (strcmp(curt, opt) == 0)
        {
            return true;
        }
    }
    else
    {
        if (strncmp(curt, opt, strlen(curt)) == 0)
        {
            *endptr = &opt[strlen(curt)];

            return true;
        }
    }

    return false;
}

void parse_args(int argc, char * argv[])
{
    int    arg_cmd;
    size_t size;
    int    i;

    for (i = 1; i < argc; i++)
    {
        long   tmp;
        char * endptr;
        char * opt_arg;

        if (option("-h", "--help", argv[i], NULL))
        {
            usage();
        }
        else if (option("-l", "--lineno", argv[i], NULL))
        {
            global.show_lineno = true;

            continue;
        }
        else if (option("-n", "--interval", argv[i], &endptr))
        {
            if (!*endptr)
            {
                if (i + 1 == argc)
                {
                    exit_failed(2, "--interval requires an argument");
                }

                opt_arg = argv[++i];
            }
            else
            {
                opt_arg = endptr;
            }

            if (!parse_long(opt_arg, &tmp, NULL))
            {
                exit_failed(2, "Invalid interval: '%s'", opt_arg);
            }

            if (tmp < 1 || tmp > 60)
            {
                exit_failed(2, "Interval out of range [1-60]");
            }

            global.interval        = (int)tmp;
            global.interval_digits = count_int_chars(global.interval);

            continue;
        }
        else if (option("-t", "--timeout", argv[i], &endptr))
        {
            if (!*endptr)
            {
                if (i + 1 == argc)
                {
                    exit_failed(2, "--timeout requires an argument");
                }

                opt_arg = argv[++i];
            }
            else
            {
                opt_arg = endptr;
            }

            if (!parse_long(opt_arg, &tmp, NULL))
            {
                exit_failed(2, "Invalid timeout: '%s'", opt_arg);
            }

            if (tmp < 1 || tmp > 60)
            {
                exit_failed(2, "Timeout out of range [1-60]");
            }

            global.timeout = (int)tmp;

            continue;
        }
        else if (option("-b", "--buffer", argv[i], &endptr))
        {
            if (!*endptr)
            {
                if (i + 1 == argc)
                {
                    exit_failed(2, "--buffer requires an argument");
                }

                opt_arg = argv[++i];
            }
            else
            {
                opt_arg = endptr;
            }

            if (!parse_long(opt_arg, &tmp, &endptr))
            {
                exit_failed(2, "Invalid buffer size: '%s'", opt_arg);
            }

            if (tmp > INT32_MAX)
            {
                exit_failed(2, "Buffer size too large");
            }

            if (*endptr)
            {
                if (endptr[1])
                {
                    exit_failed(2, "Invalid buffer size: '%s'", opt_arg);
                }

                switch (*endptr)
                {
                    case 'g':
                    case 'G': tmp *= 1024; FALLTHROUGH;
                    case 'm':
                    case 'M': tmp *= 1024; FALLTHROUGH;
                    case 'k':
                    case 'K': tmp *= 1024; break;
                    default:
                    {
                        exit_failed(2, "Invalid buffer size: '%s'", opt_arg);
                    }
                }
            }

            if (tmp < 0)
            {
                exit_failed(2, "Buffer size must be positive");
            }
            else if (tmp < 2)
            {
                exit_failed(2, "Buffer size too small");
            }
            else if (tmp > INT32_MAX)
            {
                exit_failed(2, "Buffer size too large");
            }

            global.buffer_size = tmp;

            continue;
        }
        else if (argv[i][0] == '-')
        {
            exit_failed(2, "Invalid option: '%s'", argv[i]);
        }

        break;
    }

    if (i == argc)
    {
        usage();
    }

    arg_cmd = i;
    size    = 1;

    for (i = arg_cmd; i < argc; i++)
    {
        size += strlen(argv[i]) + 1;
    }

    if (!(global.cmd = (char *)malloc(size)))
    {
        exit_failed(2, "malloc() failed");
    }

    global.cmd[0] = '\0';

    for (i = arg_cmd; i < argc; i++)
    {
        strcat(global.cmd, argv[i]);
        strcat(global.cmd, " ");
    }
}

/*******************************************************************************
main()
*******************************************************************************/
int main(int argc, char * argv[])
{
    /* Parse arguments */
    parse_args(argc, argv);
    /* Install signal handlers */
    handle_signals();

    /* Required for UTF-8 support */
    setlocale(LC_ALL, "");
    /* Recommended with above to unbreak floats */
    setlocale(LC_NUMERIC, "C");
    /* Reduce escape delay to 50ms (from 1000ms) */
    ESCDELAY = 50;
    /* Initialize curses */
    initscr();
    /* Enable support single-valued key codes */
    keypad(stdscr, true);
    /* Do not to convert \n to \r\n */
    nonl();
    /* Disable line buffering */
    cbreak();
    /* Disable input echo */
    noecho();
    /* Set getch() to be non-blocking */
    nodelay(stdscr, true);
    /* Use hardware's insert/delete line features */
    idlok(stdscr, true);

    while (1)
    {
        static int             top_row       = 0;
        static int             left_col      = 0;
        static struct timespec last_cmd_time = { 0, 0 };
        static WINDOW *        pad = NULL; /* Initializing fixes warning */
        int                    ch;

        /* Run command if interval has elapsed */
        {
            struct timespec poll_time;
            uint64_t        elapsed;

            /* Calculate time since last command execution */
            clock_gettime(CLOCK_MONOTONIC, &poll_time);

            elapsed  = (poll_time.tv_sec - last_cmd_time.tv_sec);
            elapsed *= UINT64_C(1000000000);
            elapsed += (poll_time.tv_nsec - last_cmd_time.tv_nsec);
            elapsed /= UINT64_C(1000000000);

            /* If interval has elapsed then schedule command execution */
            if ((int)elapsed >= global.interval)
            {
                /* Create pad from command output */
                pad = newpad_cmd(global.cmd);

                /* Record relative and absolute times of command execution */
                clock_gettime(CLOCK_MONOTONIC, &last_cmd_time);

                global.cmd_time = time(NULL);

                if (top_row > (global.lines - LINES + 1))
                {
                    if (global.lines > LINES)
                    {
                        top_row = (global.lines - LINES + 1);
                    }
                    else
                    {
                        top_row = 0;
                    }
                }
            }
        }

        /* Update screen */
        draw(pad, top_row, left_col, global.cmd, global.show_lineno);

        /* Read key, delay between reads, handle line number entry */
        while (1)
        {
            static bool goto_line_number = false;
            static int  line_number      = 0; /* Initializing fixes warning */

            ch = getch();

            if (ch == -1)
            {
                napms(50); /* Delay 50ms */

                if (goto_line_number)
                {
                    continue;
                }
                else
                {
                    break;
                }
            }
            else if (isdigit(ch))
            {
                if (!goto_line_number)
                {
                    line_number = 0;

                    mvprintw(0, 0, "Line: ");
                    clrtoeol();
                }

                goto_line_number = true;

                if (line_number == 0 && ch == '0')
                {
                    continue;
                }

                if (line_number < 200000000)
                {
                    addch(ch & 0xff);

                    line_number = line_number * 10 + (ch - '0');
                }

                continue;
            }
            else if (goto_line_number && ch == KEY_BACKSPACE)
            {
                int y;
                int x;

                getyx(stdscr, y, x);

                if (line_number != 0)
                {
                    line_number /= 10;
                    move(y, x - 1);
                    addch(' ');
                    move(y, x - 1);
                }

                continue;
            }
            else if (goto_line_number)
            {
                if (ch != ESCAPE && line_number != 0)
                {
                    top_row = line_number - 1;

                    if (top_row > (global.lines - LINES + 1))
                    {
                        if (global.lines > LINES)
                        {
                            top_row = (global.lines - LINES + 1);
                        }
                        else
                        {
                            top_row = 0;
                        }
                    }
                }

                goto_line_number = false;
                line_number      = 0;
                ch               = -1;

                break;
            }
            else
            {
                break;
            }
        }

        /* Process keys */
        switch (ch)
        {
            case -1:
            {
                break;
            }
            default:
            {
                beep();

                break;
            }
            case KEY_RESIZE:
            {
                if (left_col != 0)
                {
                    if (left_col + COLS > global.display_cols)
                    {
                        left_col = global.display_cols - COLS;
                    }
                }

                if (top_row != 0)
                {
                    if (top_row > (global.lines - LINES + 1))
                    {
                        top_row = global.lines - LINES + 1;
                    }

                    if (top_row < 0)
                    {
                        top_row = 0;
                    }
                }

                break;
            }
            case KEY_UP:
            case 'w':
            {
                top_row--;

                if (top_row < 0)
                {
                    top_row = 0;
                }

                break;
            }
            case KEY_DOWN:
            case 's':
            {
                if (top_row < (global.lines - LINES + 1))
                {
                    top_row++;
                }

                break;
            }
            case KEY_LEFT:
            case 'a':
            {
                left_col--;

                if (left_col < 0)
                {
                    left_col = 0;
                }

                break;
            }
            case KEY_RIGHT:
            case 'd':
            {
                if (left_col + COLS < global.display_cols)
                {
                    left_col++;
                }

                break;
            }
            case KEY_HOME:
            case 'h':
            {
                if (top_row == 0)
                {
                    left_col = 0;
                }

                top_row = 0;

                break;
            }
            case '<':
            case 'z':
            {
                left_col = 0;

                break;
            }
            case '>':
            case 'x':
            {
                if (COLS < global.display_cols)
                {
                    left_col = global.display_cols - COLS;
                }

                break;
            }
            case KEY_END:
            case 'e':
            {
                bool bottom = false;

                if (global.lines > LINES)
                {
                    if (top_row == (global.lines - LINES + 1))
                    {
                        bottom = true;
                    }

                    top_row = (global.lines - LINES + 1);
                }
                else
                {
                    if (top_row == (global.lines - 2))
                    {
                        bottom = true;
                    }

                    top_row = (global.lines - 2);
                }

                if (bottom)
                {
                    if (COLS < global.display_cols)
                    {
                        left_col = global.display_cols - COLS;
                    }
                }

                break;
            }
            case KEY_NPAGE:
            case 'n':
            {
                if ((top_row + (LINES - 1)) < (global.lines - LINES + 1))
                {
                    top_row += (LINES - 1);
                }
                else if (global.lines >= LINES)
                {
                    top_row = (global.lines - LINES + 1);
                }
                else
                {
                    top_row = 0;
                }

                break;
            }
            case KEY_PPAGE:
            case 'b':
            {
                if (top_row >= LINES)
                {
                    top_row -= (LINES - 1);
                }
                else
                {
                    top_row = 0;
                }

                break;
            }
            case KEY_F(1):
            case '?':
            {
                popup_help();

                break;
            }
            case KEY_F(5):
            case 'r':
            {
                memset(&last_cmd_time, 0, sizeof(last_cmd_time));

                break;
            }
            case ESCAPE:
            case 'q':
            {
                endwin();
                exit_curses(0);

                break; /* Unreachable */
            }
        }
    }

    /* Unreachable */
}
