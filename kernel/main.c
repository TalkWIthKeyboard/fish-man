
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            main.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

/* Our define*/
#define OS_VERSION "v0.0.1"
const int allCommandCount = 7;
const char * allCommandName[] = {"clear", "echo", "help", "open", "shutdown", "touch", "version"};
char fullPath[100] = "root";
char pathName[10] = "root";
/*****************************************************************************
 *                               kernel_main
 *****************************************************************************/
/**
 * jmp from kernel.asm::_start. 
 * 
 *****************************************************************************/
PUBLIC int kernel_main()
{
	int i, j, eflags, prio;
        u8  rpl;
        u8  priv; /* privilege */

	struct task * t;
	struct proc * p = proc_table;

	char * stk = task_stack + STACK_SIZE_TOTAL;

	for (i = 0; i < NR_TASKS + NR_PROCS; i++,p++,t++) {
		if (i >= NR_TASKS + NR_NATIVE_PROCS) {
			p->p_flags = FREE_SLOT;
			continue;
		}

	        if (i < NR_TASKS) {     /* TASK */
                        t	= task_table + i;
                        priv	= PRIVILEGE_TASK;
                        rpl     = RPL_TASK;
                        eflags  = 0x1202;/* IF=1, IOPL=1, bit 2 is always 1 */
			prio    = 15;
                }
                else {                  /* USER PROC */
                        t	= user_proc_table + (i - NR_TASKS);
                        priv	= PRIVILEGE_USER;
                        rpl     = RPL_USER;
                        eflags  = 0x202;	/* IF=1, bit 2 is always 1 */
			prio    = 5;
                }

		strcpy(p->name, t->name);	/* name of the process */
		p->p_parent = NO_TASK;

		if (strcmp(t->name, "INIT") != 0) {
			p->ldts[INDEX_LDT_C]  = gdt[SELECTOR_KERNEL_CS >> 3];
			p->ldts[INDEX_LDT_RW] = gdt[SELECTOR_KERNEL_DS >> 3];

			/* change the DPLs */
			p->ldts[INDEX_LDT_C].attr1  = DA_C   | priv << 5;
			p->ldts[INDEX_LDT_RW].attr1 = DA_DRW | priv << 5;
		}
		else {		/* INIT process */
			unsigned int k_base;
			unsigned int k_limit;
			int ret = get_kernel_map(&k_base, &k_limit);
			assert(ret == 0);
			init_desc(&p->ldts[INDEX_LDT_C],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_C | priv << 5);

			init_desc(&p->ldts[INDEX_LDT_RW],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_DRW | priv << 5);
		}

		p->regs.cs = INDEX_LDT_C << 3 |	SA_TIL | rpl;
		p->regs.ds =
			p->regs.es =
			p->regs.fs =
			p->regs.ss = INDEX_LDT_RW << 3 | SA_TIL | rpl;
		p->regs.gs = (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;
		p->regs.eip	= (u32)t->initial_eip;
		p->regs.esp	= (u32)stk;
		p->regs.eflags	= eflags;

		p->ticks = p->priority = prio;

		p->p_flags = 0;
		p->p_msg = 0;
		p->p_recvfrom = NO_TASK;
		p->p_sendto = NO_TASK;
		p->has_int_msg = 0;
		p->q_sending = 0;
		p->next_sending = 0;

		for (j = 0; j < NR_FILES; j++)
			p->filp[j] = 0;

		stk -= t->stacksize;
	}

	k_reenter = 0;
	ticks = 0;

	p_proc_ready	= proc_table;

	init_clock();
        init_keyboard();

	restart();

	while(1){}
}


/*****************************************************************************
 *                                get_ticks
 *****************************************************************************/
PUBLIC int get_ticks()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = GET_TICKS;
	send_recv(BOTH, TASK_SYS, &msg);
	return msg.RETVAL;
}


/**
 * @struct posix_tar_header
 * Borrowed from GNU `tar'
 */
struct posix_tar_header
{				/* byte offset */
	char name[100];		/*   0 */
	char mode[8];		/* 100 */
	char uid[8];		/* 108 */
	char gid[8];		/* 116 */
	char size[12];		/* 124 */
	char mtime[12];		/* 136 */
	char chksum[8];		/* 148 */
	char typeflag;		/* 156 */
	char linkname[100];	/* 157 */
	char magic[6];		/* 257 */
	char version[2];	/* 263 */
	char uname[32];		/* 265 */
	char gname[32];		/* 297 */
	char devmajor[8];	/* 329 */
	char devminor[8];	/* 337 */
	char prefix[155];	/* 345 */
	/* 500 */
};

/*****************************************************************************
 *                                shabby_shell
 *****************************************************************************/
/**
 * A very very simple shell.
 * 
 * @param tty_name  TTY file name.
 *****************************************************************************/
