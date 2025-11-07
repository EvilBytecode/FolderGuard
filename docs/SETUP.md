# 🔧 NoMoreStealer Setup Guide

> **Complete installation guide based on actual implementation requirements**

---

## ⚠️ Important Disclaimer

**This is a demonstration project with known security limitations. Do not use for production security.**

The driver has several bypasses and is intended for:
- Educational purposes
- Security research  
- Development and testing

---

## Prerequisites

### Required Software
- **Windows 10/11 (x64)** - Required for kernel driver support
- **Administrator privileges** - Essential for driver operations
- **Visual Studio 2019/2022** with Windows Driver Kit (WDK)
- **Go 1.19+** (for user-mode application)

### System Requirements
- **Test Signing enabled** OR **Secure Boot disabled**
- **Antivirus temporarily disabled** (may interfere with unsigned driver)
- **System restore point** (recommended for safety)

---

## Step 1: System Preparation

### Enable Test Signing Mode

Since the driver is unsigned, enable test signing:

```cmd
# Run as Administrator
bcdedit /set testsigning on
```

**Alternative: Disable Secure Boot**
If the above fails:
1. Restart and enter BIOS/UEFI (usually DEL or F2 during boot)
2. Find "Secure Boot" setting and disable it
3. Save and exit BIOS

### Reboot System
```cmd
shutdown /r /t 0
```

After reboot, you should see "Test Mode" in the bottom-right corner of your desktop.

---

## Step 2: Build the Kernel Driver

### Open Project in Visual Studio
1. Open `NoMoreStealer.sln` in Visual Studio
2. Ensure you have the Windows Driver Kit (WDK) installed

### Configure Build Settings
1. Set configuration to **Release**
2. Set platform to **x64**
3. Right-click project → Properties → Driver Signing → **Sign Mode: Off**

### Build the Driver
1. Build → Build Solution (Ctrl+Shift+B)
2. Check for compilation errors
3. Locate the output: `x64\Release\NoMoreStealer.sys`

**Note**: The driver will be unsigned, which is why test signing is required.

---

## Step 3: Install the Driver

### Copy Driver File
Copy the compiled driver to the Windows system directory:

```cmd
# Run as Administrator
copy "x64\Release\NoMoreStealer.sys" "C:\Windows\System32\drivers\"
```

### Verify Installation
```cmd
dir "C:\Windows\System32\drivers\NoMoreStealer.sys"
```

---

## Step 4: Registry Configuration

Run these commands in an **elevated Command Prompt**:

### Service Registration
```cmd
# Set service type (kernel driver)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v Type /t REG_DWORD /d 2 /f

# Set start type (manual start)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v Start /t REG_DWORD /d 3 /f

# Set error control (normal)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v ErrorControl /t REG_DWORD /d 1 /f

# Set filter group
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v Group /t REG_SZ /d "FSFilter Activity Monitor" /f

# Set driver path
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v ImagePath /t REG_EXPAND_SZ /d "\SystemRoot\System32\drivers\NoMoreStealer.sys" /f
```

### Minifilter Instance Configuration
```cmd
# Set default instance
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\Instances" /v DefaultInstance /t REG_SZ /d "NoMoreStealer Instance" /f

# Set altitude (priority level)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\Instances\NoMoreStealer Instance" /v Altitude /t REG_SZ /d "371000" /f

# Set flags
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\Instances\NoMoreStealer Instance" /v Flags /t REG_DWORD /d 0 /f
```

### Registry Values Explained

| Key | Value | Purpose |
|-----|-------|---------|
| `Type` | `2` | Kernel driver service type |
| `Start` | `3` | Manual start (load on demand) |
| `ErrorControl` | `1` | Normal error handling |
| `Group` | `FSFilter Activity Monitor` | Filter manager group |
| `ImagePath` | Driver location | Path to .sys file |
| `Altitude` | `371000` | Filter priority (Activity Monitor range) |
| `Flags` | `0` | No special flags |

---

## Step 5: Load the Driver

### Load Using Filter Manager
```cmd
# Run as Administrator
fltmc load NoMoreStealer
```

### Verify Driver is Running
```cmd
fltmc filters
```

