typedef struct interrupt_frame
{
    uint64_t r15;
    uint64_t r14;
    ...
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;