void shabby_shell(const char * tty_name)
{
	int fd_stdin  = open(tty_name, O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);

	char rdbuf[128];

	if (!strcmp(tty_name, "/dev_tty0")) {
		openAnimation();
		showInfomation();
		while(1) {}
	}	
	while (1) {
		printfWithColor(2, "user:~/%s$ ", fullPath);

		int r = read(0, rdbuf, 70);
		rdbuf[r] = 0;

		int argc = 0;
		char * argv[PROC_ORIGIN_STACK];
		char * p = rdbuf;
		char * s;
		int word = 0;
		char ch;
		do {
			ch = *p;
			if (*p != ' ' && *p != 0 && !word) {
				s = p;
				word = 1;
			}
			if ((*p == ' ' || *p == 0) && word) {
				word = 0;
				argv[argc++] = s;
				*p = 0;
			}
			p++;
		} while(ch);
		argv[argc] = 0;

		int fd = hasCommand(argv[0]);
		if (fd == -1) {
			if (rdbuf[0]) {
				write(1, "{", 1);
				write(1, rdbuf, r);
				write(1, "} ", 2);
				write(1, "is not a command\n", 17);
			}
		}
		else {
			chooseCommand(argv[0], argc, argv);
		}
	}

	close(1);
	close(0);
}

/*****************************************************************************
 *                                Init
 *****************************************************************************/
/**
 * The hen.
 * 
 *****************************************************************************/
