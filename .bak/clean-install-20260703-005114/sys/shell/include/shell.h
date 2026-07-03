#ifndef _SHELL_H
#define _SHELL_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

/* Shell command types */
enum class CommandType {
    UNKNOWN,
    EXIT,
    ECHO,
    CD,
    LS,
    PWD,
    MKDIR,
    RMDIR,
    REMOVE,
    CAT,
    HELP,
    CLEAR,
    UNAME,
    TIME,
    WHOAMI,
    EXEC,
    KILL,
    PS
};

/* Command structure */
struct Command {
    CommandType type;
    std::string name;
    std::vector<std::string> args;
};

/* Filesystem node */
struct FsNode {
    std::string name;
    std::string path;
    bool is_dir;
    std::map<std::string, std::shared_ptr<FsNode>> children;
    std::string content;
};

/* Shell class */
class ToriginalShell {
public:
    ToriginalShell();
    ~ToriginalShell();
    
    void run(void);
    void execute(const std::string &input);
    
private:
    std::string current_path;
    std::shared_ptr<FsNode> root_fs;
    std::shared_ptr<FsNode> current_node;
    bool running;
    
    /* Command parsing and execution */
    Command parse_command(const std::string &input);
    CommandType identify_command(const std::string &name);
    void execute_command(const Command &cmd);
    
    /* Built-in commands */
    void cmd_echo(const std::vector<std::string> &args);
    void cmd_cd(const std::vector<std::string> &args);
    void cmd_ls(const std::vector<std::string> &args);
    void cmd_pwd(const std::vector<std::string> &args);
    void cmd_mkdir(const std::vector<std::string> &args);
    void cmd_rmdir(const std::vector<std::string> &args);
    void cmd_remove(const std::vector<std::string> &args);
    void cmd_cat(const std::vector<std::string> &args);
    void cmd_help(const std::vector<std::string> &args);
    void cmd_clear(const std::vector<std::string> &args);
    void cmd_uname(const std::vector<std::string> &args);
    void cmd_time(const std::vector<std::string> &args);
    void cmd_whoami(const std::vector<std::string> &args);
    void cmd_exec(const std::vector<std::string> &args);
    void cmd_ps(const std::vector<std::string> &args);
    
    /* Filesystem operations */
    std::shared_ptr<FsNode> resolve_path(const std::string &path);
    std::shared_ptr<FsNode> get_node(const std::string &path);
    void create_directory(const std::string &path);
    void remove_file_or_dir(const std::string &path);
    void list_directory(const std::shared_ptr<FsNode> &node);
    
    /* Utility functions */
    std::vector<std::string> split_string(const std::string &str, char delimiter);
    std::string trim_string(const std::string &str);
    void print_prompt(void);
};

#endif /* _SHELL_H */
