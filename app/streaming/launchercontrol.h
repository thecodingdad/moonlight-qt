#pragma once

#include <atomic>

// Control channel for an app that embeds Moonlight-Qt as its streaming client (launcher mode,
// MOONLIGHT_LAUNCHER_MODE set): commands arrive as lines on stdin, answers leave as "LAUNCHER-"
// lines on stderr like the other launcher markers.
//
//   quit           end the stream and quit the app on the host (no error shown)
//   disconnect     end the stream, the app keeps running on the host
//   stats on|off   send "LAUNCHER-STATS: <text>" about once a second (newlines as "\n")
//   servercmd N    run the host's server command N (Apollo)
//   mouse          gamepad mouse emulation on / off
//   overlay        performance overlay on / off
//
// In launcher mode the launcher owns the gamepad combos: the built-in ones (Start held for the
// mouse, Start+Select+L1+R1 to quit, Select+L1+R1+X/Y for the overlays) are off.
class LauncherControl
{
public:
    // Starts reading stdin; does nothing outside launcher mode.
    static void start();

    static bool active() { return s_Active.load(); }

    static bool statsWanted() { return s_Stats.load(); }

    static void publishStats(const char* text);

private:
    static void handle(const char* line);

    static std::atomic<bool> s_Stats;
    static std::atomic<bool> s_Active;
};