void Init()
{
	int fd_stdin  = open("/dev_tty0", O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open("/dev_tty0", O_RDWR);
	assert(fd_stdout == 1);


	/* extract `cmd.tar' */
	//untar("/cmd.tar");
			

	char * tty_list[] = {"/dev_tty0", "/dev_tty1", "/dev_tty2"};

	int i;
	for (i = 0; i < sizeof(tty_list) / sizeof(tty_list[0]); i++) {
		int pid = fork();
		if (pid != 0) { /* parent process */
		}
		else {	/* child process */
			close(fd_stdin);
			close(fd_stdout);
			
			shabby_shell(tty_list[i]);
			assert(0);
		}
	}

	while (1) {
		int s;
		int child = wait(&s);
	}

	assert(0);
}


/*======================================================================*
                               TestA
 *======================================================================*/
void TestA()
{
	for(;;);
}

/*======================================================================*
                               TestB
 *======================================================================*/
void TestB()
{
	for(;;);
}

/*======================================================================*
                               TestC
 *======================================================================*/
void TestC()
{
	for(;;);
}

/*****************************************************************************
 *                                panic
 *****************************************************************************/
PUBLIC void panic(const char *fmt, ...)
{
	int i;
	char buf[256];

	/* 4 is the size of fmt in the stack */
	va_list arg = (va_list)((char*)&fmt + 4);

	i = vsprintf(buf, fmt, arg);

	printl("%c !!panic!! %s", MAG_CH_PANIC, buf);

	/* should never arrive here */
	__asm__ __volatile__("ud2");
}


/*************/
/*************/
/*************/
/*************/
/* Our start */
/*************/
/*************/
/*************/
/*************/
// tools: clearScreen
void clearScreen() {
	int i;
	disp_pos = 0;
	for(i = 0; i < 80 * 50; i++)
	{
		disp_str(" ");
	}
	disp_pos = 0;
}

void clearAllScreen() {
	int i;
	disp_pos = 0;
	for(i = 0; i < 80 * 300; i++)
	{
		disp_str(" ");
	}
	disp_pos = 0;	
}

// open computer
void openAnimation()//开机动画
{              
	int i, j;
	char progressBar[80];
	for (i = 0; i < 10; i++) {
		clearScreen();
		disp_str("\n\n");
		disp_str("                                             ,1&1\n");
		disp_str("                                           ;3&B#:\n");
		disp_str("                                         .G@@@@9\n");
		disp_str("                                         X@@MAh\n");
		disp_str("                                 ,irr;.  551r::iri:\n");
		disp_str("                             .18AM####HG3558AB####MH9i\n");
		disp_str("                            1H@@#MMMMM##@@@##MMMMMM#@Bi\n");
		disp_str("                           5@#MMMMMMMMMMMMMMMMMMMMMM5.\n");
		disp_str("                          ,M#MMMMMMMMMMMMMMMMMMMMM#1\n");
		disp_str("                          i#MMMMMMMMMMMMMMMMMMMMM#H\n");
		disp_str("                          ,MMMMMMMMMMMMMMMMMMMMMMMM;\n");
		disp_str("                           G@MMMMMMMMMMMMMMMMMMMMM#H;\n");
		disp_str("                           ;#MMMMMMMMMMMMMMMMMMMMMM#M9r.\n");
		disp_str("                            5@MMMMMMMMMMMMMMMMMMMMMMM@#i\n");
		disp_str("                             S@#MMMMMMMMMMMMMMMMMMMM#Mr\n");
		disp_str("                              hM@MMMMMMMM##MMMMMMM#@Hi\n");
		disp_str("                               ;GM@@@@#MBHHB##@@@#B3.\n");
		disp_str("                                 :1SShs;,..,;s5S5r.\n\n");
		disp_str("\n");
		switch (i) {
			case 0: disp_str("                    ____\n"); break;
			case 1: disp_str("                    ________\n"); break;
			case 2: disp_str("                    ____________\n"); break;
			case 3: disp_str("                    ________________\n"); break;
			case 4: disp_str("                    ____________________\n"); break;
			case 5: disp_str("                    ________________________\n"); break;
			case 6: disp_str("                    ____________________________\n"); break;
			case 7: disp_str("                    ________________________________\n"); break;
			case 8: disp_str("                    ____________________________________\n"); break;
			case 9: disp_str("                    ________________________________________\n"); break;
			default: break;
		}
		milli_delay(2000);
	}    
}    

void showInfomation() {
	clearScreen();
	printfWithColor(3, "================================================================================");
	printf("\n\n\n\n\n");
	printfWithColor(6, "                        Welcome to FishmanOS\n");
	printf("\n\n\n\n\n");
	printfWithColor(5, "                     Press F2/F3 to switch terminal\n");
	printfWithColor(5, "              You can input [help] to see all the commands\n");
	printf("\n\n\n\n\n\n\n");
	printfWithColor(0, "                               Made by:\n");
	printfWithColor(0, "                   Bewils, TalkWithKeyboard, Lucciyi\n");
	printfWithColor(0, "                                2016.9\n");
	printfWithColor(3, "===============================================================================");
}

// close computer
void closeAnimation() {
	clearAllScreen();
	printf("\n\n\n\n\n");
	printfWithColor(0, "                   GGGGG OOOOO OOOOO DDD   BBBB  Y   Y EEEEE\n");
	printfWithColor(2, "                   G     O   O O   O D  D  B   B  Y Y  E\n");
	printfWithColor(3, "                   G  GG O   O O   O D   D BBBB    Y   EEEEE\n");
	printfWithColor(4, "                   G   G O   O O   O D  D  B   B   Y   E\n");
	printfWithColor(5, "                   GGGGG OOOOO OOOOO DDD   BBBB    Y   EEEEE");
}

//	commandName: cd, clear, echo, help, ls, mkdir, shutdown, version
int hasCommand(char * commandName) {
	int i;
	for (i = 0; i < allCommandCount; i++)
		if (!strcmp(commandName, allCommandName[i]))
			return 0;
	return -1;
}

void chooseCommand(char * commandName, int argc, char * argv[]) {
	if (!strcmp(commandName, "clear")) {
		clearAllScreen();
		return;
	} else if (!strcmp(commandName, "echo")) {
		echo(argc, argv);
		return;
	} else if (!strcmp(commandName, "help")) {
		showHelp();
		return;
	} else if (!strcmp(commandName, "open")) {
		openFile(argc, argv);
		return;
	} else if (!strcmp(commandName, "shutdown")) {
		closeAnimation();
		while(1);
	} else if (!strcmp(commandName, "touch")) {
		createFile(argc, argv);
		return;
	} else if (!strcmp(commandName, "version")) {
		showVersion();
		return;
	}
}

// command
void echo(int argc, char * argv[]) {
	int i;
	for (i = 1; i < argc; i++)
		printf("%s%s", i == 1 ? "": " ", argv[i]);
	printf("\n");
}

void showHelp() {
	printfWithColor(3, "FishmanOS Help\n");
	printfWithColor(3, "Usage:\n");
	printfWithColor(0, "    options [arguments]\n");
	printfWithColor(3, "Options:\n");
	printfWithColor(0, "    clear: clear the screen\n");
	printfWithColor(0, "    echo: print what you input\n");
	printfWithColor(0, "    help: show help list\n");
	printfWithColor(0, "    open: open the file\n");
	printfWithColor(0, "    shutdown: close the computer\n");
	printfWithColor(0, "    touch: create a new file\n");
	printfWithColor(0, "    version: show the version of the OS\n");
}

void openFile(int argc, char *argv[]) {
	int fd, n;
	char bufr[3];
	if (argc < 2) {
		return;
	} else {
		fd = open(argv[1], O_RDWR);
		n = read(fd, bufr, 3);
		printf("%s\n", bufr);

		close(fd);
	}
}

// xxd -u -a -g l -c 16 -s 0xA01800 -l 512 80m.img
void createFile(int argc, char *argv[]) {
	int i;
	int fd, n;
	char bufr[100] = "";
	for (i = 2; i < argc; i++) {
		strcat(bufr, argv[i]);
	}

	fd = open(argv[1], O_CREAT | O_RDWR);
	n = write(fd, bufr, strlen(bufr));

	close(fd);
}

void showVersion() {
	printf("%s\n", OS_VERSION);
}