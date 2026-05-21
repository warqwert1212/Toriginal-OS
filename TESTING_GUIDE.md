# Toriginal OS - Comprehensive Testing & Bug Checking Guide

## Overview
This document provides comprehensive testing procedures for all components of Toriginal OS v1.0, ensuring quality and stability across the entire system.

## Pre-Testing Checklist

- [ ] Cleaned previous build: `make distclean`
- [ ] Fresh rebuild: `make all`
- [ ] All binaries present in `/build` directory
- [ ] No compiler warnings/errors
- [ ] System time and date set correctly
- [ ] Terminal supports ANSI colors

## Phase 1: Build System Testing

### Test 1.1: CMake Configuration
```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
```
**Expected Results:**
- CMake configuration completes without errors
- All subdirectories found (freeNT, shell, pkgman, gui, apps, drivers)
- All header files located
- Target dependencies resolved

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 1.2: Compilation Without Errors
```bash
make clean && make -j$(nproc) 2>&1 | grep -i error
```
**Expected Results:**
- No error messages in build output
- All targets compile successfully
- Warnings are acceptable but should be reviewed

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 1.3: Artifact Generation
```bash
find build -type f -executable | sort
```
**Expected Results:**
- freeNT (kernel)
- toriginal_shell (shell)
- desktop (GUI launcher)
- snake (game)
- libpkgman.a (package manager)
- libgui_framework.a (GUI framework)
- libnet_driver.a (network driver)

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 2: Kernel Testing

### Test 2.1: Kernel Binary Validation
```bash
file build/freeNT
readelf -h build/freeNT 2>/dev/null || echo "ELF validation passed"
```
**Expected Results:**
- Binary is ELF 64-bit executable
- Architecture is x86-64
- Entry point is properly defined

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 2.2: Kernel Symbols
```bash
nm build/freeNT | grep -E "kernel_main|boot|paging" | head -10
```
**Expected Results:**
- Key kernel functions present as symbols
- No undefined symbols
- Proper section alignment

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 3: Shell Testing

### Test 3.1: Shell Startup
```bash
echo "exit" | ./build/toriginal_shell
```
**Expected Results:**
- Shell displays welcome banner
- Shows prompt: `/sys/userpc/~ >`
- Exits cleanly without errors
- No memory leaks/segfaults

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 3.2: Basic Commands
```bash
./build/toriginal_shell << 'EOF'
pwd
ls
help
exit
EOF
```
**Expected Results:**
- `pwd` shows current directory
- `ls` lists files without errors
- `help` displays command list
- All commands execute without crashes

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 3.3: Directory Navigation
```bash
./build/toriginal_shell << 'EOF'
mkdir test_dir
cd test_dir
pwd
cd ..
rmdir test_dir
exit
EOF
```
**Expected Results:**
- Directory created successfully
- Navigation works correctly
- Directory removed without errors
- pwd shows correct path after each command

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 3.4: File Operations
```bash
./build/toriginal_shell << 'EOF'
echo "test content" > test.txt
cat test.txt
rm test.txt
exit
EOF
```
**Expected Results:**
- File created with content
- cat displays file content
- File removed successfully

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 3.5: Shell Command Parsing
```bash
./build/toriginal_shell << 'EOF'
echo hello world
echo    multiple     spaces
echo "quoted text with spaces"
exit
EOF
```
**Expected Results:**
- Multiple arguments parsed correctly
- Spaces handled appropriately
- Quoted strings work
- Output is correct

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 4: Package Manager Testing

### Test 4.1: Package Manager Initialization
```bash
./build/gui/desktop << 'EOF'
help
exit
EOF
```
**Expected Results:**
- Package manager initializes without errors
- Repository directories created
- No file permission errors

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 4.2: Package Creation
**Manual Testing:**
- Create test package with `pkg_create("test-pkg", "1.0", "Test package")`
- Verify package metadata stored correctly
- Check package.db exists

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 4.3: Package Installation
**Manual Testing:**
- Install created test package
- Verify directory `/sys/userpc/packages/test-pkg` created
- Check installation status updates

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 4.4: Package Listing
**Manual Testing:**
- List all packages
- Verify format display
- Check status indicators (installed/not installed)

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 4.5: Package Search
**Manual Testing:**
- Search for "test" in package list
- Search for non-existent package
- Verify correct results returned

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 5: Snake Game Testing

