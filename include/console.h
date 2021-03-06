
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
			      console.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _ORANGES_CONSOLE_H_
#define _ORANGES_CONSOLE_H_

/* CONSOLE */
typedef struct s_console
{
	unsigned int	crtc_start; /* set CRTC start addr reg */
	unsigned int	orig;	    /* start addr of the console */
	unsigned int	con_size;   /* how many words does the console have */
	unsigned int	cursor;
	int		is_full;
}CONSOLE;


#define SCR_UP	1	/* scroll upward */
#define SCR_DN	-1	/* scroll downward */

#define SCR_SIZE		(80 * 25)
#define SCR_WIDTH		 80

#define DEFAULT_CHAR_COLOR	(MAKE_COLOR(BLACK, WHITE))
#define GRAY_CHAR		(MAKE_COLOR(BLACK, BLACK) | BRIGHT)
#define RED_CHAR		(MAKE_COLOR(BLUE, RED) | BRIGHT)

#define B_BLUC_CHAR		(MAKE_COLOR(BLACK, BLUE))
#define B_GREEN_CHAR	(MAKE_COLOR(BLACK, GREEN))
#define B_CYAN_CHAR		(MAKE_COLOR(BLACK, CYAN))
#define B_RED_CHAR		(MAKE_COLOR(BLACK, RED))
#define B_MAGENTA_CHAR	(MAKE_COLOR(BLACK, MAGENTA))
#define B_BROWN_CHAR	(MAKE_COLOR(BLACK, BROWN))
#define B_WHITE_CHAR	(MAKE_COLOR(BLACK, WHITE))


#endif /* _ORANGES_CONSOLE_H_ */
