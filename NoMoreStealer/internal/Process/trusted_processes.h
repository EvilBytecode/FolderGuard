#pragma once
// Kernel-mode safe Trusted processes / path mapping header
// No CRT, no STL, no SAL annotations. Uses only simple C-style routines safe for kernel.
#include <ntifs.h>
#include <ntstrsafe.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// Buffer length for path handling
#define PATH_BUF_LEN 512

namespace NoMoreStealer {
    namespace Process {
        namespace Trusted {
            typedef unsigned long ULONG_PTR_T;

            struct ProcessPathMapping {
                const CHAR* processName; // expected lowercase name (e.g. "chrome.exe")
                const CHAR* paths[4];    // expected lowercase path substrings
                ULONG pathCount;
            };

            // -------------------------
            // Small helpers (no CRT)
            // -------------------------
            static inline VOID SafeCopyA(CHAR* dest, SIZE_T destCount, const CHAR* src)
            {
                if (!dest) return;
                if (!src) {
                    dest[0] = '\0';
                    return;
                }
                SIZE_T i = 0;
                for (; i + 1 < destCount && src[i] != '\0'; ++i) {
                    dest[i] = src[i];
                }
                dest[i] = '\0';
            }

            static inline VOID AsciiToLowerInPlace(CHAR* str, SIZE_T maxLen)
            {
                if (!str) return;
                for (SIZE_T i = 0; i < maxLen && str[i] != '\0'; ++i) {
                    if (str[i] >= 'A' && str[i] <= 'Z') {
                        str[i] = static_cast<CHAR>(str[i] - 'A' + 'a');
                    }
                }
            }

            static inline SIZE_T SafeStrLen(const CHAR* s, SIZE_T maxLen)
            {
                if (!s) return 0;
                SIZE_T i = 0;
                while (i < maxLen && s[i] != '\0') ++i;
                return i;
            }

            static inline BOOLEAN AsciiEqualsI(const CHAR* a, const CHAR* b)
            {
                if (a == b) return TRUE;
                if (!a || !b) return FALSE;
                for (;; ++a, ++b) {
                    CHAR ca = *a;
                    CHAR cb = *b;
                    if (ca >= 'A' && ca <= 'Z') ca = static_cast<CHAR>(ca - 'A' + 'a');
                    if (cb >= 'A' && cb <= 'Z') cb = static_cast<CHAR>(cb - 'A' + 'a');
                    if (ca == '\0' && cb == '\0') return TRUE;
                    if (ca == '\0' || cb == '\0') return FALSE;
                    if (ca != cb) return FALSE;
                }
            }

            static inline BOOLEAN AsciiStrStrI(const CHAR* haystack, const CHAR* needle)
            {
                if (!haystack || !needle) return FALSE;
                SIZE_T nlen = SafeStrLen(needle, PATH_BUF_LEN);
                SIZE_T hlen = SafeStrLen(haystack, PATH_BUF_LEN);
                if (nlen == 0) return TRUE;
                if (nlen > hlen) return FALSE;
                for (SIZE_T i = 0; i + nlen <= hlen; ++i) {
                    SIZE_T j = 0;
                    for (; j < nlen; ++j) {
                        CHAR ch = haystack[i + j];
                        CHAR nd = needle[j];
                        if (ch >= 'A' && ch <= 'Z') ch = static_cast<CHAR>(ch - 'A' + 'a');
                        if (nd >= 'A' && nd <= 'Z') nd = static_cast<CHAR>(nd - 'A' + 'a');
                        if (ch != nd) break;
                    }
                    if (j == nlen) return TRUE;
                }
                return FALSE;
            }