Look for "NoMoreStealer" in the output. You should see:
```
Filter Name                     Num Instances    Altitude    Frame
------------------------------  -------------  ------------  -----
NoMoreStealer                           1       371000.00     0
```

### Check Instance Information
```cmd
fltmc instances -f NoMoreStealer
```

---

## Step 6: Set Up DebugView (Essential)

### Download and Install DebugView
1. Download [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview) from Microsoft Sysinternals
2. Run as Administrator
3. Capture → Capture Kernel (enable this option)
4. Edit → Filter/Highlight → Add filter for "NoMoreStealer"

### Expected Log Messages
When the driver is working, you'll see messages like:
```
[NoMoreStealer] Loading minifilter
[NoMoreStealer] Comm: Shared section at 0x...
[NoMoreStealer] ALLOWED: Proc=chrome.exe PID=1234 Op=OPEN Path=\Device\HarddiskVolume2\Users\...
[NoMoreStealer] BLOCKED: Proc=malware.exe PID=5678 Op=CREATE Path=\Device\HarddiskVolume2\Users\...\Chrome\
```

---

## Step 7: Build User-Mode Application

### Install Go and Wails
```bash
# Install Wails CLI
go install github.com/wailsapp/wails/v2/cmd/wails@latest

# Verify installation
wails doctor
```

### Build the Application
```bash
cd NoMoreStealers_Usermode

# Install dependencies
go mod tidy

# Build for development
wails dev

# Or build for production
wails build
```

The built application will be in `build/bin/nomorestealers-client.exe`.

---

## Step 8: Test the System

### Start the User-Mode Application
1. Run `nomorestealers-client.exe` as Administrator
2. The application should connect to the kernel driver automatically
3. Check for "Successfully initialized kernel communication" in logs

### Test Protection
1. Try to access a protected path with an untrusted process
2. Check DebugView for blocking messages
3. Verify events appear in the application UI

### Protected Paths (Hardcoded)
The driver protects these paths by default:
- Browser data: `\Google\Chrome\User Data`, `\Mozilla\Firefox\Profiles`
- Crypto wallets: `\AppData\Local\Exodus`, `\AppData\Roaming\Electrum\wallets`
- Communication: `\AppData\Roaming\Discord`, `\AppData\Roaming\Telegram Desktop`

---

## Driver Management

### Unload the Driver
```cmd
fltmc unload NoMoreStealer
```

### Check Driver Status
```cmd
# List all filters
fltmc filters

# Check specific filter
fltmc instances -f NoMoreStealer

# View filter details
sc query NoMoreStealer
```

### Restart After Changes
If you rebuild the driver:
1. Unload: `fltmc unload NoMoreStealer`
2. Copy new .sys file
3. Load: `fltmc load NoMoreStealer`

---

## Troubleshooting

### Driver Won't Load

**Check test signing:**
```cmd
bcdedit /enum {current} | findstr testsigning
```
Should show `testsigning Yes`

**Check file permissions:**
```cmd
icacls "C:\Windows\System32\drivers\NoMoreStealer.sys"
```

**Check registry entries:**
```cmd
reg query "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer"
```

### No Debug Output

**Verify DebugView settings:**
- Running as Administrator
- "Capture Kernel" enabled
- No filters blocking NoMoreStealer messages

**Check driver status:**
```cmd
fltmc filters | findstr NoMoreStealer
```

### Application Can't Connect

**Check shared memory section:**
The driver creates `\BaseNamedObjects\NoMoreStealersNotify`. If this fails, the user-mode app can't connect.

**Verify driver is loaded:**
```cmd
fltmc instances -f NoMoreStealer
```

### System Crashes

**Use safe mode:**
1. Boot into Safe Mode
2. Delete the driver: `del "C:\Windows\System32\drivers\NoMoreStealer.sys"`
3. Remove registry entries: `reg delete "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /f`

**Enable Driver Verifier (Advanced):**
```cmd
verifier /standard /driver NoMoreStealer.sys
```

---

## Complete Uninstallation

