// Macropenis .com Framework
#define WHITE_TXT 0x07


unsigned int printf(char *message, unsigned int line)
{
	char *vidmem = (char *) 0xb8000;
	unsigned int i=0;

	i=(line*80*2);

	while(*message!=0)
	{
		if(*message=='\n')
		{
			line++;
			i=(line*80*2);
			*message++;
		} else {
			vidmem[i]=*message;
			*message++;
			i++;
			vidmem[i]=WHITE_TXT;
			i++;
		};
	};

	return(1);
}

void putchar(char c) {
    char *vidmem = (char *) 0xb8000;
    static unsigned int cursor_pos = 0;

    if (c == '\n') {
        cursor_pos = (cursor_pos / 160 + 1) * 160;
    } else {
        vidmem[cursor_pos] = c;
        vidmem[cursor_pos + 1] = WHITE_TXT;
        cursor_pos += 2;
    }

    if (cursor_pos >= 80 * 25 * 2) {
        k_clear_screen();
        cursor_pos = 0;
    }
}

void puts(const char *str) {
    while (*str) {
        putchar(*str++);
    }
    putchar('\n');
}

void k_clear_screen()
{
	char *vidmem = (char *) 0xb8000;
	unsigned int i=0;
	while(i < (80*25*2))
	{
		vidmem[i]=' ';
		i++;
		vidmem[i]=WHITE_TXT;
		i++;
	};
};