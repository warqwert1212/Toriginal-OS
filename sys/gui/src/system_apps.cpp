#include "system_apps.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>

using namespace std;

/* ============================================================================ */
/* Terminal Window */
/* ============================================================================ */

TerminalWindow::TerminalWindow(int x, int y)
    : Window("Terminal", x, y, 60, 15), scroll_pos(0) {
    addOutput("Welcome to Toriginal OS Terminal");
    addOutput("Type 'help' for commands");
    addOutput("");
}

TerminalWindow::~TerminalWindow() {}

void TerminalWindow::render() {
    cout << "\033[" << y << ";" << x << "H";
    cout << COLOR_WHITE << BG_BLUE;
    
    /* Draw window */
    cout << "╔";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╗" << endl;
    
    /* Draw title */
    cout << "║ Terminal                                               ║" << endl;
    cout << "╠" << string(width - 2, '═') << "╣" << endl;
    
    /* Draw output */
    int start = max(0, (int)output_buffer.size() - (height - 5));
    for (int i = start; i < (int)output_buffer.size(); i++) {
        cout << "║ " << output_buffer[i];
        cout << string(width - 3 - output_buffer[i].length(), ' ') << "║" << endl;
    }
    
    /* Draw input line */
    cout << "║> " << input_buffer;
    cout << string(width - 4 - input_buffer.length(), ' ') << "║" << endl;
    
    /* Draw bottom border */
    cout << "╚";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET;
}

void TerminalWindow::handleInput(char key) {
    if (key == '\n') {
        addOutput("$ " + input_buffer);
        if (input_buffer == "clear") {
            output_buffer.clear();
        } else if (input_buffer == "help") {
            addOutput("Available commands: help, clear, exit, ls, pwd, cd");
        }
        input_buffer = "";
    } else if (key == '\b' && !input_buffer.empty()) {
        input_buffer.pop_back();
    } else if (key >= 32 && key <= 126) {
        input_buffer += key;
    }
}

void TerminalWindow::addOutput(const string &text) {
    output_buffer.push_back(text);
}

/* ============================================================================ */
/* File Manager */
/* ============================================================================ */

FileManagerWindow::FileManagerWindow(int x, int y)
    : Window("File Manager", x, y, 50, 18), current_path("/sys/userpc"), selected_index(0) {
    loadDirectory();
}

FileManagerWindow::~FileManagerWindow() {}

void FileManagerWindow::render() {
    cout << "\033[" << y << ";" << x << "H";
    cout << COLOR_WHITE << BG_BLUE;
    
    /* Draw window */
    cout << "╔";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╗" << endl;
    
    /* Draw title and path */
    cout << "║ File Manager - " << current_path;
    cout << string(width - 17 - current_path.length(), ' ') << "║" << endl;
    cout << "╠" << string(width - 2, '═') << "╣" << endl;
    
    /* Draw file list */
    int max_files = height - 5;
    for (int i = 0; i < max_files && i < (int)files.size(); i++) {
        cout << "║ ";
        if (i == selected_index) {
            cout << COLOR_BLACK << BG_CYAN << "►" << files[i] << COLOR_RESET << COLOR_WHITE;
        } else {
            cout << " " << files[i];
        }
        cout << string(width - 5 - files[i].length(), ' ') << "║" << endl;
    }
    
    /* Fill remaining space */
    for (int i = files.size(); i < max_files; i++) {
        cout << "║" << string(width - 2, ' ') << "║" << endl;
    }
    
    /* Draw bottom border */
    cout << "╚";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET;
}

void FileManagerWindow::handleInput(char key) {
    if (key == 'w' || key == 'k') {
        if (selected_index > 0) selected_index--;
    } else if (key == 's' || key == 'j') {
        if (selected_index < (int)files.size() - 1) selected_index++;
    }
}

void FileManagerWindow::setPath(const string &path) {
    current_path = path;
    loadDirectory();
}

void FileManagerWindow::loadDirectory() {
    files.clear();
    DIR *dir = opendir(current_path.c_str());
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] != '.') {
                files.push_back(entry->d_name);
            }
        }
        closedir(dir);
    }
}

/* ============================================================================ */
/* Task Manager */
/* ============================================================================ */

TaskManagerWindow::TaskManagerWindow(int x, int y)
    : Window("Task Manager", x, y, 60, 18) {
    updateProcesses();
}

TaskManagerWindow::~TaskManagerWindow() {}

