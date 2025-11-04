#pragma once

namespace NoMoreStealer {
    namespace Process {
        namespace Trusted {

            struct ProcessPathMapping {
                const CHAR* processName;
                const CHAR* paths[4];
                ULONG pathCount;
            };

            static const ProcessPathMapping PROCESS_PATH_MAPPINGS[] = {
                { "chrome.exe", { "\\program files\\google\\chrome", "\\program files (x86)\\google\\chrome", "\\appdata\\local\\google\\chrome", nullptr }, 3 },
                { "brave.exe", { "\\program files\\bravesoftware\\brave-browser", "\\program files (x86)\\bravesoftware\\brave-browser", "\\appdata\\local\\bravesoftware\\brave-browser", nullptr }, 3 },
                { "msedge.exe", { "\\program files\\microsoft\\edge", "\\program files (x86)\\microsoft\\edge", "\\appdata\\local\\microsoft\\edge", nullptr }, 3 },
                { "firefox.exe", { "\\program files\\mozilla firefox", "\\program files (x86)\\mozilla firefox", "\\appdata\\local\\mozilla firefox", "\\appdata\\roaming\\mozilla firefox" }, 4 },
                { "opera.exe", { "\\program files\\opera", "\\program files (x86)\\opera", "\\appdata\\local\\programs\\opera", "\\appdata\\roaming\\opera software" }, 4 },
                { "discord.exe", { "\\appdata\\local\\discord", "\\appdata\\roaming\\discord", nullptr, nullptr }, 2 },
                { "Discordptb.exe", { "\\appdata\\local\\discord", "\\appdata\\roaming\\discord", nullptr, nullptr }, 2 },
                { "DiscordCanary.exe", { "\\appdata\\local\\discord", "\\appdata\\roaming\\discord", nullptr, nullptr }, 2 },
                { "telegram.exe", { "\\appdata\\roaming\\telegram desktop", "\\program files\\telegram desktop", nullptr, nullptr }, 2 },
                { "Signal.exe", { "\\appdata\\local\\programs\\signal", "\\program files\\signal", nullptr, nullptr }, 2 },
                { "explorer.exe", { "\\windows\\explorer.exe", nullptr, nullptr, nullptr }, 1 },
                { "RuntimeBroker.exe", { "\\windows\\system32\\runtimebroker.exe", "\\windows\\syswow64\\runtimebroker.exe", nullptr, nullptr }, 2 },
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
                { "SecurityHealthService.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "WmiPrvSE.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "dllhost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "SearchIndexer.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "SearchProtocolHost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "SearchFilterHost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 },
                { "ShellExperienceHost.exe", { "\\windows\\system32\\", "\\windows\\syswow64\\", "\\windows\\systemapps\\", nullptr }, 3 }
            };

            static const ULONG PROCESS_PATH_MAPPINGS_COUNT = sizeof(PROCESS_PATH_MAPPINGS) / sizeof(PROCESS_PATH_MAPPINGS[0]);

            static const CHAR* TRUSTED_PROCESSES[] = {
                "SearchIndexer.exe", "SearchProtocolHost.exe", "SearchFilterHost.exe",
                "svchost.exe", "dwm.exe", "audiodg.exe", "taskhostw.exe", "taskhost.exe",
                "sihost.exe", "SecurityHealthService.exe", "WmiPrvSE.exe", "dllhost.exe",
                "chrome.exe", "msedge.exe", "brave.exe", "firefox.exe", "opera.exe",
                "vivaldi.exe", "yandex.exe", "discord.exe", "Discordptb.exe", "DiscordCanary.exe",
                "telegram.exe", "Signal.exe", "explorer.exe", "ShellExperienceHost.exe",
                "RuntimeBroker.exe", "Zen.exe", "Battle.net.exe", "Agent.exe", "Battle.net Launcher.exe",
                "java.exe", "javaw.exe", "Lunar Client.exe", "Feather Launcher.exe",
                "filezilla.exe", "Minecraft Launcher.exe", "Tlauncher.exe", "Mullvad VPN.exe",
                "exodus.exe", "electrum.exe", "bitcoin-qt.exe", "bitcoind.exe", "atomic.exe",
                "litecoin-qt.exe", "monerod.exe", "Armory.exe", "bytecoind.exe", "Coinomi.exe",
                "dash-qt.exe", "Mist.exe", "geth.exe", "Guarda.exe", "Jaxx.exe", "MyMonero.exe",
                "zcashd.exe"
            };

            static const ULONG TRUSTED_PROCESSES_COUNT = sizeof(TRUSTED_PROCESSES) / sizeof(TRUSTED_PROCESSES[0]);

            static const CHAR* TRUSTED_PARENTS[] = {
                "explorer.exe", "chrome.exe", "brave.exe", "msedge.exe", "firefox.exe",
                "opera.exe", "svchost.exe", "services.exe"
            };

            static const ULONG TRUSTED_PARENTS_COUNT = sizeof(TRUSTED_PARENTS) / sizeof(TRUSTED_PARENTS[0]);

            static const CHAR* SYSTEM_PROCESSES[] = {
                "System", "smss.exe", "csrss.exe", "wininit.exe", "services.exe",
                "lsass.exe", "winlogon.exe", "ScreenClipping.exe"
            };

            static const ULONG SYSTEM_PROCESSES_COUNT = sizeof(SYSTEM_PROCESSES) / sizeof(SYSTEM_PROCESSES[0]);

        }
    }
}

