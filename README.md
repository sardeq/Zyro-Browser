# Zyro Browser

**Zyro** is a lightweight, high-performance web browser built from scratch using **C++** and **WebKitGTK**. It features a custom HTML/CSS-based user interface, ensuring speed, security, and a modern aesthetic.

![Version](https://img.shields.io/badge/version-0.0.1-blue)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey)

## 🚀 Features

* **Lightweight Core**: Minimal resource usage compared to major browsers.
* **Privacy First**: Built-in encryption for saved passwords and local data.
* **Custom UI**: The browser interface (New Tab, Settings, History) is rendered with HTML/CSS, allowing for easy theming.
* **Smart Dashboard**: The New Tab page displays real-time **CPU & RAM usage**, favorites, and a search bar.

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

### Prerequisites (Arch Linux)
```bash
sudo pacman -S base-devel cmake gtk3 webkit2gtk libsoup openssl
```

*Note: I have no idea what Im doing, this probably will and does have many issues*