void TaskManagerWindow::render() {
    cout << "\033[" << y << ";" << x << "H";
    cout << COLOR_WHITE << BG_BLUE;
    
    /* Draw window */
    cout << "╔";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╗" << endl;
    
    /* Draw title */
    cout << "║ Task Manager                                           ║" << endl;
    cout << "╠════════════════════════════════════════════════════════╣" << endl;
    cout << "║ PID  Name                      Memory  Status          ║" << endl;
    cout << "╠════════════════════════════════════════════════════════╣" << endl;
    
    /* Draw processes */
    int max_procs = height - 7;
    for (int i = 0; i < max_procs && i < (int)processes.size(); i++) {
        printf("║ %-4d %-30s %-7d %s\n",
               processes[i].pid,
               processes[i].name.c_str(),
               processes[i].memory,
               processes[i].status.c_str());
    }
    
    /* Fill remaining space */
    for (int i = processes.size(); i < max_procs; i++) {
        cout << "║" << string(width - 2, ' ') << "║" << endl;
    }
    
    /* Draw bottom border */
    cout << "╚";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET;
}

void TaskManagerWindow::handleInput(char key) {
    if (key == 'r') {
        updateProcesses();
    }
}

void TaskManagerWindow::updateProcesses() {
    processes.clear();
    
    /* Add some sample processes */
    processes.push_back({1, "kernel", 2048, "Running"});
    processes.push_back({2, "shell", 512, "Running"});
    processes.push_back({3, "gui", 1024, "Running"});
}

/* ============================================================================ */
/* Control Panel */
/* ============================================================================ */

ControlPanelWindow::ControlPanelWindow(int x, int y)
    : Window("Control Panel", x, y, 55, 20), selected_category(0) {}

ControlPanelWindow::~ControlPanelWindow() {}

void ControlPanelWindow::render() {
    cout << "\033[" << y << ";" << x << "H";
    cout << COLOR_WHITE << BG_BLUE;
    
    /* Draw window */
    cout << "╔";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╗" << endl;
    
    cout << "║ Control Panel                                        ║" << endl;
    cout << "╠═══════════════════════════════════════════════════════╣" << endl;
    
    /* Categories */
    cout << "║ System Information  Network  Display  Sound  Power   ║" << endl;
    cout << "╠═══════════════════════════════════════════════════════╣" << endl;
    
    if (selected_category == 0) {
        renderSystemInfo();
    } else if (selected_category == 1) {
        renderNetwork();
    } else if (selected_category == 2) {
        renderDisplay();
    }
}

void ControlPanelWindow::handleInput(char key) {
    if (key == 'a') {
        selected_category = (selected_category - 1 + 3) % 3;
    } else if (key == 'd') {
        selected_category = (selected_category + 1) % 3;
    }
}

void ControlPanelWindow::renderSystemInfo() {
    cout << "║ OS Name: Toriginal OS (freeNT Kernel)               ║" << endl;
    cout << "║ Version: 1.0.0                                      ║" << endl;
    cout << "║ Architecture: x86-64                                ║" << endl;
    cout << "║ Processor: Intel/AMD Compatible                     ║" << endl;
    cout << "║ RAM: 4096 MB                                        ║" << endl;
    cout << "║ Storage: 512 GB                                     ║" << endl;
    cout << "║ Up Time: 2 hours 34 minutes                         ║" << endl;
    for (int i = 0; i < height - 14; i++) {
        cout << "║" << string(width - 2, ' ') << "║" << endl;
    }
    cout << "╚";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET;
}

void ControlPanelWindow::renderNetwork() {
    cout << "║ Network Status: Connected                           ║" << endl;
    cout << "║ IP Address: 192.168.1.100                           ║" << endl;
    cout << "║ Gateway: 192.168.1.1                                ║" << endl;
    cout << "║ WiFi: Enabled                                       ║" << endl;
    cout << "║ Signal Strength: Excellent                          ║" << endl;
    cout << "║ Downloads: 542 MB/s                                 ║" << endl;
    cout << "║ Uploads: 128 MB/s                                   ║" << endl;
    for (int i = 0; i < height - 14; i++) {
        cout << "║" << string(width - 2, ' ') << "║" << endl;
    }
    cout << "╚";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET;
}

void ControlPanelWindow::renderDisplay() {
    cout << "║ Resolution: 1920x1080                               ║" << endl;
    cout << "║ Refresh Rate: 60 Hz                                 ║" << endl;
    cout << "║ Brightness: 100%                                    ║" << endl;
    cout << "║ Color Depth: 32-bit                                 ║" << endl;
    cout << "║ Display Mode: Extended                              ║" << endl;
    cout << "║ GPU: NVIDIA GeForce GTX 1080 Ti                     ║" << endl;
    cout << "║ VRAM: 12 GB                                         ║" << endl;
    for (int i = 0; i < height - 14; i++) {
        cout << "║" << string(width - 2, ' ') << "║" << endl;
    }
    cout << "╚";
    for (int i = 0; i < width - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET;
}
