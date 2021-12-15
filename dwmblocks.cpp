#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <ctime>
#include <csignal>
#include <cerrno>
#include <X11/Xlib.h>
#include <utility>
#include <fmt/printf.h>
#include <atomic>
#include <ranges>

#define CMDLENGTH        50

struct Block {
    const char *icon;
    const char *command;
    unsigned int interval;
    unsigned int signal;
};

void sighandler(int num);

void buttonhandler(int sig, siginfo_t *si, void *ucontext);

[[maybe_unused]] void replace(char *str, char old, char newc);

void remove_all(char *str, char to_remove);

void getcmds(int time);

#ifndef __OpenBSD__

void getsigcmds(int signal);

void setupsignals();

#endif

int getstatus(char *str, char *last);

void setroot();

void statusloop();

void termhandler(int signum);

#include "config.h"

static Display *dpy;
static int screen;
static Window root;
static char statusbar[blocks.size()][CMDLENGTH] = {{0}};
static char statusstr[2][256];
std::atomic<int> statusContinue = 1;

static void (*writestatus)() = setroot;

[[maybe_unused]] void replace(char *str, char old, char newc) {
    for (char *c = str; *c; c++)
        if (*c == old)
            *c = newc;
}

// the previous function looked nice but unfortunately it didnt work if to_remove was in any position other than the last character
// theres probably still a better way of doing this
void remove_all(char *str, char to_remove) {
    char *read = str;
    char *write = str;
    while (*read) {
        if (*read != to_remove) {
            *write++ = *read;
        }
        ++read;
    }
    *write = '\0';
}

constexpr auto gcd(auto a, auto b) {
    while (b > 0) {
        a = std::exchange(b, a % b);
    }
    return a;
}

//opens process *cmd and stores output in *output
void getcmd(const Block& block, char *output) {
    if (block.signal != 0) {
        output[0] = static_cast<char>(block.signal);
        output++;
    }

    auto get_value_from_cmd = [](const char* cmd)-> std::string {
        FILE *cmdf = popen(cmd, "r");
        if (!cmdf) {
            //printf("failed to run: %s, %d\n", block.command, errno);
            return {};
        }

        char* status;
        char tmpstr[CMDLENGTH]{};

        // TODO decide whether its better to use the last value till next time or just keep trying while the error was the interrupt
        // this keeps trying to read if it got nothing and the error was an interrupt
        //  could also just read to a separate buffer and not move the data over if interrupted
        //  this way will take longer trying to complete 1 thing but will get it done
        //  the other way will move on to keep going with everything and the part that failed to read will be wrong till its updated again
        // either way you have to save the data to a temp buffer because when it fails it writes nothing and then then it gets displayed before this finishes

        do {
            errno = 0;
            status = fgets(tmpstr, CMDLENGTH - (delim.length() + 1), cmdf);
        } while (!status && errno == EINTR);
        pclose(cmdf);

        return tmpstr;
    };

    const auto new_block_value = get_value_from_cmd(block.command);

    auto i = strlen(block.icon);
    strcpy(output, block.icon);
    strcpy(output + i, new_block_value.c_str());
    remove_all(output, '\n');
    i = strlen(output);
    if ((i > 0 && &block != &blocks.back())) {
        strcat(output, delim.data());
    }
    i += delim.length();
    output[i] = '\0';
}

void getcmds(int time) {
    auto updatable = [time](const auto &block) {
        return (block.interval != 0 && time % block.interval == 0) || time == -1;
    };

    for (const auto& block : blocks | std::views::filter(updatable)) {
        const auto bar_index = std::distance(blocks.cbegin(), &block);
        getcmd(block, statusbar[bar_index]);
    }
}

#ifndef __OpenBSD__

void getsigcmds(int signal) {
    for (std::size_t i = 0; const auto& block : blocks) {
        if (static_cast<int>(block.signal) == signal) {
            getcmd(block, statusbar[i]);
        }
        i++;
    }
}

