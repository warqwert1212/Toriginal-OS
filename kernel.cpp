extern "C" void kernel_main() {
    // VGA video memory address
    volatile unsigned short* video_memory = (volatile unsigned short*)0xB8000;
    
    // Clear the screen (80x25 characters)
    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i] = 0x0720; // Space character with black background, white foreground
    }
    
    // Print kernel startup message
    const char* kernel_msg = "Kernel Initialized Successfully!";
    int color = 0x0A; // Green on black
    
    int offset = 0;
    for (int i = 0; kernel_msg[i] != '\0'; i++) {
        video_memory[offset] = ((unsigned short)color << 8) | kernel_msg[i];
        offset++;
    }
    
    // Print second line
    offset = 80; // Second line
    const char* status_msg = "Toriganal OS is running in 32-bit protected mode";
    for (int i = 0; status_msg[i] != '\0'; i++) {
        video_memory[offset] = ((unsigned short)color << 8) | status_msg[i];
        offset++;
    }
    
    // Print third line
    offset = 160; // Third line
    const char* ready_msg = "System ready for next phase...";
    for (int i = 0; ready_msg[i] != '\0'; i++) {
        video_memory[offset] = ((unsigned short)color << 8) | ready_msg[i];
        offset++;
    }
    
    // Kernel idle loop
    while(true) {
        asm("hlt");
    }
}