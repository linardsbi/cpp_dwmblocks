#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <X11/Xlib.h>
#include <utility>
#include <fmt/printf.h>
#include <atomic>
#include <ranges>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <thread>

#define CMDLENGTH        50

struct Block {
    const char *icon;
    const char *command;
    unsigned int interval;
    unsigned int signal;
};

void sighandler(int num);

void buttonhandler(int sig, siginfo_t *si, void *ucontext);

void getcmds(int time);

#ifndef __OpenBSD__

void getsigcmds(int signal);

void setupsignals();

#endif

void setroot();

void statusloop();

#include "config.h"

static std::array<std::string, blocks.size()> statusbar;
std::atomic<bool> statusContinue(true);
unsigned changed_blocks = 0;

static void (*writestatus)() = setroot;

inline void set_as_changed(unsigned index) {
    changed_blocks |= 1 << index;
}

inline void strip_newlines(std::string &old) {
    std::erase_if(old, [](const auto ch) { return ch == '\n'; });
}

constexpr auto gcd(auto a, auto b) {
    while (b > 0) {
        a = std::exchange(b, a % b);
    }
    return a;
}

void write_cmd_output(const Block& block, std::string &output) {
    auto get_value_from_cmd = [](const char *cmd)-> std::optional<std::string> {

        if (FILE *cmdf = popen(cmd, "r")) {
            char *status;
            char tmpstr[CMDLENGTH]{};
            do {
                errno = 0;
                tmpstr[0] = '\0';
                status = fgets(tmpstr, CMDLENGTH - static_cast<int>(delimiter.length() + 1), cmdf);
            } while (!status && errno == EINTR);
            pclose(cmdf);

            if (tmpstr[0] < ' ' && tmpstr[0] > 0) {
                return {};
            }

            return {tmpstr};
        }

        //printf("failed to run: %s, %d\n", block.command, errno);
        return {};
    };

    if (auto value = get_value_from_cmd(block.command)) {
        output.clear();

        strip_newlines(value.value());

        if (block.signal != 0) {
            output += static_cast<char>(block.signal);
        }

        output += block.icon;
        output += value.value();

        if (output.length() > 0 && &block != &blocks.back()) {
            output += delimiter;
        }
    }
}


void getcmds(std::uint64_t time = 0) {
    auto updatable = [time](const auto &block) {
        return time == 0 || (block.interval != 0 && time % block.interval == 0);
    };

    for (const auto& block : blocks | std::views::filter(updatable)) {
        const auto bar_index = static_cast<std::size_t>(std::distance(blocks.cbegin(), &block));
        set_as_changed(static_cast<unsigned>(bar_index));
        write_cmd_output(block, statusbar[bar_index]);
    }
}

#ifndef __OpenBSD__

void getsigcmds(int signal) {
    const auto block = std::ranges::find_if(blocks, [signal](const auto &block) {
        return static_cast<int>(block.signal) == signal;
    });

    if (block != blocks.end()) {
        const auto bar_index = static_cast<std::size_t>(std::distance(blocks.cbegin(), block));
        set_as_changed(static_cast<unsigned>(bar_index));
        write_cmd_output(*block, statusbar[bar_index]);
    }
}

void setupsignals() {
    for (int i = SIGRTMIN; i <= SIGRTMAX; i++)
        signal(i, SIG_IGN);

    struct sigaction sa{
            {nullptr},
            {},
            SA_SIGINFO,
          {}
    };

    sa.sa_sigaction = buttonhandler;

    for (const auto& block : blocks) {
        if (block.signal > 0) {
            signal(SIGRTMIN + block.signal, sighandler);
            sigaddset(&sa.sa_mask, SIGRTMIN + block.signal);
        }
    }

    sigaction(SIGUSR1, &sa, nullptr);
    
    constexpr struct sigaction sigchld_action = {
            {SIG_DFL},
            {},
            SA_NOCLDWAIT,
            {}
    };
    sigaction(SIGCHLD, &sigchld_action, nullptr);
}

#endif

inline auto status_has_changed() {
    return changed_blocks > 0;
}

inline auto format_status_bar() {
    changed_blocks = 0;
    return fmt::format("{}", fmt::join(statusbar, ""));
}

void setroot() {
    if (!status_has_changed())//Only set root if text has changed.
        return;

    if (auto *d = XOpenDisplay(nullptr)) {
        static auto screen = DefaultScreen(d);
        static auto root = RootWindow(d, screen);
        XStoreName(d, root, format_status_bar().c_str());
        XCloseDisplay(d);
    } else {
        fmt::fprintf(stderr, "Unable to get display");
        exit(EXIT_FAILURE);
    }
}

void statusloop() {
#ifndef __OpenBSD__
    setupsignals();
#endif
    // first figure out the default wait interval by finding the
    // greatest common denominator of the intervals
    constexpr unsigned interval = []() {
        unsigned interval = -1;
        for (const auto & block : blocks) {
            if (block.interval > 0) {
                interval = gcd(block.interval, interval);
            }
        }
        return interval;
    }();

    constexpr auto sleep_duration = std::chrono::seconds(interval);

    for (std::uint64_t elapsed = 0; statusContinue;) {
        getcmds(elapsed);
        writestatus();

        std::this_thread::sleep_for(sleep_duration);
        elapsed += sleep_duration.count();
    }
}

#ifndef __OpenBSD__

void sighandler(int signum) {
    getsigcmds(signum - SIGRTMIN);
    writestatus();
}

void buttonhandler(int sig, siginfo_t *si, void *ucontext) {
    (void)ucontext;

    const auto process_id = getpid();
    sig = si->si_value.sival_int >> 8;

    auto is_current_sig = [sig](const auto& block) {
        return static_cast<int>(block.signal) == sig;
    };

    if (fork() == 0) {
        const auto current = std::ranges::find_if(blocks, is_current_sig);

        if (current == blocks.end()) {
            fmt::fprintf(stderr, "Invalid update signal: %d", sig);
            exit(EXIT_FAILURE);
        }

        const auto shcmd = fmt::sprintf("%s && kill -%d %d", current->command, current->signal + 34, process_id);
        const char *const command[512] = {"/bin/sh", "-c", shcmd.c_str(), nullptr};
        const char button[] = {static_cast<char>('0' + (si->si_value.sival_int & 0xff)), '\0'};

        setsid();
        setenv("BLOCK_BUTTON", button, 1);
        execvp(command[0], const_cast<char *const *>(command)); // fixme: I don't know how to deal with C APIs
        exit(EXIT_SUCCESS);
    }
}

#endif

int main(int argc, char **argv) {
    static_assert(blocks.size() <= 32, "Too many blocks!");

    for (int i = 0; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg == "-d") {
            delimiter = argv[++i];
        } else if (arg == "-p") {
            // print to stdout
            writestatus = []() {
                if (!status_has_changed())
                    return;
                fmt::printf("%s\n", format_status_bar());
                fflush(stdout);
            };
        }
    }

    auto termhandler = [](int signum) {
        (void)signum;
        statusContinue = false;
        exit(EXIT_SUCCESS);
    };

    signal(SIGTERM, termhandler);
    signal(SIGINT, termhandler);
    statusloop();
}

