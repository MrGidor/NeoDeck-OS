void main() {
    char* video_memory = (char*)0xB8000;
    
    // Row 12, Column 30 calculation:
    // (12 rows * 80 columns + 30 columns) * 2 bytes per char = 1980
    int index = 1980;
    
    video_memory[index]     = 'H'; video_memory[index + 1] = 0x0F; // White text on Black
    video_memory[index + 2] = 'E'; video_memory[index + 3] = 0x0F;
    video_memory[index + 4] = 'L'; video_memory[index + 5] = 0x0F;
    video_memory[index + 6] = 'L'; video_memory[index + 7] = 0x0F;
    video_memory[index + 8] = 'O'; video_memory[index + 9] = 0x0F;
    
    asm volatile("ret");
}