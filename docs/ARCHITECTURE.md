# Architecture Overview

## System Architecture

```
┌─────────────────────────────────────────┐
│       Toriginal OS Shell (C++17)        │
│     User Interface & Commands           │
└────────────┬────────────────────────────┘
             │ Syscalls (INT 0x80)
             ▼
┌─────────────────────────────────────────┐
│         freeNT Kernel (C99)             │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │    Process Management            │  │
│  │  - Scheduling                    │  │
│  │  - Context Switching             │  │
│  │  - Fork/Exec/Exit                │  │
│  └──────────────────────────────────┘  │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │    Memory Management             │  │
│  │  - Paging (4KB pages)            │  │
│  │  - Virtual Address Translation   │  │
│  │  - Heap Allocation               │  │
│  └──────────────────────────────────┘  │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │    Filesystem (VFS)              │  │
│  │  - Directory Navigation          │  │
│  │  - File I/O                      │  │
│  │  - Inode Management              │  │
│  └──────────────────────────────────┘  │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │    Interrupt Management          │  │
│  │  - IDT (256 vectors)             │  │
│  │  - Exception Handling            │  │
│  │  - IRQ/PIC                       │  │
│  └──────────────────────────────────┘  │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │    Executable Loaders            │  │
│  │  - PE (.exe) Format              │  │
│  │  - TRP (.trp) Format             │  │
│  │  - ELF (.elf) Format             │  │
│  └──────────────────────────────────┘  │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │    Drivers & Hardware            │  │
│  │  - Console/VGA                   │  │
│  │  - Serial Port                   │  │
│  │  - Timer/PIT                     │  │
│  └──────────────────────────────────┘  │
│                                         │
└─────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────┐
│    Hardware (x86-64 CPU)                │
│  - Multicore Support Ready              │
│  - Long Mode (64-bit)                   │
│  - Paging/TLB                           │
│  - Interrupts/APIC                      │
└─────────────────────────────────────────┘
```

## Execution Flow

### Application Launch
```
User Command → Shell Parser → Command Execution → Syscall → Kernel Handler
```

### Process Creation
```
SYS_FORK → process_create() → Allocate PID
                           → Create Address Space
                           → Copy Context
                           → Add to Run Queue
```

### Memory Access
```
User Code (Ring 3) → CPU Paging Hardware → Page Table
                                         → Physical RAM
```

### Interrupt Handling
```
Hardware/Software Interrupt → CPU checks IDT → Handler Execution → Return
```

## Memory Layout (64-bit)

```
0xFFFFFFFFFFFFFFFF ┌─────────────────────┐
                   │   Kernel Space      │
                   │  (High Memory)      │
                   │                     │
0xFFFF800000000000 ├─────────────────────┤
                   │                     │
                   │   Unused Space      │
                   │                     │
0x00007FFFFFFFFFFF ├─────────────────────┤
                   │   User Space        │
                   │  (Per-process)      │
                   │                     │
                   │   - Kernel Heap     │
                   │   - Process Heap    │
                   │   - BSS/Data        │
                   │   - Text/Code       │
                   │   - Stack           │
                   │                     │
0x0000000000000000 └─────────────────────┘
```

## Interrupt Vectors

```
0x00-0x1F    CPU Exceptions
0x20-0x2F    PIC1 IRQs
0x30-0x3F    PIC2 IRQs
0x40-0x7F    Hardware IRQs/Special
0x80         System Call Interface
0x81-0xFF    User/Future Use
```

## File System Hierarchy

```
/                    (Root)
├── sys              (Entire drive)
│   ├── userpc       (User personal area)
│   │   └── ~        (User home)
│   │       ├── bin  (Executables)
│   │       ├── lib  (Libraries)
│   │       ├── tmp  (Temporary)
│   │       └── config (Config files)
│   └── kernel       (Kernel files)
├── var              (Variable data)
└── tmp              (System temp)
```

## Process States and Transitions

```
          create
             │
             ▼
       CREATED ──┐
             │   │ schedule
             │   ▼
          exec  RUNNABLE ◄──┐
             │   │          │ yield/IRQ
             ▼   ▼          │
          RUNNING ─────────┘
             │
        wait │ block
             ▼
          WAITING
             │
             ▼
        ready ──┐
             │  │
             ▼  ▼
        STOPPED RUNNABLE
             │
        exit │
             ▼
       TERMINATED ──┐
             │      │
             ▼      ▼
          ZOMBIE  reap
             │
             ▼
          cleaned up
```

## System Call Flow

```
User Program (Ring 3)
         │
         │ mov $SYS_NUMBER, %rax
         │ syscall (or int 0x80)
         ▼
    CPU Mode Switch (Ring 3 → Ring 0)
         │
         ▼
    Kernel Trap Handler
         │
         ▼
    syscall_dispatch()
         │
         ▼
    Handler Function
         │
         ▼
    Kernel Operation
         │
         ▼
    CPU Mode Switch (Ring 0 → Ring 3)
         │
         ▼
User Program Resumes
```

## Binary Format Loading

### PE Executable (.exe)
```
File Header → Optional Header → Section Headers → Section Data

Parse → Allocate Memory → Load Sections → Resolve Imports → Execute
```

### TRP Package (.trp)
```
TRP Header → Section Headers → Section Data

Validate → Parse Flags → Load Sections → Apply Relocations → Execute
```

### Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| Process Creation | < 1ms | Depends on heap size |
| Context Switch | < 10µs | Pure register save/restore |
| Page Allocation | < 100µs | Bitmap lookup + walk tables |
| Syscall (read) | < 1ms | Includes disk I/O |
| Interrupt Handling | < 50µs | Handler-dependent |

## Scheduling Algorithm

**Round-Robin with Priority Levels**

1. Processes grouped by priority (0-255)
2. Higher priority processes run first
3. Equal priority → FIFO round-robin
4. Time slice: ~10ms (configurable)
5. Yield → Move to back of queue

## Security Considerations

1. **Ring Separation**: User/Kernel space isolation
2. **Virtual Addressing**: Each process isolated memory
3. **Syscall Validation**: All parameters checked
4. **Stack Overflow Protection**: Canaries in userspace
5. **DEP/NX**: Executable bit in page tables

## Future Architecture Enhancements

- SMP/Multi-core support with spinlocks
- NUMA-aware memory zones
- Virtual machine hypervisor capabilities
- Capability-based security model
- Real-time scheduling
- Device pass-through support