### Test 5.1: Game Startup
```bash
timeout 3 ./build/apps/games/snake << 'EOF'

EOF
```
**Expected Results:**
- Game displays welcome screen
- Instructions shown clearly
- No crashes on startup
- Proper Unicode graphics render

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 5.2: Game Initialization
**Manual Testing:**
- Start game
- Verify snake appears at center
- Food appears at random location
- Score shown as 0
- Length shown as 3

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 5.3: Game Controls
**Manual Testing:**
- Test W key (move up)
- Test S key (move down)
- Test A key (move left)
- Test D key (move right)
- Snake should change direction

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 5.4: Collision Detection
**Manual Testing:**
- Move snake into wall - game ends
- Move snake into self - game ends
- Verify game over message displayed
- Check final score shown

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 5.5: Food Collection
**Manual Testing:**
- Move snake to food
- Score should increase by 10
- Snake length should increase by 1
- New food appears randomly
- Game continues

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 5.6: Game Performance
**Manual Testing:**
- Play for 5 minutes
- Monitor for lag or stuttering
- Check memory usage
- Verify consistent frame rate

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 6: GUI Framework Testing

### Test 6.1: GUI Compilation
```bash
find build -name "*gui*" -type f
```
**Expected Results:**
- desktop executable found
- libgui_framework.a library found
- No undefined references

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 6.2: Window Rendering
**Manual Testing:**
- Launch desktop
- Verify windows render with borders
- Check title bars display correctly
- Verify blue color scheme

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 6.3: Component Rendering
**Manual Testing:**
- Check buttons display correctly
- Verify labels show text
- Test textbox rendering
- Check colors and formatting

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 6.4: Desktop Environment
```bash
timeout 5 ./build/gui/desktop << 'EOF'
gui
help
exit
EOF
```
**Expected Results:**
- Desktop environment launches
- Wallpaper displays (cyan background)
- Taskbar shows at bottom
- Application icons visible
- Commands are recognized

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 7: System Applications Testing

### Test 7.1: Terminal Application
**Manual Testing:**
- Launch desktop environment
- Select "terminal" command
- Terminal window opens
- Can type commands
- Clear screen works

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 7.2: File Manager
**Manual Testing:**
- Launch file manager
- Current directory shown
- Files listed correctly
- Can navigate directories
- Path updates correctly

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 7.3: Task Manager
**Manual Testing:**
- Task manager displays
- Shows PID column
- Shows process names
- Shows memory usage
- Updates on refresh

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 7.4: Control Panel
**Manual Testing:**
- Control panel launches
- System info tab shows data
- Network tab displays config
- Display tab shows settings
- Can switch between tabs

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 8: Network Driver Testing

### Test 8.1: Driver Compilation
```bash
nm build/lib/libnet_driver.a | grep -E "eth_init|wifi_init" | head -5
```
**Expected Results:**
- Ethernet functions present
- WiFi functions present
- No undefined symbols

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 8.2: Network Device Initialization
**Manual Testing:**
- Call eth_init() - eth0 device created
- Call wifi_init() - wlan0 device created
- Devices appear in network device table

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 8.3: Ethernet Operations
**Manual Testing:**
- Enable eth0: `eth_enable_device("eth0")`
- Send packet: `eth_send_packet("eth0", &frame)`
- Verify status updates
- Check statistics increment

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 8.4: WiFi Scanning
**Manual Testing:**
- Call wifi_scan("wlan0")
- Verify networks returned (4 test networks)
- Check no crashes on scan
- Verify output format

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 8.5: WiFi Connection
**Manual Testing:**
- Call wifi_connect("wlan0", "TestNet", "password")
- Verify connection message
- Check status changes to CONNECTED
- Verify signal strength returned

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 8.6: Network Configuration
**Manual Testing:**
- Configure IP: `net_configure_ip("eth0", "192.168.1.100", ...)`
- Set DNS: `net_set_dns("eth0", "8.8.8.8", "8.8.4.4")`
- Get config: `net_get_config("eth0", &config)`
- Verify settings applied

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 9: Integration Testing

### Test 9.1: Component Interaction
**Manual Testing:**
- Launch desktop
- Open terminal in GUI
- Run shell commands in terminal
- Verify output in window

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 9.2: GUI + File Manager
**Manual Testing:**
- Open file manager
- Navigate to /sys/userpc/packages
- Verify installed packages shown
- Test directory navigation

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 9.3: GUI + Task Manager
**Manual Testing:**
- Open task manager
- Show running processes
- Verify kernel, shell, gui listed
- Update process list

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 9.4: GUI + Control Panel
**Manual Testing:**
- Open control panel
- Check system info displays
- Verify network settings
- Check display settings

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 10: Performance & Stability Testing

