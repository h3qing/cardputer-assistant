// cpa-sim serve (gh #32 MVP 2): the device on a pseudo-terminal, in real time, for cpad,
// the HIL harness (tools/sim) or a terminal.
//   cpa-sim serve [--store sd|internal|none] [--usb on|off] [--bat MV]
//                 [--mic silence|FILE.wav] [--shots DIR] [--scale N]
// It runs the runtime every 10 ms with the pty as its USB link (lib/simio), built with
// CPA_TEST_HOOKS, so debug.key / debug.state / debug.reboot work as on the HIL build.
// The card and the outbox store are in memory and survive debug.reboot, not the process.
// While recording, the microphone is silence or the WAV (16 kHz mono 16-bit), looped.
// stdout, one line per event, for the tools that drive it:
//   pty <path>                     first line: open this path as the device's port
//   shot <file> <module> <backlight>  after a `shot` step, once the screen is drawn
//   usb on|off / bat <mv>          a step took effect
//   reboot <boot>                  the device restarted (debug.reboot)
//   rx_dropped <bytes>             the host overran the 8 KB RX queue: the total lost so far
//   error <message>                a stdin line was refused
//   bye                            the last line
// stdin: script steps for a live device (lib/simscript/live.h), run in order. It stops on
// `quit`, at the end of stdin (so it never outlives the tool that started it), on SIGINT
// and on SIGTERM. Without a driver, keep stdin open: `tail -f /dev/null | cpa-sim serve`.
#pragma once

namespace cpasim {

extern const char* const kServeUsage;

int serve_main(int argc, char** argv);  // argv after "serve"; the exit status

}  // namespace cpasim
