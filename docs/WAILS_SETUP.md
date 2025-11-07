# ⚡ Wails Frontend Setup Guide

> **Modern desktop application setup for NoMoreStealer management interface**

---

## 🎯 Overview

The NoMoreStealer frontend is built with **Wails v2**, providing a modern desktop application interface for managing the kernel driver. It features:

- 🖥️ **Native Desktop App** - Cross-platform desktop application
- 🌐 **Web Technologies** - HTML, CSS, JavaScript frontend
- 🔗 **Go Backend** - Powerful Go-based application logic
- 📡 **Real-time Communication** - WebSocket integration with the driver
- 🎨 **Modern UI** - Clean, responsive interface

---

## 📋 Prerequisites

### Required Software
- ✅ **Go 1.19+** - [Download Go](https://golang.org/dl/)
- ✅ **Node.js 16+** - [Download Node.js](https://nodejs.org/)
- ✅ **Wails CLI** - Will be installed in setup
- ✅ **Git** - For cloning dependencies

### System Requirements
- **Windows 10/11** (primary support)
- **4GB RAM** minimum
- **WebView2 Runtime** (usually pre-installed on Windows 11)

---

## 🚀 Quick Start

### 1. Install Wails CLI
```bash
go install github.com/wailsapp/wails/v2/cmd/wails@latest
```

### 2. Verify Installation
```bash
wails doctor
```
This command checks your system for all required dependencies.

### 3. Navigate to Frontend Directory
```bash
cd NoMoreStealers_Usermode
```

### 4. Install Dependencies
```bash
# Install Go dependencies
go mod tidy

# Install frontend dependencies (if any)
# npm install  # Currently not needed as per wails.json
```

---

## 🏗️ Development Setup

### Development Mode
Run the application in development mode with hot reload:

```bash
wails dev
```

This will:
- Start the Go backend
- Launch the frontend with hot reload
- Open the application window
- Enable debugging tools

### Development Features
- 🔄 **Hot Reload** - Automatic refresh on code changes
- 🐛 **Debug Mode** - Browser dev tools available
- 📊 **Live Logs** - Real-time application logs
- ⚡ **Fast Iteration** - Instant feedback on changes

---

## 📦 Building for Production

### Build Release Version
```bash
wails build
```

### Build with Custom Options
```bash
# Build with specific flags
wails build -clean -upx -s

# Build for different platforms
wails build -platform windows/amd64
```

### Build Output
The built application will be located in:
```
build/bin/nomorestealers-client.exe
```

---

## ⚙️ Configuration

### Wails Configuration (`wails.json`)
```json
{
  "name": "NoMoreStealers Client",
  "outputfilename": "nomorestealers-client",
  "width": 1024,
  "height": 768,
  "frameless": true,
  "backgroundColour": "#000000"
}
```

### Key Settings Explained
| Setting | Value | Purpose |
|---------|-------|---------|
| `frameless` | `true` | Custom window decorations |
| `width/height` | `1024x768` | Default window size |
| `backgroundColour` | `#000000` | Window background |
| `debounceMS` | `10` | Event debouncing |

---

## 🔧 Application Structure

```
NoMoreStealers_Usermode/
├── 📁 frontend/           # Web frontend
│   ├── index.html         # Main HTML file
│   ├── app.js            # JavaScript logic
│   └── wailsjs/          # Generated Wails bindings
├── 📁 internal/          # Go backend modules
│   ├── app/              # Main application logic
│   ├── comm/             # Driver communication
│   ├── logging/          # Logging system
│   ├── process/          # Process management
│   ├── tray/             # System tray integration
│   └── ws/               # WebSocket server
├── main.go               # Application entry point
├── wails.json           # Wails configuration
└── go.mod               # Go dependencies
```

---

## 🌐 Frontend Development

### HTML Structure
The frontend is located in `frontend/index.html`:
```html
<!DOCTYPE html>
<html>
<head>
    <title>NoMoreStealer</title>
    <!-- Modern CSS framework integration -->
</head>
<body>
    <!-- Application UI -->
    <script src="wailsjs/runtime/runtime.js"></script>
    <script src="app.js"></script>
</body>
</html>
```

### JavaScript Integration
Access Go functions from JavaScript:
```javascript
// Import Wails runtime
import { GetSystemInfo } from './wailsjs/go/app/App.js';

// Call Go functions
const systemInfo = await GetSystemInfo();
console.log(systemInfo);
```

### Styling
- Use modern CSS frameworks (Tailwind, Bootstrap, etc.)
- Responsive design for different window sizes
- Dark theme support (matches driver aesthetic)

---

## 🔗 Backend Integration

### Go Application Context
```go
// main.go
func main() {
    app := NewApp()
    
    err := wails.Run(&options.App{
        Title:  "NoMoreStealer",
        Width:  1024,
        Height: 768,
        OnStartup: app.startup,
        // ... other options
    })
}
```

### Driver Communication
The backend communicates with the kernel driver through:
- **Named Pipes** - For control commands
- **Shared Memory** - For high-frequency data
- **Registry Monitoring** - For configuration changes
- **Event Logs** - For driver status

---

## 🎨 UI Features

### Main Dashboard
- 📊 **Real-time Statistics** - Protected files, blocked attempts
- 🔍 **Process Monitor** - Live process trust status
- ⚙️ **Configuration Panel** - Protected paths management
- 📋 **Activity Logs** - Recent driver decisions

### System Tray Integration
- 🔔 **Notifications** - Security alerts and status updates
- ⚡ **Quick Actions** - Enable/disable protection
- 📊 **Status Indicator** - Driver operational status

---

## 🐛 Debugging

### Development Console
```bash
# Enable verbose logging
wails dev -v

# Debug with specific tags
wails dev -tags debug
```

### Browser DevTools
In development mode:
- Right-click → "Inspect Element"
- Or press `F12` to open DevTools
- Console shows both JS and Go logs

### Go Debugging
```go
// Add debug prints
fmt.Printf("Debug: %+v\n", data)

// Use proper logging
log.Printf("Info: Operation completed")
```

---

## 📱 Platform-Specific Features

### Windows Integration
- **System Tray** - Minimize to system tray
- **Windows Notifications** - Native toast notifications
- **Registry Access** - Direct registry manipulation
- **Service Control** - Start/stop driver service

### WebView2 Runtime
Ensure WebView2 is installed:
```bash
# Check WebView2 installation
wails doctor
```

If missing, download from [Microsoft WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/).

---

## 🚀 Deployment

### Installer Creation
```bash
# Build with installer
wails build -nsis

# Custom installer options
wails build -nsis -upx -s
```

### Distribution
- **Portable Executable** - Single `.exe` file
- **NSIS Installer** - Full installation package
- **Auto-updater** - Built-in update mechanism

---

## ❌ Troubleshooting

### Common Issues

#### Wails Command Not Found
```bash
# Ensure Go bin is in PATH
export PATH=$PATH:$(go env GOPATH)/bin

# Or reinstall Wails
go install github.com/wailsapp/wails/v2/cmd/wails@latest
```

#### WebView2 Missing
```bash
# Check system
wails doctor

# Download WebView2 Runtime if needed
```

#### Build Failures
```bash
# Clean build cache
wails build -clean

# Update dependencies
go mod tidy
```

#### Frontend Not Loading
- Check `frontend/` directory exists
- Verify `index.html` is present
- Check browser console for errors

---

## 🔄 Updates and Maintenance

### Updating Wails
```bash
# Update Wails CLI
go install github.com/wailsapp/wails/v2/cmd/wails@latest

# Update project dependencies
go get -u github.com/wailsapp/wails/v2
go mod tidy
```

### Regenerating Bindings
```bash
# Regenerate frontend bindings
wails generate module
```

---

## 📞 Need Help?

- 📚 **Wails Documentation** - [wails.io](https://wails.io)
- 🐛 **Issues** - [Project Issues](../../issues)
- 💬 **Discussions** - [Project Discussions](../../discussions)
- 🤝 **Contributing** - [Contributing Guide](CONTRIBUTING.md)

---

<div align="center">

**🎉 Ready to build amazing desktop applications with Wails! 🎉**

</div>
# ⚡ Wails Frontend Setup Guide

> **Modern desktop application setup for NoMoreStealer management interface**

---

## 🎯 Overview

The NoMoreStealer frontend is built with **Wails v2**, providing a modern desktop application interface for managing the kernel driver. It features:

- 🖥️ **Native Desktop App** - Cross-platform desktop application
- 🌐 **Web Technologies** - HTML, CSS, JavaScript frontend
- 🔗 **Go Backend** - Powerful Go-based application logic
- 📡 **Real-time Communication** - WebSocket integration with the driver
- 🎨 **Modern UI** - Clean, responsive interface

---

## 📋 Prerequisites

### Required Software
- ✅ **Go 1.19+** - [Download Go](https://golang.org/dl/)
- ✅ **Node.js 16+** - [Download Node.js](https://nodejs.org/)
- ✅ **Wails CLI** - Will be installed in setup
- ✅ **Git** - For cloning dependencies

### System Requirements
- **Windows 10/11** (primary support)
- **4GB RAM** minimum
- **WebView2 Runtime** (usually pre-installed on Windows 11)

---

## 🚀 Quick Start

### 1. Install Wails CLI
```bash
go install github.com/wailsapp/wails/v2/cmd/wails@latest
```

### 2. Verify Installation
```bash
wails doctor
```
This command checks your system for all required dependencies.

### 3. Navigate to Frontend Directory
```bash
cd NoMoreStealers_Usermode
```

### 4. Install Dependencies
```bash
# Install Go dependencies
go mod tidy

# Install frontend dependencies (if any)
# npm install  # Currently not needed as per wails.json
```

---

## 🏗️ Development Setup

### Development Mode
Run the application in development mode with hot reload:

```bash
wails dev
```

This will:
- Start the Go backend
- Launch the frontend with hot reload
- Open the application window
- Enable debugging tools

### Development Features
- 🔄 **Hot Reload** - Automatic refresh on code changes
- 🐛 **Debug Mode** - Browser dev tools available
- 📊 **Live Logs** - Real-time application logs
- ⚡ **Fast Iteration** - Instant feedback on changes

---

## 📦 Building for Production

### Build Release Version
```bash
wails build
```

### Build with Custom Options
```bash
# Build with specific flags
wails build -clean -upx -s

# Build for different platforms
wails build -platform windows/amd64
```

### Build Output
The built application will be located in:
```
build/bin/nomorestealers-client.exe
```

---

## ⚙️ Configuration

### Wails Configuration (`wails.json`)
```json
{
  "name": "NoMoreStealers Client",
  "outputfilename": "nomorestealers-client",
  "width": 1024,
  "height": 768,
  "frameless": true,
  "backgroundColour": "#000000"
}
```

### Key Settings Explained
| Setting | Value | Purpose |
|---------|-------|---------|
| `frameless` | `true` | Custom window decorations |
| `width/height` | `1024x768` | Default window size |
| `backgroundColour` | `#000000` | Window background |
| `debounceMS` | `10` | Event debouncing |

---

## 🔧 Application Structure

```
NoMoreStealers_Usermode/
├── 📁 frontend/           # Web frontend
│   ├── index.html         # Main HTML file
│   ├── app.js            # JavaScript logic
│   └── wailsjs/          # Generated Wails bindings
├── 📁 internal/          # Go backend modules
│   ├── app/              # Main application logic
│   ├── comm/             # Driver communication
│   ├── logging/          # Logging system
│   ├── process/          # Process management
│   ├── tray/             # System tray integration
│   └── ws/               # WebSocket server
├── main.go               # Application entry point
├── wails.json           # Wails configuration
└── go.mod               # Go dependencies
```

---

## 🌐 Frontend Development

### HTML Structure
The frontend is located in `frontend/index.html`:
```html
<!DOCTYPE html>
<html>
<head>
    <title>NoMoreStealer</title>
    <!-- Modern CSS framework integration -->
</head>
<body>
    <!-- Application UI -->
    <script src="wailsjs/runtime/runtime.js"></script>
    <script src="app.js"></script>
</body>
</html>
```

### JavaScript Integration
Access Go functions from JavaScript:
```javascript
// Import Wails runtime
import { GetSystemInfo } from './wailsjs/go/app/App.js';

// Call Go functions
const systemInfo = await GetSystemInfo();
console.log(systemInfo);
```

### Styling
- Use modern CSS frameworks (Tailwind, Bootstrap, etc.)
- Responsive design for different window sizes
- Dark theme support (matches driver aesthetic)

---

## 🔗 Backend Integration

### Go Application Context
```go
// main.go
func main() {
    app := NewApp()
    
    err := wails.Run(&options.App{
        Title:  "NoMoreStealer",
        Width:  1024,
        Height: 768,
        OnStartup: app.startup,
        // ... other options
    })
}
```

### Driver Communication
The backend communicates with the kernel driver through:
- **Named Pipes** - For control commands
- **Shared Memory** - For high-frequency data
- **Registry Monitoring** - For configuration changes
- **Event Logs** - For driver status

---

## 🎨 UI Features

### Main Dashboard
- 📊 **Real-time Statistics** - Protected files, blocked attempts
- 🔍 **Process Monitor** - Live process trust status
- ⚙️ **Configuration Panel** - Protected paths management
- 📋 **Activity Logs** - Recent driver decisions

### System Tray Integration
- 🔔 **Notifications** - Security alerts and status updates
- ⚡ **Quick Actions** - Enable/disable protection
- 📊 **Status Indicator** - Driver operational status

---

## 🐛 Debugging

### Development Console
```bash
# Enable verbose logging
wails dev -v

# Debug with specific tags
wails dev -tags debug
```

### Browser DevTools
In development mode:
- Right-click → "Inspect Element"
- Or press `F12` to open DevTools
- Console shows both JS and Go logs

### Go Debugging
```go
// Add debug prints
fmt.Printf("Debug: %+v\n", data)

// Use proper logging
log.Printf("Info: Operation completed")
```

---

## 📱 Platform-Specific Features

### Windows Integration
- **System Tray** - Minimize to system tray
- **Windows Notifications** - Native toast notifications
- **Registry Access** - Direct registry manipulation
- **Service Control** - Start/stop driver service

### WebView2 Runtime
Ensure WebView2 is installed:
```bash
# Check WebView2 installation
wails doctor
```

If missing, download from [Microsoft WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/).

---

## 🚀 Deployment

### Installer Creation
```bash
# Build with installer
wails build -nsis

# Custom installer options
wails build -nsis -upx -s
```

### Distribution
- **Portable Executable** - Single `.exe` file
- **NSIS Installer** - Full installation package
- **Auto-updater** - Built-in update mechanism

---

## ❌ Troubleshooting

### Common Issues

#### Wails Command Not Found
```bash
# Ensure Go bin is in PATH
export PATH=$PATH:$(go env GOPATH)/bin

# Or reinstall Wails
go install github.com/wailsapp/wails/v2/cmd/wails@latest
```

#### WebView2 Missing
```bash
# Check system
wails doctor

# Download WebView2 Runtime if needed
```

#### Build Failures
```bash
# Clean build cache
wails build -clean

# Update dependencies
go mod tidy
```

#### Frontend Not Loading
- Check `frontend/` directory exists
- Verify `index.html` is present
- Check browser console for errors

---

## 🔄 Updates and Maintenance

### Updating Wails
```bash
# Update Wails CLI
go install github.com/wailsapp/wails/v2/cmd/wails@latest

# Update project dependencies
go get -u github.com/wailsapp/wails/v2
go mod tidy
```

### Regenerating Bindings
```bash
# Regenerate frontend bindings
wails generate module
```

---

## 📞 Need Help?

- 📚 **Wails Documentation** - [wails.io](https://wails.io)
- 🐛 **Issues** - [Project Issues](../../issues)
- 💬 **Discussions** - [Project Discussions](../../discussions)
- 🤝 **Contributing** - [Contributing Guide](CONTRIBUTING.md)

---

<div align="center">

**🎉 Ready to build amazing desktop applications with Wails! 🎉**

</div>
