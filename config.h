#include <array>

//Modify this file to change what commands output to your statusbar, and recompile using the make command.
static constexpr std::array blocks = {
              /*Icon*/    /*Command*/        /*Update Interval*/    /*Update Signal*/
        /* Block{"⌨", "sb-kbselect", 0, 30}, */
        Block{"", "cat /tmp/recordingicon 2>/dev/null", 0, 9},
//        Block{"", "sb-tasks", 10, 26},
        Block{"", "sb-music", 0, 11},
        Block{"", "sb-pacpackages", 0, 8},
        Block{"", "sb-price btc Bitcoin 'BTC '", 3600, 23},
        Block{"", "sb-price ftm \"Fantom\" 'FTM '", 3600, 21},
//        Block{"", "sb-torrent", 20, 7},
        Block{"", "sb-memory", 10, 14},
        Block{"", "sb-cpu", 10, 18},
        /* Block{"",	"sb-moonphase",	18000,	17}, */
        Block{"", "sb-forecast", 18000, 5},
//	Block{"",	"sb-mailbox",	180,	12},
        Block{"", "sb-nettraf", 1, 16},
        Block{"", "sb-volume", 0, 10},
//        Block{"", "sb-battery", 5, 3},
        Block{"", "sb-clock", 20, 1},
        Block{"", "sb-internet", 5, 4},
//        Block{"", "sb-help-icon", 0, 15},
};

//Sets delimiter between status commands. NULL character ('\0') means no delimiter.
static std::string_view delimiter = "  ";

// Have dwmblocks automatically recompile and run when you edit this file in
// vim with the following line in your vimrc/init.vim:

// autocmd BufWritePost ~/.local/src/dwmblocks/config.h !cd ~/.local/src/dwmblocks/; sudo make install && { killall -q dwmblocks;setsid dwmblocks & }

