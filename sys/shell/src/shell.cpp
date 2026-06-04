#include "shell.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <cstring>

using namespace std;

ToriginalShell::ToriginalShell() 
    : current_path("/sys/userpc/~"), running(true) {
    
    /* Initialize filesystem hierarchy */
    root_fs = make_shared<FsNode>();
    root_fs->name = "/";
    root_fs->path = "/";
    root_fs->is_dir = true;
    
    /* Create /sys directory */
    auto sys_dir = make_shared<FsNode>();
    sys_dir->name = "sys";
    sys_dir->path = "/sys";
    sys_dir->is_dir = true;
    root_fs->children["sys"] = sys_dir;
    
    /* Create /sys/userpc directory */
    auto userpc_dir = make_shared<FsNode>();
    userpc_dir->name = "userpc";
    userpc_dir->path = "/sys/userpc";
    userpc_dir->is_dir = true;
    sys_dir->children["userpc"] = userpc_dir;
    
    /* Set current directory */
    current_node = userpc_dir;
}

ToriginalShell::~ToriginalShell() {
    /* Cleanup */
}

void ToriginalShell::run(void) {
    cout << "=== Toriginal OS Shell ===" << endl;
    cout << "Type 'help' for available commands" << endl;
    cout << endl;
    
    string input;
    while (running) {
        print_prompt();
        getline(cin, input);
        
        if (!input.empty()) {
            execute(input);
        }
    }
}

void ToriginalShell::print_prompt(void) {
    cout << current_path << " > ";
}

