#include <stdint.h>

#define KEYBOARD_BUFFER_SIZE 256

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buffer_head = 0;
static volatile uint32_t buffer_tail = 0;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile(
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

/* Basic US keyboard map */
static const char scancode_map[128] =
{
    0,
    27,
    '1','2','3','4','5','6','7','8','9','0',
    '-','=',
    '\b',
    '\t',
    'q','w','e','r','t','y','u','i','o','p',
    '[',']',
    '\n',
    0,
    'a','s','d','f','g','h','j','k','l',
    ';','\'','`',
    0,
    '\\',
    'z','x','c','v','b','n','m',
    ',', '.', '/',
    0,
    '*',
    0,
    ' ',
};

uint8_t keyboard_get_scancode(void)
{
    return inb(0x60);
}

static void keyboard_push(char c)
{
    uint32_t next =
        (buffer_head + 1) %
        KEYBOARD_BUFFER_SIZE;

    if(next == buffer_tail)
        return;

    keyboard_buffer[buffer_head] = c;
    buffer_head = next;
}

int keyboard_available(void)
{
    return buffer_head != buffer_tail;
}

char keyboard_getchar(void)
{
    if(buffer_head == buffer_tail)
        return 0;

    char c =
        keyboard_buffer[buffer_tail];

    buffer_tail =
        (buffer_tail + 1) %
        KEYBOARD_BUFFER_SIZE;

    return c;
}

unsigned char keyboard_read(void)
{
    return keyboard_getchar();
}

void keyboard_init(void)
{
    buffer_head = 0;
    buffer_tail = 0;
}

void keyboard_handler(void)
{
    uint8_t scancode =
        keyboard_get_scancode();

    /* Ignore key release events */
    if(scancode & 0x80)
        return;

    if(scancode < sizeof(scancode_map))
    {
        char c =
            scancode_map[scancode];

        if(c)
            keyboard_push(c);
    }

// Blocking read: wait (hlt) until a key is available, then return it
char keyboard_getc(void)
{
    while(!keyboard_available()) {
        __asm__ volatile("hlt");
    }

    return keyboard_getchar();
}
}