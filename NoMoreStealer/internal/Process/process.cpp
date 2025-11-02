#include <fltKernel.h>
#include <ntimage.h>
#include <ntifs.h>   // ← HIER IST PsIsProtectedProcessLight!
#include "process.h"

#pragma warning(push)
#pragma warning(disable: 4100)
#pragma warning(disable: 4201)

// Am Anfang von process.cpp, nach den Includes:
#ifndef PsIsProtectedProcessLight
BOOLEAN PsIsProtectedProcessLight(PEPROCESS Process) {
    UNREFERENCED_PARAMETER(Process);
    return FALSE;  // Fallback für Win7/8
}
#endif

extern "C" {
    NTKERNELAPI PSTR PsGetProcessImageFileName(_In_ PEPROCESS Process);
    NTKERNELAPI PPEB PsGetProcessPeb(_In_ PEPROCESS Process);
    NTKERNELAPI PVOID PsGetProcessSectionBaseAddress(_In_ PEPROCESS Process);
}

#ifndef NMS_DEBUG
#define NMS_DEBUG 0
#endif

#define NMS_LOG(fmt, ...) do { if (NMS_DEBUG) { DbgPrint(fmt, __VA_ARGS__); } } while (0)

namespace NoMoreStealer {
    namespace Process {

        // --- 1. Signaturprüfung: PsIsProtectedProcessLight ---
        BOOLEAN IsProcessProtected(PEPROCESS process) {
            if (!process) return FALSE;

            // Verfügbar ab Windows 10 1607 (Build 14393)
            // Prüft: Microsoft-signiert, Store-App, Antimalware, etc.
            return PsIsProtectedProcessLight(process);
        }

        // --- 2. Systemprozesse ---
        BOOLEAN IsSystemProcess(PEPROCESS process) {
            if (!process) return FALSE;
            if (PsGetProcessId(process) == (HANDLE)4) return TRUE;  // System

            const CHAR* image = PsGetProcessImageFileName(process);
            if (!image) return FALSE;

            return (!_stricmp(image, "System") ||
                !_stricmp(image, "smss.exe") ||
                !_stricmp(image, "csrss.exe") ||
                !_stricmp(image, "wininit.exe") ||
                !_stricmp(image, "services.exe") ||
                !_stricmp(image, "lsass.exe") ||
                !_stricmp(image, "winlogon.exe"));
        }

        // --- 3. Bekannte vertrauenswürdige Prozesse ---
        BOOLEAN IsKnownTrustedProcess(PEPROCESS process) {
            const CHAR* image = PsGetProcessImageFileName(process);
            if (!image) return FALSE;

            return (!_stricmp(image, "chrome.exe") ||
                !_stricmp(image, "msedge.exe") ||
                !_stricmp(image, "brave.exe") ||
                !_stricmp(image, "firefox.exe") ||
                !_stricmp(image, "opera.exe") ||
                !_stricmp(image, "vivaldi.exe") ||
                !_stricmp(image, "yandex.exe") ||
                !_stricmp(image, "discord.exe") ||
                !_stricmp(image, "telegram.exe") ||
                !_stricmp(image, "explorer.exe") ||
                !_stricmp(image, "ShellExperienceHost.exe") ||
                !_stricmp(image, "RuntimeBroker.exe") ||
                !_stricmp(image, "Discordptb.exe") ||
                !_stricmp(image, "Signal.exe") ||
                !_stricmp(image, "Mullvad VPN.exe") ||
                !_stricmp(image, "Zen.exe") ||
                !_stricmp(image, "Battle.net.exe") ||
                !_stricmp(image, "java.exe") ||
                !_stricmp(image, "javaw.exe") ||
                !_stricmp(image, "DiscordCanary.exe") ||

                // Wallets
                !_stricmp(image, "exodus.exe") ||
                !_stricmp(image, "electrum.exe") ||
                !_stricmp(image, "bitcoin-qt.exe") ||
                !_stricmp(image, "monero-wallet-gui.exe"));
        }

        // --- 4. Hauptfunktion ---
        BOOLEAN IsAllowed(PEPROCESS process) {
            if (!process) return FALSE;

            if (IsSystemProcess(process)) return TRUE;
            if (IsKnownTrustedProcess(process)) return TRUE;
            if (IsProcessProtected(process)) return TRUE;

            return FALSE;
        }

    } // namespace Process
} // namespace NoMoreStealer

#pragma warning(pop)