void setupsignals() {
    for (int i = SIGRTMIN; i <= SIGRTMAX; i++)
        signal(i, SIG_IGN);

    struct sigaction sa{};

    for (const auto& block : blocks) {
        if (block.signal > 0) {
            signal(SIGRTMIN + block.signal, sighandler);
            sigaddset(&sa.sa_mask, SIGRTMIN + block.signal);
        }
    }
    
    sa.sa_sigaction = buttonhandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, nullptr);
    
    struct sigaction sigchld_action = {
            {SIG_DFL},
            {},
            SA_NOCLDWAIT,
            {}
    };
    sigaction(SIGCHLD, &sigchld_action, nullptr);
}

#endif

int getstatus(char *str, char *last) {
    strcpy(last, str);
    str[0] = '\0';

    for (auto & i : statusbar) {
        strcat(str, i);
    }
    strcat(str, " ");
    str[strlen(str) - 1] = '\0';
    return strcmp(str, last);//0 if they are the same
}

void setroot() {
    if (!getstatus(statusstr[0], statusstr[1]))//Only set root if text has changed.
        return;
    Display *d = XOpenDisplay(nullptr);
    if (d) {
        dpy = d;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    XStoreName(dpy, root, statusstr[0]);
    XCloseDisplay(dpy);
}

void pstdout() {
    if (!getstatus(statusstr[0], statusstr[1]))//Only write out if text has changed.
        return;
    printf("%s\n", statusstr[0]);
    fflush(stdout);
}


void statusloop() {
#ifndef __OpenBSD__
    setupsignals();
#endif
    // first figure out the default wait interval by finding the
    // greatest common denominator of the intervals
    unsigned interval = -1;

    for (const auto & block : blocks) {
        if (block.interval) {
            interval = gcd(block.interval, interval);
        }
    }

    const struct timespec sleeptime = {interval, 0};
    struct timespec tosleep = sleeptime;
    getcmds(-1);
    for (int i = 0; statusContinue;) {
        // sleep for tosleep (should be a sleeptime of interval seconds) and put what was left if interrupted back into tosleep
        const auto interrupted = nanosleep(&tosleep, &tosleep);
        // if interrupted then just go sleep again for the remaining time
        if (interrupted == -1) {
            continue;
        }
        // if not interrupted then do the calling and writing
        getcmds(i);
        writestatus();
        // then increment since its actually been a second (plus the time it took the commands to run)
        i += static_cast<int>(interval);
        // set the time to sleep back to the sleeptime of 1s
        tosleep = sleeptime;
    }
}

#ifndef __OpenBSD__

void sighandler(int signum) {
    getsigcmds(signum - SIGRTMIN);
    writestatus();
}
#include <algorithm>

void buttonhandler(int sig, siginfo_t *si, void *ucontext) {
    (void)ucontext;

    char button[] = {static_cast<char>(('0' + si->si_value.sival_int) & 0xff), '\0'};
    const auto process_id = getpid();
    sig = si->si_value.sival_int >> 8;

    const auto is_current_sig = [sig](const auto& block) {
        return static_cast<int>(block.signal) == sig;
    };

    if (fork() == 0) {
        const auto current = std::ranges::find_if(blocks, is_current_sig);

        if (current == blocks.end()) {
            fmt::fprintf(stderr, "Invalid update signal: %d", sig);
            exit(EXIT_FAILURE);
        }

        const auto shcmd = fmt::sprintf("%s && kill -%d %d", current->command, current->signal + 34, process_id);

        const char * const command[1024] = {"/bin/sh", "-c", &shcmd[0], nullptr};
        setenv("BLOCK_BUTTON", button, 1);
        setsid();
        execvp(command[0], const_cast<char* const *>(command)); // fixme: I don't know how to deal with C APIs
        exit(EXIT_SUCCESS);
    }
}

#endif

void termhandler(int signum) {
    (void)signum;
    statusContinue = 0;
    exit(0);
}

int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (!strcmp("-d", argv[i]))
            delim = argv[++i];
        else if (!strcmp("-p", argv[i]))
            writestatus = pstdout;
    }
    signal(SIGTERM, termhandler);
    signal(SIGINT, termhandler);
    statusloop();
}