            // -------------------------
            // Data: mappings & lists
            // -------------------------
            static const ProcessPathMapping PROCESS_PATH_MAPPINGS[] = {
                { "chrome.exe", { "\\program files\\google\\chrome", "\\program files (x86)\\google\\chrome", "\\appdata\\local\\google\\chrome", nullptr }, 3 },
                { "brave.exe", { "\\program files\\bravesoftware\\brave-browser", "\\program files (x86)\\bravesoftware\\brave-browser", "\\appdata\\local\\bravesoftware\\brave-browser", nullptr }, 3 },
                { "msedge.exe", { "\\program files\\microsoft\\edge", "\\program files (x86)\\microsoft\\edge", "\\appdata\\local\\microsoft\\edge", nullptr }, 3 },
                { "firefox.exe", { "\\program files\\mozilla firefox", "\\program files (x86)\\mozilla firefox", "\\appdata\\local\\mozilla firefox", "\\appdata\\roaming\\mozilla firefox" }, 4 },
                { "opera.exe", { "\\program files\\opera", "\\program files (x86)\\opera", "\\appdata\\local\\programs\\opera", "\\appdata\\roaming\\opera software" }, 4 },
                { "discord.exe", { "\\appdata\\local\\discord", "\\appdata\\roaming\\discord", nullptr, nullptr }, 2 },
                { "discordptb.exe", { "\\appdata\\local\\discordptb", "\\appdata\\roaming\\discord", nullptr, nullptr }, 2 },
                { "discordcanary.exe", { "\\appdata\\local\\discordcanary", "\\appdata\\roaming\\discord", nullptr, nullptr }, 2 },
                { "update.exe", { "\\appdata\\local\\discord", "\\appdata\\local\\discordptb", "\\appdata\\local\\discordcanary", nullptr }, 3 },
                { "telegram.exe", { "\\appdata\\roaming\\telegram desktop", "\\program files\\telegram desktop", nullptr, nullptr }, 2 },
                { "signal.exe", { "\\appdata\\local\\programs\\signal", "\\program files\\signal", nullptr, nullptr }, 2 },
                { "explorer.exe", { "\\windows\\explorer.exe", nullptr, nullptr, nullptr }, 1 },
                { "runtimebroker.exe", { "\\windows\\system32\\runtimebroker.exe", "\\windows\\syswow64\\runtimebroker.exe", nullptr, nullptr }, 2 },
                { "werfault.exe", { "\\windows\\system32\\werfault.exe", "\\windows\\syswow64\\werfault.exe", nullptr, nullptr }, 2 },
                { "csrss.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", nullptr, nullptr }, 2 },
                { "java.exe", { "\\program files\\java", "\\program files (x86)\\java", nullptr, nullptr }, 2 },
                { "javaw.exe", { "\\program files\\java", "\\program files (x86)\\java", nullptr, nullptr }, 2 },
                { "exodus.exe", { "\\appdata\\local\\programs\\exodus", "\\appdata\\local\\exodus", nullptr, nullptr }, 2 },
                { "electrum.exe", { "\\program files\\electrum", "\\appdata\\local\\programs\\electrum", nullptr, nullptr }, 2 },
                { "filezilla.exe", { "\\program files\\filezilla", "\\program files (x86)\\filezilla", nullptr, nullptr }, 2 },
                { "svchost.exe", { "\\windows\\system32\\svchost.exe", "\\windows\\syswow64\\svchost.exe", nullptr, nullptr }, 2 },
                { "dwm.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "taskhostw.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "taskhost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "sihost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "securityhealthservice.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "wmiprvse.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "dllhost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "searchindexer.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "searchprotocolhost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "searchfilterhost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "shellexperiencehost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "zen.exe", { "\\AppData\\Local\\Zen Browser\\",  nullptr }, 3 }

            };
            static const ULONG PROCESS_PATH_MAPPINGS_COUNT = sizeof(PROCESS_PATH_MAPPINGS) / sizeof(PROCESS_PATH_MAPPINGS[0]);

