#include "launchercontrol.h"
#include "session.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <Limelight.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

std::atomic<bool> LauncherControl::s_Stats { false };
std::atomic<bool> LauncherControl::s_Active { false };
std::atomic<bool> LauncherControl::s_Mouse { false };
std::atomic<float> LauncherControl::s_MouseSpeed { 1200.0f };
std::atomic<float> LauncherControl::s_MouseExponent { 2.4f };

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
