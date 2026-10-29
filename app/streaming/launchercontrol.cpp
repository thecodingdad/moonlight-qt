#include "launchercontrol.h"
#include "session.h"

#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>

#include "SDL_compat.h"
#include <SDL_syswm.h>

#include <Limelight.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

std::atomic<bool> LauncherControl::s_Stats { false };
std::atomic<bool> LauncherControl::s_Active { false };
std::atomic<bool> LauncherControl::s_Mouse { false };
std::atomic<float> LauncherControl::s_MouseSpeed { 1200.0f };
std::atomic<float> LauncherControl::s_MouseExponent { 2.4f };
std::atomic<bool> LauncherControl::s_Menu { false };
std::atomic<void*> LauncherControl::s_HiddenWindow { nullptr };
std::atomic<bool> LauncherControl::s_RevealScheduled { false };
std::atomic<bool> LauncherControl::s_FramesLost { false };
std::atomic<bool> LauncherControl::s_OverlayShown { false };
std::atomic<int> LauncherControl::s_AudioDrops { 0 };

// Shown at the latest after this long, also when no picture arrives (the stream's own errors).
static const Uint32 REVEAL_FALLBACK_MS = 8000;
// After the first decoded picture: time for it to be presented.
static const Uint32 REVEAL_DELAY_MS = 100;

void LauncherControl::start()
{
    if (!qEnvironmentVariableIsSet("MOONLIGHT_LAUNCHER_MODE")) {
        return;
    }
    s_Active = true;
    s_Mouse = qgetenv("MOONLIGHT_LAUNCHER_MOUSE") == "1";
    bool ok = false;
    float speed = qEnvironmentVariable("MOONLIGHT_LAUNCHER_MOUSE_SPEED").toFloat(&ok);
    if (ok && speed > 0) {
        s_MouseSpeed = speed;
    }
    float exponent = qEnvironmentVariable("MOONLIGHT_LAUNCHER_MOUSE_EXPONENT").toFloat(&ok);
    if (ok && exponent > 0) {
        s_MouseExponent = exponent;
    }

#ifdef Q_OS_WIN
    // One taskbar button with the launcher (before any window is created).
    QString appId = qEnvironmentVariable("MOONLIGHT_LAUNCHER_APPID");
    if (!appId.isEmpty()) {
        typedef HRESULT (WINAPI *SetAppId)(PCWSTR);
        auto setAppId = reinterpret_cast<SetAppId>(
            GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SetCurrentProcessExplicitAppUserModelID"));
        if (setAppId != nullptr) {
            setAppId(reinterpret_cast<PCWSTR>(appId.utf16()));
        }
    }
#endif

    // The connection once a second while a stream is connected (no round trip time otherwise).
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            uint32_t rtt = 0;
            uint32_t variance = 0;
            if (!LiGetEstimatedRttInfo(&rtt, &variance)) {
                s_AudioDrops = 0;
                continue;
            }
            std::fprintf(stderr, "LAUNCHER-QUALITY: %u %u %d %d %d\n", rtt, variance, s_FramesLost.load() ? 1 : 0,
                         s_AudioDrops.exchange(0), s_OverlayShown.load() ? 1 : 0);
            std::fflush(stderr);
        }
    }).detach();

    std::thread([] {
        std::string line;
#ifdef Q_OS_WIN
        // A GUI program has no C runtime stdin: read the inherited pipe directly.
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        if (in == nullptr || in == INVALID_HANDLE_VALUE) {
            return;
        }
        char buffer[256];
        DWORD read = 0;
        while (ReadFile(in, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            for (DWORD i = 0; i < read; i++) {
                if (buffer[i] == '\n') {
                    handle(line.c_str());
                    line.clear();
                }
                else if (buffer[i] != '\r') {
                    line += buffer[i];
                }
            }
        }
#else
        int c;
        while ((c = std::getchar()) != EOF) {
            if (c == '\n') {
                handle(line.c_str());
                line.clear();
            }
            else if (c != '\r') {
                line += static_cast<char>(c);
            }
        }
#endif
    }).detach();
}

