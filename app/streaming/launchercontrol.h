#pragma once

#include <atomic>

struct SDL_Window;

// Control channel for an app that embeds Moonlight-Qt as its streaming client (launcher mode,
// MOONLIGHT_LAUNCHER_MODE set): commands arrive as lines on stdin, answers leave as "LAUNCHER-"
// lines on stderr like the other launcher markers.
//
//   quit           end the stream and quit the app on the host (no error shown)
//   disconnect     end the stream, the app keeps running on the host
//   stats on|off   send "LAUNCHER-STATS: <text>" about once a second (newlines as "\n")
//   servercmd N    run the host's server command N (Apollo)
//   mouse on|off   gamepad mouse emulation of all gamepads (also ones connected later)
//   mouse speed S E  its pointer speed (pixels per second at full deflection) and curve exponent
//
// MOONLIGHT_LAUNCHER_MOUSE=1 starts with the mouse emulation on, MOONLIGHT_LAUNCHER_MOUSE_SPEED
// and MOONLIGHT_LAUNCHER_MOUSE_EXPONENT set speed and curve from the start.
//   overlay        performance overlay on / off
//   menu on|off    the launcher's menu is shown over the stream: gamepads reach the host as
//                  released (their input drives the menu) and the mouse is set free
//
// The launcher's identity: MOONLIGHT_LAUNCHER_APPID puts the stream under the launcher's taskbar
// button, MOONLIGHT_LAUNCHER_TITLE and MOONLIGHT_LAUNCHER_ICON (a PNG) name the stream window.
// The window stays invisible until its first picture (no black frame), then "LAUNCHER-FIRSTFRAME".
//
// Once a second while connected: "LAUNCHER-QUALITY: <rtt ms> <rtt variance ms> <frames lost 0|1>
// <audio packets dropped since the last line> <performance overlay 0|1>" for the launcher's own
// connection warning (Moonlight's "poor connection" text stays off in launcher mode).
//
// MOONLIGHT_LAUNCHER_FIRST_PAD="<n>:<name>" names the pad that started the stream (the n-th connected
// game controller of that name, from 0): it becomes player 1, the others leave that slot free for it.
//
// In launcher mode the launcher owns the gamepad combos: the built-in ones (Start held for the
// mouse, Start+Select+L1+R1 to quit, Select+L1+R1+X/Y for the overlays) are off.
class LauncherControl
{
public:
    // Starts reading stdin; does nothing outside launcher mode.
    static void start();

    static bool active() { return s_Active.load(); }

    static bool mouseWanted() { return s_Mouse.load(); }
    static float mouseSpeed() { return s_MouseSpeed.load(); }
    static float mouseExponent() { return s_MouseExponent.load(); }

    static bool statsWanted() { return s_Stats.load(); }

    static void publishStats(const char* text);

    static bool menuOpen() { return s_Menu.load(); }

    // Whether the device at SDL device index deviceIndex is the pad that started the stream.
    static bool isFirstPad(int deviceIndex);

    // Connection details for "LAUNCHER-QUALITY" (any thread).
    static void setFramesLost(bool lost) { s_FramesLost = lost; }
    static void setOverlayShown(bool shown) { s_OverlayShown = shown; }
    static void audioDropped() { s_AudioDrops++; }

    // The new stream window: the launcher's title and icon, hidden until frameDecoded().
    static void prepareWindow(SDL_Window* window);

    // A picture was decoded (decoder thread): shows the window shortly after the first one.
    static void frameDecoded();

    // The window still hidden, taken by whoever shows it (once).
    static void* takeHiddenWindow();

private:
    static void handle(const char* line);

    static std::atomic<bool> s_Stats;
    static std::atomic<bool> s_Active;
    static std::atomic<bool> s_Mouse;
    static std::atomic<float> s_MouseSpeed;
    static std::atomic<float> s_MouseExponent;
    static std::atomic<bool> s_Menu;
    static std::atomic<void*> s_HiddenWindow;
    static std::atomic<bool> s_RevealScheduled;
    static std::atomic<bool> s_FramesLost;
    static std::atomic<bool> s_OverlayShown;
    static std::atomic<int> s_AudioDrops;
};