### Remove Everything
```cmd
# 1. Unload driver
fltmc unload NoMoreStealer

# 2. Delete driver file
del "C:\Windows\System32\drivers\NoMoreStealer.sys"

# 3. Remove registry entries
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /f

# 4. Disable test signing (optional)
bcdedit /set testsigning off

# 5. Reboot
shutdown /r /t 0
```

---

## Security Considerations

### Known Limitations
- **Easily bypassed** by renaming malware to trusted process names
- **Only monitors file creation**, not writes or modifications  
- **Hardcoded paths** cannot adapt to new stealer techniques
- **No behavioral analysis** of process activities

### Safe Testing Practices
- **Use virtual machines** with snapshots
- **Create system restore points** before installation
- **Monitor system stability** during testing
- **Have removal procedures ready**

---

## What Actually Gets Protected

Based on the source code, these specific paths are protected:

### Browser Data
- `\Google\Chrome\User Data`
- `\Microsoft\Edge\User Data`
- `\BraveSoftware\Brave-Browser\User Data`
- `\Opera Software\Opera Stable`
- `\Vivaldi\User Data`
- `\Yandex\YandexBrowser\User Data`
- `\Mozilla\Firefox\Profiles`
- `\AppData\Roaming\zen\Profiles`

### Cryptocurrency Wallets
- `\AppData\Local\Exodus`
- `\AppData\Roaming\Armory`
- `\AppData\Roaming\Atomic\Local Storage\leveldb`
- `\AppData\Roaming\Bitcoin\wallets`
- `\AppData\Roaming\Electrum\wallets`
- `\AppData\Roaming\Ethereum\keystore`
- And many more...

### Communication Apps
- `\AppData\Roaming\Discord`
- `\AppData\Roaming\Discordptb`
- `\AppData\Roaming\Discordcanary`
- `\AppData\Roaming\Telegram Desktop`
- `\AppData\Roaming\Signal`

### Other Targets
- `\AppData\Local\Mullvad VPN\Local Storage\leveldb`
- `C:\Windows\System32\drivers\etc`
- `\AppData\Roaming\Battle.net`

---

<div align="center">

**⚠️ Remember: This is a demo project with known security limitations ⚠️**

*Always test in isolated environments and have removal procedures ready*

# 🔧 NoMoreStealer Setup Guide

> **Complete installation guide based on actual implementation requirements**

---

## ⚠️ Important Disclaimer

**This is a demonstration project with known security limitations. Do not use for production security.**

The driver has several bypasses and is intended for:
- Educational purposes
- Security research  
- Development and testing

---

## Prerequisites

### Required Software
- **Windows 10/11 (x64)** - Required for kernel driver support
- **Administrator privileges** - Essential for driver operations
- **Visual Studio 2019/2022** with Windows Driver Kit (WDK)
- **Go 1.19+** (for user-mode application)

### System Requirements
- **Test Signing enabled** OR **Secure Boot disabled**
- **Antivirus temporarily disabled** (may interfere with unsigned driver)
- **System restore point** (recommended for safety)

---

## Step 1: System Preparation

### Enable Test Signing Mode

Since the driver is unsigned, enable test signing:

```cmd
# Run as Administrator
bcdedit /set testsigning on
```

**Alternative: Disable Secure Boot**
If the above fails:
1. Restart and enter BIOS/UEFI (usually DEL or F2 during boot)
2. Find "Secure Boot" setting and disable it
3. Save and exit BIOS

### Reboot System
```cmd
shutdown /r /t 0
```

After reboot, you should see "Test Mode" in the bottom-right corner of your desktop.

---

## Step 2: Build the Kernel Driver

### Open Project in Visual Studio
1. Open `NoMoreStealer.sln` in Visual Studio
2. Ensure you have the Windows Driver Kit (WDK) installed

### Configure Build Settings
1. Set configuration to **Release**
2. Set platform to **x64**
3. Right-click project → Properties → Driver Signing → **Sign Mode: Off**

### Build the Driver
1. Build → Build Solution (Ctrl+Shift+B)
2. Check for compilation errors
3. Locate the output: `x64\Release\NoMoreStealer.sys`

**Note**: The driver will be unsigned, which is why test signing is required.

---

## Step 3: Install the Driver

### Copy Driver File
Copy the compiled driver to the Windows system directory:

