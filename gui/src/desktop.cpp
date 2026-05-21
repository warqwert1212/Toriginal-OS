/*
 * Toriginal OS Desktop Environment
 * Main GUI launcher and window manager
 */

#include "gui_framework.h"
#include "system_apps.h"
#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

using namespace std;

/* Terminal input configuration */
struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    atexit(disableRawMode);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    /* Initialize GUI */
    system("clear");
    cout << "\033]0;Toriginal OS Desktop\007";  /* Set window title */
    
    cout << "\n";
    cout << "╔════════════════════════════════════════════════════════╗\n";
    cout << "║   TORIGINAL OS - DESKTOP ENVIRONMENT v1.0             ║\n";
    cout << "║   Building graphical user interface...                ║\n";
    cout << "╚════════════════════════════════════════════════════════╝\n\n";
    
    /* Initialize desktop */
    Desktop &desktop = Desktop::getInstance();
    desktop.initialize();
    
    /* Create system windows */
    TerminalWindow *terminal = new TerminalWindow(5, 3);
    FileManagerWindow *file_manager = new FileManagerWindow(70, 3);
    TaskManagerWindow *task_manager = new TaskManagerWindow(5, 20);
    ControlPanelWindow *control_panel = new ControlPanelWindow(70, 20);
    
    /* Add windows to desktop */
    desktop.addWindow(terminal);
    desktop.addWindow(file_manager);
    desktop.addWindow(task_manager);
    desktop.addWindow(control_panel);
    
    cout << "\033[2J\033[H";  /* Clear screen */
    cout << COLOR_CYAN << BG_BLUE;
    cout << "╔══════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n";
    cout << "║                        TORIGINAL OS - DESKTOP ENVIRONMENT                                              ║\n";
    cout << "║                                                                                                          ║\n";
    cout << "║  📁 File Manager                                          🖥  System                                     ║\n";
    cout << "║                                                                                                          ║\n";
    cout << "║  🎮 Games                                                 ⚙  Settings                                    ║\n";
    cout << "║                                                                                                          ║\n";
    cout << "║  🔧 Tools                                                 📊 Control Panel                              ║\n";
    cout << "║                                                                                                          ║\n";
    cout << "╚══════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n";
    cout << COLOR_RESET;
    
    /* Render taskbar at bottom */
    cout << "\033[" << SCREEN_HEIGHT << ";0H";
    cout << COLOR_BLUE << BG_BLACK;
    cout << "╔";
    for (int i = 0; i < SCREEN_WIDTH - 2; i++) cout << "═";
    cout << "╗\n";
    
    cout << "║ 🪟 Start Menu  [Terminal] [Files] [Games] [Settings] [Control Panel]";
    for (int i = 0; i < SCREEN_WIDTH - 72; i++) cout << " ";
    cout << "║\n";
    
    cout << "╚";
    for (int i = 0; i < SCREEN_WIDTH - 2; i++) cout << "═";
    cout << "╝" << COLOR_RESET << endl;
    
    cout << "\nDesktop initialized successfully.\n";
    cout << "Available Commands:\n";
    cout << "  - Type 'gui' to launch full GUI\n";
    cout << "  - Type 'terminal' for terminal window\n";
    cout << "  - Type 'filemanager' for file browser\n";
    cout << "  - Type 'taskmanager' for process manager\n";
    cout << "  - Type 'control' for control panel\n";
    cout << "  - Type 'snake' to play snake game\n";
    cout << "  - Type 'exit' to quit\n\n";
    
    string command;
    while (true) {
        cout << "desktop> ";
        getline(cin, command);
        
        if (command == "exit" || command == "quit") {
            break;
        } else if (command == "terminal") {
            cout << "Launching Terminal...\n";
            system("clear");
            cout << terminal << endl;
        } else if (command == "filemanager") {
            cout << "Launching File Manager...\n";
            /* Would open file manager */
        } else if (command == "taskmanager") {
            cout << "Launching Task Manager...\n";
            /* Would open task manager */
        } else if (command == "control") {
            cout << "Launching Control Panel...\n";
            /* Would open control panel */
        } else if (command == "snake") {
            cout << "Launching Snake Game...\n";
            system("build/apps/snake");  /* Run compiled snake game */
        } else if (command == "gui") {
            cout << "Launching full GUI environment...\n";
            desktop.render();
        } else if (command == "help") {
            cout << "Available commands: terminal, filemanager, taskmanager, control, snake, gui, exit\n";
        } else {
            cout << "Unknown command: " << command << "\n";
        }
    }
    
    cout << "Shutting down desktop environment...\n";
    cout << "Goodbye!\n";
    
    delete terminal;
    delete file_manager;
    delete task_manager;
    delete control_panel;
    
    return 0;
}
