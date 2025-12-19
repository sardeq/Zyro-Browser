# 🌐 Zyro Browser

![Zyro Logo](assets/Images/logo_no_text.png)

**Zyro Browser** is an experimental, lightweight web browser built from scratch in **C++** using **WebKitGTK**.  
It focuses on performance, simplicity, and a fully custom user interface rendered with **HTML/CSS** instead of native widgets.

> ⚠️ Early-stage project — expect bugs, missing features, and breaking changes.

![Version](https://img.shields.io/badge/version-0.0.1-blue)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![Language](https://img.shields.io/badge/language-C%2B%2B-brightgreen)
![Engine](https://img.shields.io/badge/engine-WebKitGTK-orange)

---

## ✨ Overview

Zyro is not intended to compete with mainstream browsers like Chrome or Firefox.  
It is a learning-focused project designed to explore:

- Browser architecture and rendering pipelines
- WebKitGTK integration
- Native ↔ Web UI communication
- Performance and resource management in C++

---

## 🚀 Features

- **Lightweight Core**  
  Minimal resource usage compared to major browsers.

- **Privacy-Oriented Design**  
  Local data and saved credentials are encrypted on disk.

- **Smart New Tab Dashboard**  
  Displays:
  - Real-time **CPU & RAM usage**
  - Favorites
  - Search bar
---

## 📥 Installation

Choose the file format that matches your Linux distribution.

### Option 1: Debian, Ubuntu, Linux Mint, Kali (.deb)
*Best for most users on Debian-based systems.*

1.  Download **`zyro_browser.deb`**.
2.  Open your terminal in the folder where you downloaded the file.
3.  Run the following command to install:
    ```bash
    sudo apt install ./zyro_browser.deb
    ```
4.  **Run it**: You can now find **Zyro Browser** in your application menu or run it by typing `zyro` in the terminal.

> **Note**: If you get a "dependencies missing" error, run `sudo apt --fix-broken install` and try again.

### Option 2: Portable Linux Package (.tar.gz)
*Works on Arch Linux, Fedora, Void, Solus, etc.*

1.  Download **`zyro-browser-linux.tar.gz`**.
2.  Extract the archive:
    ```bash
    tar -xzvf zyro-browser-linux.tar.gz
    ```
3.  Enter the extracted folder:
    ```bash
    cd zyro-linux
    ```
4.  **Run it**:
    ```bash
    ./run.sh
    ```
    *(You can also double-click `run.sh` in your file manager if executable permissions are enabled).*

**Windows soon i think**
---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| **Ctrl + T** | Open New Tab |
| **Ctrl + W** | Close Current Tab |
| **Ctrl + R / F5** | Reload Page |
| **Alt + Left** | Go Back |
| **Alt + Right** | Go Forward |
| **Ctrl + L** | Focus URL Bar |
| **Ctrl + PgUp/PgDn** | Switch Tabs |

---
## Build from source

### Prerequisites (Arch Linux)
```bash
sudo pacman -S base-devel cmake gtk3 webkit2gtk libsoup openssl
```

### Build steps
```bash
git clone https://github.com/sardeq/Zyro-Browser.git
cd Zyro-Browser
mkdir build && cd build
cmake ..
make

```
### run
```bash
./ZyroBrowser
```

*Note: I have no idea what Im doing, this probably will and does have many issues*