void LauncherControl::handle(const char* line)
{
    if (std::strcmp(line, "quit") == 0 || std::strcmp(line, "disconnect") == 0) {
        bool quitApp = std::strcmp(line, "quit") == 0;
        // The session lives on the main thread.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [quitApp] {
            Session* session = Session::get();
            if (session == nullptr) {
                return;
            }
            if (quitApp) {
                session->setShouldExit(true);
            }
            else {
                session->setShouldExitKeepingApp();
            }
            session->interrupt();
        }, Qt::QueuedConnection);
    }
    else if (std::strcmp(line, "stats on") == 0) {
        s_Stats = true;
    }
    else if (std::strcmp(line, "stats off") == 0) {
        s_Stats = false;
    }
    else if (std::strcmp(line, "mouse on") == 0 || std::strcmp(line, "mouse off") == 0) {
        bool on = std::strcmp(line, "mouse on") == 0;
        s_Mouse = on;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [on] {
            Session* session = Session::get();
            if (session != nullptr) {
                session->setGamepadMouse(on);
            }
        }, Qt::QueuedConnection);
    }
    else if (std::strncmp(line, "mouse speed ", 12) == 0) {
        float speed = 0, exponent = 0;
        if (std::sscanf(line + 12, "%f %f", &speed, &exponent) == 2 && speed > 0 && exponent > 0) {
            s_MouseSpeed = speed;
            s_MouseExponent = exponent;
        }
    }
    else if (std::strcmp(line, "overlay") == 0) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [] {
            Session* session = Session::get();
            if (session != nullptr) {
                Overlay::OverlayManager& overlays = session->getOverlayManager();
                overlays.setOverlayState(Overlay::OverlayDebug, !overlays.isStatsEnabled());
            }
        }, Qt::QueuedConnection);
    }
    else if (std::strcmp(line, "menu on") == 0 || std::strcmp(line, "menu off") == 0) {
        bool on = std::strcmp(line, "menu on") == 0;
        s_Menu = on;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [on] {
            Session* session = Session::get();
            if (session != nullptr) {
                session->setLauncherMenu(on);
            }
        }, Qt::QueuedConnection);
    }
    else if (std::strncmp(line, "servercmd ", 10) == 0) {
        int index = std::atoi(line + 10);
        if (index >= 0 && index <= 255) {
            LiSendExecServerCmd(static_cast<uint8_t>(index));
        }
    }
}

void LauncherControl::publishStats(const char* text)
{
    std::string escaped;
    for (const char* p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            escaped += "\\n";
        }
        else if (*p != '\r') {
            escaped += *p;
        }
    }
    std::fprintf(stderr, "LAUNCHER-STATS: %s\n", escaped.c_str());
    std::fflush(stderr);
}

#ifdef Q_OS_WIN
static void setCloaked(void* hwnd, BOOL cloaked)
{
    DwmSetWindowAttribute(static_cast<HWND>(hwnd), DWMWA_CLOAK, &cloaked, sizeof(cloaked));
}
#endif

static void reveal()
{
    void* hwnd = LauncherControl::takeHiddenWindow();
    if (hwnd == nullptr) {
        return;
    }
#ifdef Q_OS_WIN
    // The window may be gone by then (stream ended early): DWM just refuses the handle.
    setCloaked(hwnd, FALSE);
#endif
    std::fprintf(stderr, "LAUNCHER-FIRSTFRAME\n");
    std::fflush(stderr);
}

static Uint32 revealTimer(Uint32, void*)
{
    reveal();
    return 0;
}

void LauncherControl::prepareWindow(SDL_Window* window)
{
    if (!active()) {
        return;
    }
    QString title = qEnvironmentVariable("MOONLIGHT_LAUNCHER_TITLE");
    if (!title.isEmpty()) {
        SDL_SetWindowTitle(window, title.toUtf8().constData());
    }
    QImage icon(qEnvironmentVariable("MOONLIGHT_LAUNCHER_ICON"));
    if (!icon.isNull()) {
        icon = icon.convertToFormat(QImage::Format_RGBA8888);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom((void*)icon.constBits(), icon.width(), icon.height(),
                                                                  32, static_cast<int>(icon.bytesPerLine()), SDL_PIXELFORMAT_RGBA32);
        if (surface != nullptr) {
            SDL_SetWindowIcon(window, surface);
            SDL_FreeSurface(surface);
        }
    }
#ifdef Q_OS_WIN
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
        // Invisible but drawn into: the launcher's start screen stays until the first picture.
        setCloaked(info.info.win.window, TRUE);
        s_RevealScheduled = false;
        s_HiddenWindow = info.info.win.window;
        SDL_AddTimer(REVEAL_FALLBACK_MS, revealTimer, nullptr);
    }
#endif
}

void* LauncherControl::takeHiddenWindow()
{
    return s_HiddenWindow.exchange(nullptr);
}

void LauncherControl::frameDecoded()
{
    // Called for every picture: only the first one of a hidden window schedules the reveal.
    if (s_HiddenWindow.load() != nullptr && !s_RevealScheduled.exchange(true)) {
        SDL_AddTimer(REVEAL_DELAY_MS, revealTimer, nullptr);
    }
}