```cmd
# Run as Administrator
copy "x64\Release\NoMoreStealer.sys" "C:\Windows\System32\drivers\"
```

### Verify Installation
```cmd
dir "C:\Windows\System32\drivers\NoMoreStealer.sys"
```

---

## Step 4: Registry Configuration

Run these commands in an **elevated Command Prompt**:

### Service Registration
```cmd
# Set service type (kernel driver)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v Type /t REG_DWORD /d 2 /f

# Set start type (manual start)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v Start /t REG_DWORD /d 3 /f

# Set error control (normal)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v ErrorControl /t REG_DWORD /d 1 /f

# Set filter group
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v Group /t REG_SZ /d "FSFilter Activity Monitor" /f

# Set driver path
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /v ImagePath /t REG_EXPAND_SZ /d "\SystemRoot\System32\drivers\NoMoreStealer.sys" /f
```

### Minifilter Instance Configuration
```cmd
# Set default instance
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\Instances" /v DefaultInstance /t REG_SZ /d "NoMoreStealer Instance" /f

# Set altitude (priority level)
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\Instances\NoMoreStealer Instance" /v Altitude /t REG_SZ /d "371000" /f

# Set flags
reg add "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer\Instances\NoMoreStealer Instance" /v Flags /t REG_DWORD /d 0 /f
```

### Registry Values Explained

| Key | Value | Purpose |
|-----|-------|---------|
| `Type` | `2` | Kernel driver service type |
| `Start` | `3` | Manual start (load on demand) |
| `ErrorControl` | `1` | Normal error handling |
| `Group` | `FSFilter Activity Monitor` | Filter manager group |
| `ImagePath` | Driver location | Path to .sys file |
| `Altitude` | `371000` | Filter priority (Activity Monitor range) |
| `Flags` | `0` | No special flags |

---

## Step 5: Load the Driver

### Load Using Filter Manager
```cmd
# Run as Administrator
fltmc load NoMoreStealer
```

### Verify Driver is Running
```cmd
fltmc filters
```

Look for "NoMoreStealer" in the output. You should see:
```
Filter Name                     Num Instances    Altitude    Frame
------------------------------  -------------  ------------  -----
NoMoreStealer                           1       371000.00     0
```

### Check Instance Information
```cmd
fltmc instances -f NoMoreStealer
```

---

## Step 6: Set Up DebugView (Essential)

### Download and Install DebugView
1. Download [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview) from Microsoft Sysinternals
2. Run as Administrator
3. Capture → Capture Kernel (enable this option)
4. Edit → Filter/Highlight → Add filter for "NoMoreStealer"

### Expected Log Messages
When the driver is working, you'll see messages like:
```
[NoMoreStealer] Loading minifilter
[NoMoreStealer] Comm: Shared section at 0x...
[NoMoreStealer] ALLOWED: Proc=chrome.exe PID=1234 Op=OPEN Path=\Device\HarddiskVolume2\Users\...
[NoMoreStealer] BLOCKED: Proc=malware.exe PID=5678 Op=CREATE Path=\Device\HarddiskVolume2\Users\...\Chrome\
```

---

## Step 7: Build User-Mode Application

### Install Go and Wails
```bash
# Install Wails CLI
go install github.com/wailsapp/wails/v2/cmd/wails@latest

# Verify installation
wails doctor
```

### Build the Application
```bash
cd NoMoreStealers_Usermode

# Install dependencies
go mod tidy

# Build for development
wails dev

# Or build for production
wails build
```

The built application will be in `build/bin/nomorestealers-client.exe`.

---

## Step 8: Test the System

### Start the User-Mode Application
1. Run `nomorestealers-client.exe` as Administrator
2. The application should connect to the kernel driver automatically
3. Check for "Successfully initialized kernel communication" in logs

### Test Protection
1. Try to access a protected path with an untrusted process
2. Check DebugView for blocking messages
3. Verify events appear in the application UI

### Protected Paths (Hardcoded)
The driver protects these paths by default:
- Browser data: `\Google\Chrome\User Data`, `\Mozilla\Firefox\Profiles`
- Crypto wallets: `\AppData\Local\Exodus`, `\AppData\Roaming\Electrum\wallets`
- Communication: `\AppData\Roaming\Discord`, `\AppData\Roaming\Telegram Desktop`