vector<string> ToriginalShell::split_string(const string &str, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream iss(str);
    
    while (getline(iss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

string ToriginalShell::trim_string(const string &str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

Command ToriginalShell::parse_command(const string &input) {
    Command cmd;
    cmd.type = CommandType::UNKNOWN;
    
    vector<string> tokens = split_string(input, ' ');
    if (tokens.empty())
        return cmd;
    
    cmd.name = tokens[0];
    cmd.type = identify_command(cmd.name);
    
    for (size_t i = 1; i < tokens.size(); i++) {
        cmd.args.push_back(tokens[i]);
    }
    
    return cmd;
}

CommandType ToriginalShell::identify_command(const string &name) {
    if (name == "exit") return CommandType::EXIT;
    if (name == "echo") return CommandType::ECHO;
    if (name == "cd") return CommandType::CD;
    if (name == "ls") return CommandType::LS;
    if (name == "pwd") return CommandType::PWD;
    if (name == "mkdir") return CommandType::MKDIR;
    if (name == "rmdir") return CommandType::RMDIR;
    if (name == "rm") return CommandType::REMOVE;
    if (name == "cat") return CommandType::CAT;
    if (name == "help") return CommandType::HELP;
    if (name == "clear") return CommandType::CLEAR;
    if (name == "uname") return CommandType::UNAME;
    if (name == "time") return CommandType::TIME;
    if (name == "whoami") return CommandType::WHOAMI;
    if (name == "exec") return CommandType::EXEC;
    if (name == "kill") return CommandType::KILL;
    if (name == "ps") return CommandType::PS;
    
    return CommandType::UNKNOWN;
}

void ToriginalShell::execute(const string &input) {
    Command cmd = parse_command(input);
    execute_command(cmd);
}

void ToriginalShell::execute_command(const Command &cmd) {
    switch (cmd.type) {
        case CommandType::EXIT:
            running = false;
            cout << "Exiting Toriginal OS Shell..." << endl;
            break;
        case CommandType::ECHO:
            cmd_echo(cmd.args);
            break;
        case CommandType::CD:
            cmd_cd(cmd.args);
            break;
        case CommandType::LS:
            cmd_ls(cmd.args);
            break;
        case CommandType::PWD:
            cmd_pwd(cmd.args);
            break;
        case CommandType::MKDIR:
            cmd_mkdir(cmd.args);
            break;
        case CommandType::RMDIR:
            cmd_rmdir(cmd.args);
            break;
        case CommandType::REMOVE:
            cmd_remove(cmd.args);
            break;
        case CommandType::CAT:
            cmd_cat(cmd.args);
            break;
        case CommandType::HELP:
            cmd_help(cmd.args);
            break;
        case CommandType::CLEAR:
            cmd_clear(cmd.args);
            break;
        case CommandType::UNAME:
            cmd_uname(cmd.args);
            break;
        case CommandType::TIME:
            cmd_time(cmd.args);
            break;
        case CommandType::WHOAMI:
            cmd_whoami(cmd.args);
            break;
        case CommandType::EXEC:
            cmd_exec(cmd.args);
            break;
        case CommandType::PS:
            cmd_ps(cmd.args);
            break;
        default:
            cout << "Command not found: " << cmd.name << endl;
            break;
    }
}

void ToriginalShell::cmd_echo(const vector<string> &args) {
    for (size_t i = 0; i < args.size(); i++) {
        cout << args[i];
        if (i < args.size() - 1)
            cout << " ";
    }
    cout << endl;
}

void ToriginalShell::cmd_cd(const vector<string> &args) {
    if (args.empty()) {
        current_path = "/sys/userpc/~";
        current_node = root_fs->children["sys"]->children["userpc"];
    } else {
        string path = args[0];
        if (path == "..") {
            /* Go up one directory */
            size_t last_slash = current_path.rfind('/');
            if (last_slash != string::npos && last_slash > 0) {
                current_path = current_path.substr(0, last_slash);
            }
        } else if (path == "/") {
            current_path = "/";
            current_node = root_fs;
        } else if (path[0] == '/') {
            /* Absolute path */
            current_path = path;
            current_node = resolve_path(path);
        } else {
            /* Relative path */
            if (current_path == "/")
                current_path += path;
            else
                current_path += "/" + path;
            current_node = resolve_path(path);
        }
    }
}

void ToriginalShell::cmd_ls(const vector<string> &args) {
    if (current_node) {
        list_directory(current_node);
    } else {
        cout << "Error: Cannot list directory" << endl;
    }
}

void ToriginalShell::cmd_pwd(const vector<string> &args) {
    cout << current_path << endl;
}

void ToriginalShell::cmd_mkdir(const vector<string> &args) {
    if (args.empty()) {
        cout << "Usage: mkdir <directory_name>" << endl;
        return;
    }
    
    string dir_name = args[0];
    
    if (current_node) {
        auto new_dir = make_shared<FsNode>();
        new_dir->name = dir_name;
        new_dir->path = current_path + "/" + dir_name;
        new_dir->is_dir = true;
        current_node->children[dir_name] = new_dir;
        cout << "Directory created: " << dir_name << endl;
    } else {
        cout << "Error: Current directory is invalid" << endl;
    }
}

void ToriginalShell::cmd_rmdir(const vector<string> &args) {
    if (args.empty()) {
        cout << "Usage: rmdir <directory_name>" << endl;
        return;
    }
    
    string dir_name = args[0];
    
    if (current_node && current_node->children.count(dir_name)) {
        auto node = current_node->children[dir_name];
        if (!node->is_dir) {
            cout << "Error: Not a directory" << endl;
            return;
        }
        
        if (!node->children.empty()) {
            cout << "Error: Directory not empty" << endl;
            return;
        }
        
        current_node->children.erase(dir_name);
        cout << "Directory removed: " << dir_name << endl;
    } else {
        cout << "Error: Directory not found" << endl;
    }
}

void ToriginalShell::cmd_remove(const vector<string> &args) {
    if (args.empty()) {
        cout << "Usage: rm <file_name>" << endl;
        return;
    }
    
    string file_name = args[0];
    remove_file_or_dir(file_name);
}

void ToriginalShell::cmd_cat(const vector<string> &args) {
    if (args.empty()) {
        cout << "Usage: cat <file_name>" << endl;
        return;
    }
    
    string file_name = args[0];
    
    if (current_node && current_node->children.count(file_name)) {
        auto node = current_node->children[file_name];
        if (node->is_dir) {
            cout << "Error: Is a directory" << endl;
            return;
        }
        cout << node->content;
        if (!node->content.empty() && node->content.back() != '\n')
            cout << endl;
    } else {
        cout << "Error: File not found" << endl;
    }
}

void ToriginalShell::cmd_help(const vector<string> &args) {
    cout << "Toriginal OS Shell - Available Commands:" << endl;
    cout << endl;
    cout << "  exit        - Exit the shell" << endl;
    cout << "  echo <text> - Print text" << endl;
    cout << "  cd <path>   - Change directory (.. to go up)" << endl;
    cout << "  ls          - List directory contents" << endl;
    cout << "  pwd         - Print working directory" << endl;
    cout << "  mkdir <dir> - Create directory" << endl;
    cout << "  rmdir <dir> - Remove empty directory" << endl;
    cout << "  rm <file>   - Remove file" << endl;
    cout << "  cat <file>  - Display file contents" << endl;
    cout << "  clear       - Clear screen" << endl;
    cout << "  uname       - Print system information" << endl;
    cout << "  time        - Print current time" << endl;
    cout << "  whoami      - Print current user" << endl;
    cout << "  ps          - List running processes" << endl;
    cout << "  exec <file> - Execute a program (.exe or .trp)" << endl;
    cout << "  help        - Show this help message" << endl;
}

void ToriginalShell::cmd_clear(const vector<string> &args) {
    system("clear");
}

void ToriginalShell::cmd_uname(const vector<string> &args) {
    cout << "freeNT 1.0.0 (Toriginal OS x86_64)" << endl;
}

void ToriginalShell::cmd_time(const vector<string> &args) {
    time_t now = time(nullptr);
    cout << ctime(&now);
}

void ToriginalShell::cmd_whoami(const vector<string> &args) {
    cout << "user" << endl;
}

void ToriginalShell::cmd_exec(const vector<string> &args) {
    if (args.empty()) {
        cout << "Usage: exec <executable>" << endl;
        return;
    }
    
    string exec_name = args[0];
    
    /* Check for .exe or .trp extension */
    if (exec_name.length() >= 4) {
        string ext = exec_name.substr(exec_name.length() - 4);
        if (ext == ".exe" || ext == ".trp") {
            cout << "Executing: " << exec_name << endl;
            cout << "[Syscall to kernel: Load and execute program]" << endl;
            /* In real implementation, would call kernel syscall to load/exec */
        } else {
            cout << "Error: Only .exe and .trp files are supported" << endl;
        }
    } else {
        cout << "Error: Invalid executable" << endl;
    }
}

void ToriginalShell::cmd_ps(const vector<string> &args) {
    cout << "PID\tNAME\t\tSTATE" << endl;
    cout << "1\tkernel\t\tRUNNING" << endl;
    cout << "2\tshell\t\tRUNNING" << endl;
    cout << "[More processes would be listed from kernel]" << endl;
}

void ToriginalShell::list_directory(const shared_ptr<FsNode> &node) {
    if (!node) return;
    
    for (const auto &pair : node->children) {
        const auto &child = pair.second;
        if (child->is_dir) {
            cout << "[DIR]  " << child->name << "/" << endl;
        } else {
            cout << "[FILE] " << child->name << endl;
        }
    }
}

shared_ptr<FsNode> ToriginalShell::resolve_path(const string &path) {
    if (path == "/" || path.empty())
        return root_fs;
    
    vector<string> parts = split_string(path, '/');
    auto current = root_fs;
    
    for (const auto &part : parts) {
        if (part.empty() || part == "~")
            continue;
        
        if (current->children.count(part)) {
            current = current->children[part];
        } else {
            return nullptr;
        }
    }
    
    return current;
}

shared_ptr<FsNode> ToriginalShell::get_node(const string &path) {
    return resolve_path(path);
}

void ToriginalShell::create_directory(const string &path) {
    auto node = make_shared<FsNode>();
    node->name = path;
    node->path = current_path + "/" + path;
    node->is_dir = true;
    
    if (current_node) {
        current_node->children[path] = node;
    }
}

void ToriginalShell::remove_file_or_dir(const string &path) {
    if (current_node && current_node->children.count(path)) {
        current_node->children.erase(path);
        cout << "Removed: " << path << endl;
    } else {
        cout << "Error: File or directory not found" << endl;
    }
}
