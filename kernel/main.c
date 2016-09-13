
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
const int allCommandCount = 10;
const char * allCommandName[] = {"clear", "echo", "help", "open", "shutdown", "touch", "version","calc","cale","goBangGame"};
char fullPath[100] = "root";
char pathName[10] = "root";

/*calaculate*/
const char sign[4] = { '+','-','*','/' };
const char level[4] = { 2,2,1,1 };
const char number[10] = { '0','1','2','3','4','5','6','7','8','9' };

struct ans{
	int num;
	int err;
};

struct readStr{
	int argc;
	char * argv[PROC_ORIGIN_STACK];
};

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
	for (i = 0; i < allCommandCount; i++){
		if (!strcmp(commandName, allCommandName[i])){
			return 0;
		}
	}
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
	} else if (!strcmp(commandName, "calc")){
		calculateMain(argc,argv);
		return;
	} else if (!strcmp(commandName, "cale")){
		calendarMain();
		return;
	} else if (!strcmp(commandName, "goBangGame")){
		goBangGameStart();
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
	printfWithColor(0, "    calc: open calculator capabilities\n");
	printfWithColor(0, "    cale: open calendar capabilities\n");
	printfWithColor(0, "    goBangGame: open goBangGame\n");
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


/*-----------------------readScanf-----------------------*/
struct readStr readScanf(){

	struct readStr rs;
	int i;
	int argc = 0;
	char * argv[PROC_ORIGIN_STACK];
	char rdbuf[128];
	int r = read(0, rdbuf, 70);
	rdbuf[r] = 0;

	rs.argc = 0;

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
	rs.argc = argc;
	for (i = 0; i <= argc; i++){
		rs.argv[i] = argv[i];
	}
	return rs;
}


/*-----------------------calculate-----------------------*/
int checkOutArit(char * str) {
	int i, j, flag;
	for (i = 0; i < strlen(str); i++) {
		flag = 0;

		for (j = 0; j < 4; j++) {
			if (str[i] == sign[j]) {
				flag = 1;
				break;
			}
		}

		if (flag == 0) {
			for (j = 0; j < 10; j++) {
				if (str[i] == number[j]) {
					flag = 1;
					break;
				}
			}
		}
		if (flag == 0) {
			return 0;
		}
	}

	return 1;
}

//寻找符号的编号
int findSignNumber(char reSign) {
	int i;
	for (i = 0; i < 4; i++)
	{
		if (reSign == sign[i]) {
			return i;
		}
	}

	return -1;
}

//构建数字
struct ans createNumber(char *str, int first, int last) {
	int a = 1,sum = 0,i;
	struct ans reAns;

	if (first > last) {
		reAns.num = 0;
		reAns.err = 2;
	}
	else {
		for (i = last; i >= first; i--) {
			sum = sum + (str[i] - '0') * a;
			a = a * 10;
		}
		reAns.num = sum;
		reAns.err = -1;
	}

	return reAns;
}

//单一表达式求值
struct ans calculateOnly(int num1, int num2, char op) {
	struct ans reAns;

	reAns.err = -1;
	if (op == '+') {
		reAns.num = num1 + num2;
	}
	if (op == '-') {
		reAns.num = num1 - num2;
	}
	if (op == '*') {
		reAns.num = num1 * num2;
	}
	if (op == '/') {
		if (num2 == 0){
			reAns.num = 0;
			reAns.err = 3;
		}
		else {
			reAns.num = num1 / num2;
		}
	}

	return reAns;
}

//递归进行计算
struct ans calculateArit(char *str, int first, int last) {
	int i,signNumber,maxLevel = 0,maxAdress = -1;
	struct ans num1, num2;

	//寻找最高优先级
	for (i = first; i <= last; i++) {
		signNumber = findSignNumber(str[i]);
		if (signNumber != -1 && level[signNumber] > maxLevel)
		{
			maxLevel = level[signNumber];
			maxAdress = i;
		}
	}

	if (maxLevel == 0) {
		return createNumber(str, first, last);
	}
	else {
		num1 = calculateArit(str, first, maxAdress - 1);
		num2 = calculateArit(str, maxAdress + 1, last);
		if (num1.err == -1 && num2.err == -1) {
			return calculateOnly(num1.num, num2.num, str[maxAdress]);
		}
		else {
			if (num1.err != -1) {
				return num1;
			}
			else {
				return num2;
			}
		}
	}
}

//输出违规操作提示
void printfLawlessOperation(int lawlessNum) {
	if (lawlessNum == 1) {
		printf("-----> Lawlessness character! <-----\n\n");
	}
	if (lawlessNum == 2) {
		printf("-----> Half-baked expression! <-----\n\n");
	}
	if (lawlessNum == 3) {
		printf("-----> Divide zero operation! <-----\n\n");
	}
}

void calculateMain(int argc, char *argv[]) {
	struct ans reAns;
	printf("-----> Calculator <-----\n");
	if (checkOutArit(argv[1]) == 0) {
		printfLawlessOperation(1);
	}
	else {
		reAns = calculateArit(argv[1], 0, strlen(argv[1]) - 1);
		if (reAns.err == -1){
			printf("%d\n\n", reAns.num);
		}
		else {
			printfLawlessOperation(reAns.err);
		}
	}
}

/*-----------------------calender-----------------------*/
#define N 7
void calendarMain(){
	int year, month;
	struct readStr rs;

	while (1){
		rs = readScanf();
		if (strcmp(rs.argv[0],"quit") == 0){
			break;
		}
		year=createNumber(rs.argv[0],0,strlen(rs.argv[1]) - 1).num;
		month=createNumber(rs.argv[1],0,strlen(rs.argv[2]) - 1).num;
		rili(year,month);
	}
}

void print(int day,int tian)
{
	int a[N][N],i,j,sum=1;
	for(i=0,j=0;j<7;j++)
	{
		if(j<day)
			printf("    ");
		else
		{
			a[i][j]=sum;
			printf("   %d",sum++);
			// printf("aaa\n");
		}
	}
	printf("\n");
	for(i=1;sum<=tian;i++)
	{
		for(j=0;sum<=tian&&j<7;j++)
		{
			a[i][j]=sum;
			if (sum<10)
			{
				printf("   %d", sum++);
			}
			else{
				printf("  %d",sum++);
			}
		}
		printf("\n");
	}
}

int duo(int year)
{
	if(year%4==0&&year%100!=0||year%400==0)
		return 1;
	else
		return 0;
}

int rili(int year,int month)
{
	int day,tian,preday,strday;
	printf("***************%dmonth %dyear*********\n",month,year);
	printf(" SUN MON TUE WED THU FRI SAT\n");
	switch(month)
	{
		case 1:
		tian=31;
		preday=0;
		break;
		case 2:
		tian=28;
		preday=31;
		break;
		case 3:
		tian=31;
		preday=59;
		break;
		case 4:
		tian=30;
		preday=90;
		break;
		case 5:
		tian=31;
		preday=120;
		break;
		case 6:
		tian=30;
		preday=151;
		break;
		case 7:
		tian=31;
		preday=181;
		break;
		case 8:
		tian=31;
		preday=212;
		break;
		case 9:
		tian=30;
		preday=243;
		break;
		case 10:
		tian=31;
		preday=273;
		break;
		case 11:
		tian=30;
		preday=304;
		break;
		default:
		tian=31;
		preday=334;
	}
	if(duo(year)&&month>2)
	preday++;
	if(duo(year)&&month==2)
	tian=29;
	day=((year-1)*365+(year-1)/4-(year-1)/100+(year-1)/400+preday+1)%7;
	print(day,tian);
}



/*-----------------------goBangGame-----------------------*/
char gameMap[15][15];

int maxInt(int x,int y){
	return x>y?x:y;
}

int selectPlayerOrder()
{
	printf("o player\n");
	printf("* computer\n");
	printf("who play first?[1/user  other/computer]\n");
	struct readStr rs;
	rs = readScanf();
	if (strcmp(rs.argv[0][0],"1")==0) return 1;
	else return 0;
}

void displayGameState()
{
	int n=15;
	int i,j;
	for (i=0; i<=n; i++)
	{
		if (i<10) printf("%d   ",i);
		else printf("%d  ",i);
	}
	printf("\n");
	for (i=0; i<n; i++)
	{
		if (i<9) printf("%d   ",i+1);
		else printf("%d  ",i+1);
		for (j=0; j<n; j++)
		{
			if (j<10) printf("%c   ",gameMap[i][j]);
			else printf("%c   ",gameMap[i][j]);
		}
		printf("\n");
	}

}

int checkParameter(int x, int y)	//检查玩家输入的参数是否正确
{
	int n=15;
	if (x<0 || y<0 || x>=n || y>=n) return 0;
	if (gameMap[x][y]!='_') return 0;
	return 1;
}

//更新的位置为x，y，因此 只要检查坐标为x，y的位置
int win(int x,int y)		//胜利返回1    否则0（目前无人获胜）
{
	int n=15;
	int i,j;
	int gameCount;
	//左右扩展
	gameCount=1;
	for (j=y+1; j<n; j++)
	{
		if (gameMap[x][j]==gameMap[x][y]) gameCount++;
		else break;
	}
	for (j=y-1; j>=0; j--)
	{
		if (gameMap[x][j]==gameMap[x][y]) gameCount++;
		else break;
	}
	if (gameCount>=5) return 1;

	//上下扩展
	gameCount=1;
	for (i=x-1; i>0; i--)
	{
		if (gameMap[i][y]==gameMap[x][y]) gameCount++;
		else break;
	}
	for (i=x+1; i<n; i++)
	{
		if (gameMap[i][y]==gameMap[x][y]) gameCount++;
		else break;
	}
	if (gameCount>=5) return 1;

	//正对角线扩展
	gameCount=1;
	for (i=x-1,j=y-1; i>=0 && j>=0; i--,j--)
	{
		if (gameMap[i][j]==gameMap[x][y]) gameCount++;
		else break;
	}
	for (i=x+1,j=y+1; i<n && j<n; i++,j++)
	{
		if (gameMap[i][j]==gameMap[x][y]) gameCount++;
		else break;
	}
	if (gameCount>=5) return 1;

	//负对角线扩展
	gameCount=1;
	for (i=x-1,j=y+1; i>=0 && j<n; i--,j++)
	{
		if (gameMap[i][j]==gameMap[x][y]) gameCount++;
		else break;
	}
	for (i=x+1,j=y-1; i<n && j>=0; i++,j--)
	{
		if (gameMap[i][j]==gameMap[x][y]) gameCount++;
		else break;
	}
	if (gameCount>=5) return 1;

	return 0;
}

void free1(int x,int y1,int y2,int* ff1,int* ff2)
{
	int n=15;
	int i;
	int f1=0,f2=0;
	for (i=y1; i>=0; i++)
	{
		if (gameMap[x][i]=='_') f1++;
		else break;
	}
	for (i=y2; i<n; i++)
	{
		if (gameMap[x][i]=='_') f2++;
		else break;
	}
	*ff1=f1;
	*ff2=f2;
}

void free2(int x1,int x2,int y,int *ff1,int *ff2)
{
	int n=15;
	int i;
	int f1=0,f2=0;
	for (i=x1; i>=0; i--)
	{
		if (gameMap[i][y]=='_') f1++;
		else break;
	}
	for (i=x2; i<n; i++)
	{
		if (gameMap[i][y]=='_') f2++;
		else break;
	}
	*ff1=f1;
	*ff2=f2;
}

void free3(int x1,int y1,int x2,int y2,int *ff1,int *ff2)
{
	int n=15;
	int x,y;
	int f1=0;
	int f2=0;
	for (x=x1,y=y1; 0<=x && 0<=y; x--,y--)
	{
		if (gameMap[x][y]=='_') f1++;
		else break;
	}
	for (x=x2,y=y2; x<n &&  y<n; x++,y++)
	{
		if (gameMap[x][y]=='_') f2++;
		else break;
	}
	*ff1=f1;
	*ff2=f2;
}

void free4(int x1,int y1,int x2,int y2,int *ff1,int *ff2)
{
	int n=15;
	int x,y;
	int f1=0,f2=0;
	for (x=x1,y=y1; x>=0 && y<n; x--,y++)
	{
		if (gameMap[x][y]=='_') f1++;
		else break;
	}
	for (x=x2,y=y2; x<n && y>=0; x++,y--)
	{
		if (gameMap[x][y]=='_') f2++;
		else break;
	}
	*ff1=f1;
	*ff2=f2;
}

int getPossibleByAD(int attack,int defence,int attackFree1,int attackFree2,int defenceFree1,int defenceFree2)
{
	if (attack>=5) return 20;						//5攻击
	if (defence>=5) return 19;						//5防御
	if (attack==4 && (attackFree1>=1 && attackFree2>=1)) return 18;		//4攻击 2边
	if (attack==4 && (attackFree1>=1 || attackFree2>=1)) return 17;		//4攻击 1边
	if (defence==4 && (defenceFree1>=1 || defenceFree2>=1)) return 16;	//4防御
	if (attack==3 && (attackFree1>=2 && attackFree2>=2)) return 15;		//3攻击 2边
	if (defence==3 && (defenceFree1>=2 && defenceFree2>=2)) return 14;	//3防御 2边
	if (defence==3 && (defenceFree1>=2 || defenceFree2>=2)) return 13;	//3防御 1边
	if (attack==3 && (attackFree1>=2 || attackFree2>=2)) return 12;		//3攻击 1边
	if (attack==2 && (attackFree1>=3 && attackFree2>=3)) return 11;		//2攻击 2边
	if (defence==2 && defenceFree1+defenceFree2>=3) return 10;	//2防御 2边
	if (defence==2 && defenceFree1+defenceFree2>=3) return 9;		//2防御 1边
	if (attack==1 && attackFree1+attackFree2>=4) return 8;
	if (defence==1 && defenceFree1+defenceFree2>=4) return 7;
	return 6;
}

int getPossible(int x,int y)
{
	int n=15;
	int attack;
	int defence;
	int attackFree1;
	int defenceFree1;
	int attackFree2;
	int defenceFree2;
	int possible=-100;

	//左右扩展
	int al,ar;
	int dl,dr;
	//横向攻击
	for (al=y-1; al>=0; al--)
	{
		if (gameMap[x][al]!='*') break;
	}
	for (ar=y+1; ar<n; ar++)
	{
		if (gameMap[x][ar]!='*') break;
	}
	//横向防守
	for (dl=y-1; dl>=0; dl--)
	{
		if (gameMap[x][dl]!='o') break;
	}
	for (dr=y+1; dr<n; dr++)
	{
		if (gameMap[x][dr]!='o') break;
	}
	attack=ar-al-1;
	defence=dr-dl-1;
	free1(x,al,ar,&attackFree1,&attackFree2);
	free1(x,dl,dr,&defenceFree1,&defenceFree2);
	possible=maxInt(possible,getPossibleByAD(attack,defence,attackFree1,attackFree2,defenceFree1,defenceFree2));

	//竖向进攻
	for (al=x-1; al>=0; al--)
	{
		if (gameMap[al][y]!='*') break;
	}
	for (ar=x+1; ar<n; ar++)
	{
		if (gameMap[ar][y]!='*') break;
	}
	//竖向防守
	for (dl=x-1; dl>=0; dl--)
	{
		if (gameMap[dl][y]!='o') break;
	}
	for (dr=x+1; dr<n; dr++)
	{
		if (gameMap[dr][y]!='o') break;
	}
	attack=ar-al-1;
	defence=dr-dl-1;
	free2(al,ar,y,&attackFree1,&attackFree2);
	free2(dl,dr,y,&defenceFree1,&defenceFree2);
	possible=maxInt(possible,getPossibleByAD(attack,defence,attackFree1,attackFree2,defenceFree1,defenceFree2));

	//正对角线进攻
	int al1,al2,ar1,ar2;
	int dl1,dl2,dr1,dr2;
	for (al1=x-1,al2=y-1; al1>=0 && al2>=0; al1--,al2--)
	{
		if (gameMap[al1][al2]!='*') break;
	}
	for (ar1=x+1,ar2=y+1; ar1<n && ar2<n; ar1++,ar2++)
	{
		if (gameMap[ar1][ar2]!='*') break;
	}
	//正对角线防守
	for (dl1=x-1,dl2=y-1; dl1>=0 && dl2>=0; dl1--,dl2--)
	{
		if (gameMap[dl1][dl2]!='o') break;
	}
	for (dr1=x+1,dr2=y+1; dr1<n && dr2<n; dr1++,dr2++)
	{
		if (gameMap[dr1][dr2]!='o') break;
	}
	attack=ar1-al1-1;
	defence=dr1-dl1-1;
	free3(al1,al2,ar1,ar2,&attackFree1,&attackFree2);
	free3(dl1,dl2,dr1,dr2,&defenceFree1,&defenceFree2);
	possible=maxInt(possible,getPossibleByAD(attack,defence,attackFree1,attackFree1,defenceFree1,defenceFree2));

	//负对角线进攻
	for (al1=x-1,al2=y+1; al1>=0 && al2<n; al1--,al2++)
	{
		if (gameMap[al1][al2]!='*') break;
	}
	for (ar1=x+1,ar2=y-1; ar1<n && ar2>=0; ar1++,ar2--)
	{
		if (gameMap[ar1][ar2]!='*') break;
	}
	//负对角线防守
	for (dl1=x-1,dl2=y+1; dl1>=0 && dl2<n; dl1--,dl2++)
	{
		if (gameMap[dl1][dl2]!='o') break;
	}
	for (dr1=x+1,dr2=y-1; dr1<n && dr2>=0; dr1++,dr2--)
	{
		if (gameMap[dr1][dr2]!='o') break;
	}
	attack=ar1-al1-1;
	defence=dr1-dl1-1;
	free4(al1,al2,ar1,ar2,&attackFree1,&attackFree2);
	free4(dl1,dl2,dr1,dr2,&defenceFree1,&defenceFree2);
	possible=maxInt(possible,getPossibleByAD(attack,defence,attackFree1,attackFree2,defenceFree1,defenceFree2));
	return possible;
}


void goBangGameStart()
{
	int playerStep=0;
	int computerStep=0;
	int n=15;
	int i,j;
	while (1)
	{
		for (i=0; i<n; i++)
			for (j=0; j<n; j++)
				gameMap[i][j]='_';


		gameMap[n>>1][n>>1]='*';
		displayGameState();
		printf("[computer step:%d]%d,%d\n",++computerStep,(n>>1)+1,(n>>1)+1);
		/*else
		{
			displayGameState();
		}*/

		while (1)
		{
			int x,y;
			while (1)
			{
				printf("[player step:%d]\n",++playerStep);
				//scanf("%d%d",&x,&y);
				struct readStr rs;
				rs = readScanf();
				x = createNumber(rs.argv[0],0,strlen(rs.argv[1]) - 1).num;
				y = createNumber(rs.argv[1],0,strlen(rs.argv[1]) - 1).num;
				x--,y--;
				if ( checkParameter(x,y) )
				{
					gameMap[x][y]='o';
					break;
				}
				else
				{
					playerStep--;
					printf("the position you put error\n");
				}
			}
			if (win(x,y))
			{
				displayGameState();
				printf("Congratulation you won the game\n");
				break;
			}
			int willx,willy,winPossible=-100;
			for (i=0; i<n; i++)
				for (j=0; j<n; j++)
				{
					if (gameMap[i][j]=='_')
					{
						int possible=getPossible(i,j);
						if (possible>=winPossible)
						{
							willx=i; willy=j;
							winPossible=possible;
						}
					}
				}
				gameMap[willx][willy]='*';
				displayGameState();
				printf("[computer step:%d]%d,%d\n",++computerStep,willx+1,willy+1);
				if (win(willx,willy))
				{
					printf("Sorry you lost the game\n");
					break;
				}
		}
	}

}
