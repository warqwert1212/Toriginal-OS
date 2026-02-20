extern "C" void kernel_main() {
    // Initialize kernel
    // This is the main entry point for the kernel after bootloader
    
    volatile char* video_memory = (volatile char*)0xB8000;
    
    // Clear the screen
    for (int i = 0; i < 80 * 25 * 2; i++) {
        video_memory[i] = 0;
    }
    
    // Print welcome message
    const char* message = "Toriganal OS Kernel Initialized";
    int color = 0x0A; // Green on black
    
    int offset = 0;
    for (int i = 0; message[i] != '\0'; i++) {
        video_memory[offset] = message[i];
        video_memory[offset + 1] = color;
        offset += 2;
    }
    
    // Kernel halt
    while(true) {
        asm("hlt");
    }
}