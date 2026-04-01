#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include "../include/syscalls.h"
#include "../include/path_resolver.h"
#include "../include/process.h"

// Forward declarations for syscall functions
extern "C" {
    uint32_t syscall_exec(const char* filename, char** argv);
    int syscall_fork();
    int syscall_wait(uint32_t pid);
    void syscall_exit(int status);
    int syscall_read(int fd, char* buf, int count);
    int syscall_write(int fd, const char* buf, int count);
}

// Shell command structure
typedef struct {
    char command[256];
    char args[512];
} ShellCommand;

// Parse and execute shell commands
int execute_command(const char* cmd) {
    if (!cmd || strlen(cmd) == 0) return 0;
    
    // Check if it's a path-based system call
    if (cmd[0] == '/') {
        // Path-based calling convention
        ResolvedPath resolved;
        
        if (resolve_system_path(cmd, &resolved) == 0) {
            if (resolved.type == PATH_SYS) {
                // System function call
                if (strcmp(resolved.command, "exec") == 0) {
                    std::cout << "Executing: " << resolved.target << std::endl;
                    if (resolved.target) {
                        char* argv[] = {resolved.target, NULL};
                        syscall_exec(resolved.target, argv);
                    }
                }
                else if (strcmp(resolved.command, "fork") == 0) {
                    std::cout << "Forking process..." << std::endl;
                    int child_pid = syscall_fork();
                    std::cout << "Child PID: " << child_pid << std::endl;
                }
                else if (strcmp(resolved.command, "wait") == 0) {
                    if (resolved.target) {
                        uint32_t pid = atoi(resolved.target);
                        std::cout << "Waiting for PID: " << pid << std::endl;
                        syscall_wait(pid);
                    }
                }
                else if (strcmp(resolved.command, "exit") == 0) {
                    int status = resolved.target ? atoi(resolved.target) : 0;
                    std::cout << "Exiting with status: " << status << std::endl;
                    syscall_exit(status);
                }
            }
            else if (resolved.type == PATH_BIN) {
                // Binary execution
                std::cout << "Executing binary: " << resolved.full_path << std::endl;
                char* argv[] = {resolved.full_path, NULL};
                syscall_exec(resolved.full_path, argv);
            }
        }
        
        free_resolved_path(&resolved);
        return 0;
    }
    
    // Standard shell commands
    if (strcmp(cmd, "help") == 0) {
        std::cout << "\n=== Toriganal OS Shell ===" << std::endl;
        std::cout << "System calls use '/' convention:" << std::endl;
        std::cout << "  /sys/exec <filename>    - Execute a program" << std::endl;
        std::cout << "  /sys/fork               - Create new process" << std::endl;
        std::cout << "  /sys/wait <pid>         - Wait for process" << std::endl;
        std::cout << "  /sys/exit <status>      - Exit shell" << std::endl;
        std::cout << "  /bin/<program>          - Run binary from /bin directory" << std::endl;
        std::cout << "\nOther commands:" << std::endl;
        std::cout << "  help                    - Show this help message" << std::endl;
        std::cout << "  clear                   - Clear screen" << std::endl;
        std::cout << "  exit                    - Exit shell" << std::endl;
        return 0;
    }
    
    if (strcmp(cmd, "clear") == 0) {
        std::cout << "\033[2J\033[1;1H";  // ANSI clear screen
        return 0;
    }
    
    if (strcmp(cmd, "exit") == 0) {
        std::cout << "Goodbye!" << std::endl;
        return -1;  // Signal to exit
    }
    
    std::cout << "Unknown command: " << cmd << std::endl;
    std::cout << "Type 'help' for available commands." << std::endl;
    
    return 0;
}

// Main shell loop
void shell_loop() {
    char input[512];
    
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   TORIGANAL OS SHELL v1.0              ║" << std::endl;
    std::cout << "║   Type 'help' for command list         ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    
    while (true) {
        std::cout << "toriganal> ";
        std::cout.flush();
        
        if (!std::cin.getline(input, sizeof(input))) {
            break;
        }
        
        // Trim whitespace
        std::string cmd(input);
        size_t start = cmd.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        
        cmd = cmd.substr(start);
        size_t end = cmd.find_last_not_of(" \t");
        cmd = cmd.substr(0, end + 1);
        
        // Execute command
        int result = execute_command(cmd.c_str());
        if (result < 0) break;
        
        std::cout << std::endl;
    }
}

int main() {
    shell_loop();
    return 0;
}