### Test 10.1: Memory Leak Detection
```bash
valgrind --leak-check=full ./build/toriginal_shell << 'EOF'
pwd
ls
help
exit
EOF
```
**Expected Results:**
- No memory leaks reported
- Heap allocation properly freed
- No invalid memory access

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 10.2: Load Testing
**Manual Testing:**
- Run shell with 100 commands
- Launch multiple GUI windows
- Monitor CPU usage
- Check memory growth

**Expected Results:**
- Stable under load
- No crash after 100 commands
- Memory usage reasonable
- CPU not maxed out

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 10.3: Long Running Stability
**Manual Testing:**
- Keep desktop running for 30 minutes
- Monitor for memory leaks
- Check for responsiveness degradation
- Verify no console errors

**Expected Results:**
- Stable operation
- Responsive input
- Memory usage constant
- No crashes

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 10.4: Stress Testing
**Manual Testing:**
- Open 10+ windows simultaneously
- Create 100+ package entries
- Navigate deep directory structures
- Play snake game for extended period

**Expected Results:**
- All features functional
- No visible slowdown
- No crashes under stress
- Clean shutdown

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 11: Error Handling Testing

### Test 11.1: Invalid Input
**Manual Testing:**
- Enter invalid commands in shell
- Try installing non-existent packages
- Open invalid files in file manager
- Connect to invalid WiFi

**Expected Results:**
- Graceful error messages
- No crashes
- Application recovers
- User can continue

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 11.2: File System Errors
**Manual Testing:**
- Create file with invalid name
- Try to access /root (permission denied)
- Remove directory containing files
- Create duplicate directory

**Expected Results:**
- Appropriate error messages
- Operations fail gracefully
- File system not corrupted
- State remains consistent

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 11.3: Network Errors
**Manual Testing:**
- Disable network device
- Try to send packet when disconnected
- Connect to invalid SSID
- DNS resolution failure

**Expected Results:**
- Error status returned
- No crashes
- Recovery possible
- Statistics updated

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 12: Compatibility Testing

### Test 12.1: Terminal Compatibility
```bash
echo $TERM
./build/toriginal_shell  # Should work in various terminals
```
**Expected Results:**
- Works in xterm
- Works in gnome-terminal
- Works in konsole
- Colors render correctly

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 12.2: Unicode Support
**Manual Testing:**
- Display unicode characters in snake game
- Show icons in desktop
- Render special characters in terminal
- Display emojis in GUI

**Expected Results:**
- All unicode renders correctly
- No character corruption
- Display is centered
- Colors are proper

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Phase 13: Documentation Testing

### Test 13.1: README Accuracy
- [ ] Build instructions work as documented
- [ ] Quick start guide is accurate
- [ ] File structure matches documentation
- [ ] Command examples produce expected output

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

### Test 13.2: Code Comments
- [ ] Complex functions have comments
- [ ] Parameter descriptions present
- [ ] Return values documented
- [ ] Edge cases noted

**Pass/Fail:** ☐ Pass ☐ Fail
**Notes:** ________________________________________

## Bug Report Template

For any bugs found, use this template:

```
BUG REPORT
==========
ID: BUG-XXX
Severity: [Critical|High|Medium|Low]
Component: [Kernel|Shell|GUI|Network|Apps]
Title: 

Description:


Steps to Reproduce:
1.
2.
3.

Expected Result:


Actual Result:


Environment:
- OS: Linux
- Terminal: 
- Build Date:

Workaround:

```

## Test Results Summary

| Phase | Test Count | Passed | Failed | Notes |
|-------|-----------|--------|--------|-------|
| 1. Build | 3 | _ | _ | |
| 2. Kernel | 2 | _ | _ | |
| 3. Shell | 5 | _ | _ | |
| 4. Package Manager | 5 | _ | _ | |
| 5. Snake Game | 6 | _ | _ | |
| 6. GUI Framework | 4 | _ | _ | |
| 7. System Apps | 4 | _ | _ | |
| 8. Network | 6 | _ | _ | |
| 9. Integration | 4 | _ | _ | |
| 10. Performance | 4 | _ | _ | |
| 11. Error Handling | 3 | _ | _ | |
| 12. Compatibility | 2 | _ | _ | |
| 13. Documentation | 2 | _ | _ | |
| **TOTAL** | **50** | **_** | **_** | |

## Final Sign-Off

- Tester Name: ________________
- Date: ________________
- Status: ☐ PASS ☐ FAIL
- Critical Issues: ☐ None ☐ Found
- Recommended for Release: ☐ Yes ☐ No

**Notes:**
_________________________________________________________________
_________________________________________________________________
