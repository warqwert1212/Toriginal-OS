#ifndef SYSTEM_APPS_H
#define SYSTEM_APPS_H

#include "gui_framework.h"
#include <string>
#include <vector>

/* System Terminal */
class TerminalWindow : public Window {
public:
    TerminalWindow(int x, int y);
    virtual ~TerminalWindow();
    
    virtual void render();
    virtual void handleInput(char key);
    void addOutput(const std::string &text);
    
private:
    std::vector<std::string> output_buffer;
    std::string input_buffer;
    int scroll_pos;
};

/* File Manager */
class FileManagerWindow : public Window {
public:
    FileManagerWindow(int x, int y);
    virtual ~FileManagerWindow();
    
    virtual void render();
    virtual void handleInput(char key);
    void setPath(const std::string &path);
    
private:
    std::string current_path;
    std::vector<std::string> files;
    int selected_index;
    void loadDirectory();
};

/* Task Manager */
class TaskManagerWindow : public Window {
public:
    TaskManagerWindow(int x, int y);
    virtual ~TaskManagerWindow();
    
    virtual void render();
    virtual void handleInput(char key);
    void updateProcesses();
    
private:
    struct Process {
        int pid;
        std::string name;
        int memory;
        std::string status;
    };
    
    std::vector<Process> processes;
};

/* Control Panel */
class ControlPanelWindow : public Window {
public:
    ControlPanelWindow(int x, int y);
    virtual ~ControlPanelWindow();
    
    virtual void render();
    virtual void handleInput(char key);
    
private:
    int selected_category;
    void renderSystemInfo();
    void renderNetwork();
    void renderDisplay();
};

#endif /* SYSTEM_APPS_H */