---

## Driver Management

### Unload the Driver
```cmd
fltmc unload NoMoreStealer
```

### Check Driver Status
```cmd
# List all filters
fltmc filters

# Check specific filter
fltmc instances -f NoMoreStealer

# View filter details
sc query NoMoreStealer
```

### Restart After Changes
If you rebuild the driver:
1. Unload: `fltmc unload NoMoreStealer`
2. Copy new .sys file
3. Load: `fltmc load NoMoreStealer`

---

## Troubleshooting

### Driver Won't Load

**Check test signing:**
```cmd
bcdedit /enum {current} | findstr testsigning
```
Should show `testsigning Yes`

**Check file permissions:**
```cmd
icacls "C:\Windows\System32\drivers\NoMoreStealer.sys"
```

**Check registry entries:**
```cmd
reg query "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer"
```

### No Debug Output

**Verify DebugView settings:**
- Running as Administrator
- "Capture Kernel" enabled
- No filters blocking NoMoreStealer messages

**Check driver status:**
```cmd
fltmc filters | findstr NoMoreStealer
```

### Application Can't Connect

**Check shared memory section:**
The driver creates `\BaseNamedObjects\NoMoreStealersNotify`. If this fails, the user-mode app can't connect.

**Verify driver is loaded:**
```cmd
fltmc instances -f NoMoreStealer
```

### System Crashes

**Use safe mode:**
1. Boot into Safe Mode
2. Delete the driver: `del "C:\Windows\System32\drivers\NoMoreStealer.sys"`
3. Remove registry entries: `reg delete "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /f`

**Enable Driver Verifier (Advanced):**
```cmd
verifier /standard /driver NoMoreStealer.sys
```

---

## Complete Uninstallation

### Remove Everything
```cmd
# 1. Unload driver
fltmc unload NoMoreStealer

# 2. Delete driver file
del "C:\Windows\System32\drivers\NoMoreStealer.sys"

# 3. Remove registry entries
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\NoMoreStealer" /f

# 4. Disable test signing (optional)
bcdedit /set testsigning off

# 5. Reboot
shutdown /r /t 0
```

---

## Security Considerations

### Known Limitations
- **Easily bypassed** by renaming malware to trusted process names
- **Only monitors file creation**, not writes or modifications  
- **Hardcoded paths** cannot adapt to new stealer techniques
- **No behavioral analysis** of process activities

### Safe Testing Practices
- **Use virtual machines** with snapshots
- **Create system restore points** before installation
- **Monitor system stability** during testing
- **Have removal procedures ready**

---

## What Actually Gets Protected

Based on the source code, these specific paths are protected:

### Browser Data
- `\Google\Chrome\User Data`
- `\Microsoft\Edge\User Data`
- `\BraveSoftware\Brave-Browser\User Data`
- `\Opera Software\Opera Stable`
- `\Vivaldi\User Data`
- `\Yandex\YandexBrowser\User Data`
- `\Mozilla\Firefox\Profiles`
- `\AppData\Roaming\zen\Profiles`

### Cryptocurrency Wallets
- `\AppData\Local\Exodus`
- `\AppData\Roaming\Armory`
- `\AppData\Roaming\Atomic\Local Storage\leveldb`
- `\AppData\Roaming\Bitcoin\wallets`
- `\AppData\Roaming\Electrum\wallets`
- `\AppData\Roaming\Ethereum\keystore`
- And many more...

### Communication Apps
- `\AppData\Roaming\Discord`
- `\AppData\Roaming\Discordptb`
- `\AppData\Roaming\Discordcanary`
- `\AppData\Roaming\Telegram Desktop`
- `\AppData\Roaming\Signal`

### Other Targets
- `\AppData\Local\Mullvad VPN\Local Storage\leveldb`
- `C:\Windows\System32\drivers\etc`
- `\AppData\Roaming\Battle.net`

---

<div align="center">

**⚠️ Remember: This is a demo project with known security limitations ⚠️**

*Always test in isolated environments and have removal procedures ready*

</div>