            static const CHAR* TRUSTED_PROCESSES[] = {
                "system", "csrss.exe", "update.exe", "werfault.exe",
                "searchindexer.exe", "searchprotocolhost.exe", "searchfilterhost.exe",
                "svchost.exe", "dwm.exe", "audiodg.exe", "taskhostw.exe", "taskhost.exe",
                "sihost.exe", "securityhealthservice.exe", "wmiprvse.exe", "dllhost.exe",
                "chrome.exe", "msedge.exe", "brave.exe", "firefox.exe", "opera.exe",
                "vivaldi.exe", "yandex.exe", "discord.exe", "discordptb.exe", "discordcanary.exe",
                "telegram.exe", "signal.exe", "explorer.exe", "shellexperiencehost.exe",
                "runtimebroker.exe", "java.exe", "javaw.exe", "filezilla.exe",
                "exodus.exe", "electrum.exe", "bitcoin-qt.exe", "bitcoind.exe",
                "atomic.exe", "litecoin-qt.exe", "monerod.exe", "armory.exe", "bytecoind.exe",
                "coinomi.exe", "dash-qt.exe", "mist.exe", "geth.exe", "guarda.exe",
                "jaxx.exe", "mymonero.exe", "zcashd.exe", "lghub_agent.exe",
                "feather launch", "zen.exe"
            };
            static const ULONG TRUSTED_PROCESSES_COUNT = sizeof(TRUSTED_PROCESSES) / sizeof(TRUSTED_PROCESSES[0]);

            static const CHAR* SYSTEM_PROCESSES[] = {
                "system", "smss.exe", "csrss.exe", "wininit.exe",
                "services.exe", "lsass.exe", "winlogon.exe"
            };
            static const ULONG SYSTEM_PROCESSES_COUNT = sizeof(SYSTEM_PROCESSES) / sizeof(SYSTEM_PROCESSES[0]);

            // NEW: Trusted parent processes
            static const CHAR* TRUSTED_PARENTS[] = {
                "explorer.exe", "chrome.exe", "msedge.exe", "brave.exe",
                "firefox.exe", "discord.exe", "telegram.exe", "code.exe",
                "devenv.exe", "svchost.exe", "winlogon.exe", "services.exe",
                "lsass.exe", "taskhostw.exe", "runtimebroker.exe"
            };
            static const ULONG TRUSTED_PARENTS_COUNT = sizeof(TRUSTED_PARENTS) / sizeof(TRUSTED_PARENTS[0]);

            // -------------------------
            // Main trust check
            // -------------------------
            static inline BOOLEAN IsTrustedProcess(const CHAR* procName, const CHAR* procPath)
            {
                CHAR nameBuf[64] = { 0 };
                CHAR pathBuf[PATH_BUF_LEN] = { 0 };
                if (!procName) return FALSE;
                SafeCopyA(nameBuf, sizeof(nameBuf), procName);
                AsciiToLowerInPlace(nameBuf, sizeof(nameBuf));

                for (ULONG i = 0; i < SYSTEM_PROCESSES_COUNT; ++i) {
                    if (AsciiEqualsI(nameBuf, SYSTEM_PROCESSES[i])) return TRUE;
                }

                for (ULONG i = 0; i < TRUSTED_PROCESSES_COUNT; ++i) {
                    if (AsciiEqualsI(nameBuf, TRUSTED_PROCESSES[i])) return TRUE;
                }

                if (!procPath || SafeStrLen(procPath, PATH_BUF_LEN) == 0) return FALSE;
                SafeCopyA(pathBuf, sizeof(pathBuf), procPath);
                AsciiToLowerInPlace(pathBuf, sizeof(pathBuf));

                for (ULONG i = 0; i < PROCESS_PATH_MAPPINGS_COUNT; ++i) {
                    if (AsciiEqualsI(nameBuf, PROCESS_PATH_MAPPINGS[i].processName)) {
                        for (ULONG j = 0; j < PROCESS_PATH_MAPPINGS[i].pathCount; ++j) {
                            const CHAR* sub = PROCESS_PATH_MAPPINGS[i].paths[j];
                            if (!sub) continue;
                            if (AsciiStrStrI(pathBuf, sub)) return TRUE;
                        }
                        return FALSE;
                    }
                }
                return FALSE;
            }
        } // namespace Trusted
    } // namespace Process
} // namespace NoMoreStealer