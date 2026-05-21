# Toriginal OS Shell - User Guide

## Starting the Shell

```bash
./toriginal_shell
```

The shell will display:
```
=== Toriginal OS Shell ===
Type 'help' for available commands

/sys/userpc/~ >
```

## Command Reference

### Navigation Commands

#### `cd <path>`
Change to a different directory.

```bash
/sys/userpc/~ > cd bin
/sys/userpc/~/bin >

/sys/userpc/~/bin > cd ..
/sys/userpc/~ >

/sys/userpc/~ > cd /sys
/sys >
```

#### `pwd`
Print the current working directory path.

```bash
/sys/userpc/~ > pwd
/sys/userpc/~
```

#### `ls`
List the contents of the current directory.

```bash
/sys/userpc/~ > ls
[DIR]  bin/
[DIR]  lib/
[DIR]  tmp/
[FILE] config.txt
```

### File Operations

#### `cat <filename>`
Display the contents of a file.

```bash
/sys/userpc/~ > cat config.txt
[file contents]
```

#### `mkdir <dirname>`
Create a new directory.

```bash
/sys/userpc/~ > mkdir projects
Directory created: projects

/sys/userpc/~ > ls
[DIR]  bin/
[DIR]  projects/
```

#### `rmdir <dirname>`
Remove an empty directory.

```bash
/sys/userpc/~ > rmdir projects
Directory removed: projects
```

#### `rm <filename>`
Remove a file.

```bash
/sys/userpc/~ > rm oldfile.txt
Removed: oldfile.txt
```

### System Commands

#### `echo <text>`
Print text to the screen.

```bash
/sys/userpc/~ > echo Hello freeNT!
Hello freeNT!
```

#### `clear`
Clear the terminal screen.

```bash
/sys/userpc/~ > clear
```

#### `uname`
Display system information.

```bash
/sys/userpc/~ > uname
freeNT 1.0.0 (Toriginal OS x86_64)
```

#### `time`
Show the current date and time.

```bash
/sys/userpc/~ > time
Wed May 21 12:34:56 2025
```

#### `whoami`
Display the current user.

```bash
/sys/userpc/~ > whoami
user
```

#### `ps`
List running processes.

```bash
/sys/userpc/~ > ps
PID	NAME	STATE
1	kernel	RUNNING
2	shell	RUNNING
```

### Program Execution

#### `exec <program.exe>`
Execute a Windows PE executable.

```bash
/sys/userpc/~ > exec myprogram.exe
Executing: myprogram.exe
[Syscall to kernel: Load and execute program]
```

#### `exec <program.trp>`
Execute a Toriganal Runtime Package.

```bash
/sys/userpc/~ > exec myapp.trp
Executing: myapp.trp
[Syscall to kernel: Load and execute program]
```

### Information Commands

#### `help`
Display all available commands.

```bash
/sys/userpc/~ > help
Toriginal OS Shell - Available Commands:
...
```

#### `exit`
Exit the shell and return to the system.

```bash
/sys/userpc/~ > exit
Exiting Toriginal OS Shell...
```

## File System Structure

The default filesystem structure is:

```
/sys/userpc/~
├── bin/          # Executable programs
├── lib/          # Libraries
├── tmp/          # Temporary files
├── home/         # User home
│   └── user/
└── config/       # Configuration files
```

## Path Syntax

The shell supports both absolute and relative paths:

```bash
# Absolute path (from root)
/sys/userpc/~ > cd /sys

# Relative path
/sys/userpc/~ > cd bin

# Parent directory
/sys/userpc/~/bin > cd ..

# Root directory
/sys/userpc/~ > cd /
```

## Example Workflow

```bash
# Start shell
$ ./toriginal_shell

# Create working directory
/sys/userpc/~ > mkdir myproject
Directory created: myproject

# Enter directory
/sys/userpc/~ > cd myproject
/sys/userpc/~/myproject >

# Create subdirectories
/sys/userpc/~/myproject > mkdir src
Directory created: src
/sys/userpc/~/myproject > mkdir bin
Directory created: bin

# List contents
/sys/userpc/~/myproject > ls
[DIR]  bin/
[DIR]  src/

# Execute a program
/sys/userpc/~/myproject > exec bin/myapp.exe
Executing: myapp.exe

# Exit shell
/sys/userpc/~/myproject > exit
Exiting Toriginal OS Shell...
```

## Tips & Tricks

1. **Fast Navigation**: Use `cd /sys/userpc/~` to quickly return home
2. **Directory Listing**: `ls` shows both files and directories with indicators
3. **Autocomplete**: Type the first few characters and press Tab (if available)
4. **Command History**: Use arrow keys to cycle through recent commands
5. **Absolute Paths**: Always start with `/` for absolute paths

## Limitations

- Filenames are limited to 256 characters
- Maximum 1024 open files per session
- No wildcards or globbing patterns
- No pipe or redirection operators
- Case-sensitive paths

## Troubleshooting

### "Command not found"
- Check spelling
- Use `help` to see available commands

### "File not found"
- Check current directory with `pwd`
- Use `ls` to list available files
- Verify path is correct (case-sensitive)

### "Directory not empty"
- Remove files from directory first with `rm`
- Then use `rmdir`

### "Is a directory"
- Use `ls` instead of `cat` to view directory contents
- Use `rmdir` instead of `rm` to remove directories
