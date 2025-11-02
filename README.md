# 🛡️ NoMoreStealer

> **A Windows kernel-mode minifilter driver that monitors file system access to protect against information-stealing malware**

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![Status](https://img.shields.io/badge/status-Demo-orange.svg)]()

---

## 📖 Documentation

| 📋 Guide | 🔗 Link |
|-----------|----------|
| **🔧 Driver Setup** | [Setup Guide](docs/SETUP.md) |
| **⚡ Wails Frontend** | [Wails Setup](docs/WAILS_SETUP.md) |
| **🏗️ How It Works** | [Architecture](docs/ARCHITECTURE.md) |
| **🤝 Contributing** | [Contributing Guide](docs/CONTRIBUTING.md) |
| **🖌️ Showcase** | [Showacase of UI](docs/Showcase.md) |
---

## What NoMoreStealer Actually Does

NoMoreStealer is a **Windows kernel minifilter driver** that intercepts file system operations (`IRP_MJ_CREATE` only) and blocks untrusted processes from accessing specific protected paths. It consists of two components:

### Kernel Driver (`NoMoreStealer/`)
- **File System Monitoring**: Hooks `IRP_MJ_CREATE` operations via Filter Manager
- **Path Protection**: Maintains a hardcoded list of protected paths (browser profiles, wallets, etc.)
- **Process Trust**: Uses simple allowlists and `PsIsProtectedProcessLight()` for trust decisions
- **Communication**: Creates a shared memory section for user-mode notifications
- **Logging**: Outputs decisions via `DbgPrint` for debugging

### User-Mode Application (`NoMoreStealers_Usermode/`)
- **Wails Frontend**: Desktop app with HTML/CSS/JavaScript UI
- **Real-time Monitoring**: Reads shared memory section from kernel driver
- **Event Display**: Shows blocked/allowed access attempts with process details
- **Anti-Spy Feature**: Creates transparent overlay windows to block screen capture
- **WebSocket Server**: Provides real-time updates to the frontend
- **System Tray**: Minimizes to system tray with click-to-show functionality

---

## Current Implementation Status

### ✅ What Works
- Basic file system interception on `IRP_MJ_CREATE`
- Hardcoded path protection for common stealer targets
- Simple process trust evaluation using allowlists
- Shared memory communication between kernel and user-mode
- Real-time event display in Wails frontend
- Digital signature verification using WinVerifyTrust
- Anti-spy overlay window creation
- System tray integration

### ⚠️ Current Limitations
- **Demo Status**: This is explicitly a demo/proof-of-concept
- **Limited IRP Coverage**: Only monitors `IRP_MJ_CREATE`, not `WRITE`, `SET_INFORMATION`, etc.
- **Hardcoded Paths**: Protected paths are compiled into the driver
- **Basic Trust Model**: Simple filename-based allowlists, easily bypassed
- **No Certificate Validation**: Uses `PsIsProtectedProcessLight()` but no custom cert checking
- **DbgPrint Logging**: Relies on debug output instead of proper user notifications
- **File Name Spoofing**: Malware can name itself `chrome.exe` to bypass checks

---

## Protected Paths (Hardcoded)

The driver currently protects these specific paths:

**Browsers:**
- `\Google\Chrome\User Data`
- `\Microsoft\Edge\User Data`
- `\BraveSoftware\Brave-Browser\User Data`
- `\Mozilla\Firefox\Profiles`
- And others...

**Cryptocurrency Wallets:**
- `\AppData\Local\Exodus`
- `\AppData\Roaming\Electrum\wallets`
- `\AppData\Roaming\Bitcoin\wallets`
- And others...

**Communication Apps:**
- `\AppData\Roaming\Discord`
- `\AppData\Roaming\Telegram Desktop`
- `\AppData\Roaming\Signal`

---

## Trust Model

The driver considers processes "trusted" if they match:

1. **System Processes**: `System`, `csrss.exe`, `winlogon.exe`, etc.
2. **Hardcoded Allowlist**: `chrome.exe`, `firefox.exe`, `discord.exe`, etc.
3. **Protected Processes**: Via `PsIsProtectedProcessLight()` (Windows 10+ only)

**Note**: This is easily bypassed by malware naming itself as a trusted process.

---

## Quick Installation

### Prerequisites
- Windows 10/11 (x64)
- Administrator privileges
- Visual Studio with WDK (for building)
- Go 1.19+ (for Wails frontend)

### Basic Setup
```cmd
# 1. Enable test signing
bcdedit /set testsigning on

# 2. Reboot system
shutdown /r /t 0

# 3. Build driver in Visual Studio (Release x64)
# 4. Copy NoMoreStealer.sys to C:\Windows\System32\drivers\
# 5. Configure registry (see Setup Guide)
# 6. Load driver: fltmc load NoMoreStealer
```

👉 **[Complete Setup Instructions](docs/SETUP.md)**

---

## Known Issues & TODOs

### High Priority
- **No User-Mode Communicator**: Currently uses `DbgPrint` instead of proper notifications
- **File Name Spoofing**: Malware can impersonate trusted processes by filename
- **Limited IRP Coverage**: Only monitors file creation, not writes/modifications
- **Certificate Verification**: Needs proper WinVerify integration in kernel mode

### Security Concerns
- Easily bypassed by process name spoofing
- No parent process verification
- No behavioral analysis
- Hardcoded paths can't be updated without recompiling
---

## Contributing

This project welcomes contributions, especially for the known limitations:

- **User-mode communication** to replace DbgPrint
- **Enhanced trust verification** beyond simple filename matching
- **Broader IRP coverage** for write operations
- **Dynamic path configuration** without recompiling
- **Anti-bypass techniques** against process spoofing

See [Contributing Guide](docs/CONTRIBUTING.md) for details.

---

## Disclaimer

**⚠️ Important Notice**

This is a **demonstration project** and is not production-ready. It has known security limitations and should only be used for:
- Educational purposes
- Security research
- Development and testing

Do not rely on this for actual protection against sophisticated malware.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

<div align="center">

**Made with the goal of protecting users from information stealers**

*This is my second driver project - help and contributions are appreciated!*

</div>
