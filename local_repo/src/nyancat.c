/*
 * 版权所有 (c) 2011-2018 K. Lange。保留所有权利。
 *
 * 开发者:                  K. Lange
 *                          http://github.com/klange/nyancat
 *                          http://nyancat.dakko.us
 *
 * 40列支持:                Peter Hazenberg
 *                          http://github.com/Peetz0r/nyancat
 *                          http://peter.haas-en-berg.nl
 *
 * 统一的构建工具：           Aaron Peschel
 *                          https://github.com/apeschel
 *
 * 有关贡献者的完整列表，请参见git提交历史记录。
 *
 * 这是一个简单的telnet服务器/独立应用程序，它将经典的Nyan Cat（或“poptart cat”）呈现到您的终端。
 *
 * 它利用各种ANSI转义序列来呈现颜色，或者在VT220的情况下，只需将文本转储到屏幕上。
 *
 * 欲了解更多信息，请参见:
 *
 *      http://nyancat.dakko.us
 *
 * 特此免费授予任何获取本软件及相关文档文件（“软件”）的人不受限制地处理该软件的权限，包括但不限于使用、复制、修改、合并、发布、分发、转让、销售该软件副本并允许获得该软件的人无条件地处理该软件，但需满足以下条件：
 * 1.源代码的再发布必须保留上述版权声明、本条件列表和以下免责声明。
 * 2.以二进制形式再发布的包必须在文档和/或其他材料中重复上述版权声明、本条件列表和以下免责声明。
 * 3.不得使用计算机协会、K. Lange的名称或其贡献者的名称来认可或推广从本软件派生的产品，除非有特定的书面许可。
 *//* 软件按原样提供，没有任何保证，无论是明示的还是暗示的，
 * 包括但不限于适销性，某一特定用途的适用性和非侵权性。 在任何情况下，
 * 贡献者或版权持有人都不承担任何索赔，损害或其他责任，
 * 无论是在合同行为，侵权行为或其他方面产生的，与软件或使用或其他关系有关的。
 * "。
 
#define  _XOPEN_SOURCE 700
#define  _DARWIN_C_SOURCE 1
#define  _BSD_SOURCE
#define  _DEFAULT_SOURCE
#define  __BSD_VISIBLE 1
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <setjmp.h>
#include <getopt.h>

#include <sys/ioctl.h>

#ifndef TIOCGWINSZ
#include <termios.h>
#endif

#ifdef ECHO
#undef ECHO
#endif

/*
 * telnet.h 包含一些有关各种 #define 的内容
 * 命令、转义字符和 telnet 的模式。
 * （有些人惊讶的是 telnet 实际上是一种协议，而不只是原始文本传输）。
 */
#include "telnet.h"

/*
 * 动画帧被单独存储在这个header中，以免混淆核心来源。
 */
#include "animation.c"

/*
 * 用于最终输出的色板。
 * 具体来说，这应该是控制序列或原始字符（例如vt220模式）。
 */
const char * colors[256] = {NULL};

/*
 * 对于大多数模式，我们输出空格，但对于某些模式，我们将使用块字符（甚至什么都不用）。
 */
const char * output = "  ";

/*
 * 我们当前是否在telnet模式下？
 */
int telnet = 0;

/*
 * 是否显示计数器
 */
int show_counter = 1;

/*
 * 在退出之前要显示的帧数或者0无限制循环（默认）。
 */
unsigned int frame_count = 0;

/*
 * 在帧之间清除屏幕（与重置相对）。
 */
int clear_screen = 1;

/*
 * 强制设置终端标题。
 */
int set_title = 1;* 当从选项处理器中断时，用于 setjmp/longjmp 的环境
 */
 jmp_buf environment;


/*
 * 为了保持外部依赖的少，我拒绝包含libm。
 *
 * 计算数字的位数，以便进行字符串输出。
 */
 int digits(int val) {
 	int d = 1, c;
 	if (val >= 0) for (c = 10; c <= val; c *= 10) d++;
 	else for (c = -10 ; c >= val; c *= 10) d++;
 	return (c < 0) ? ++d : d;
 }

/*
 * 这些值裁剪了动画，因为我们有一个完整的64x64存储，
 * 但我们只想显示40x24（双宽）。
 */
 int min_row = -1;
 int max_row = -1;
 int min_col = -1;
 int max_col = -1;

/*
 * 终端的实际宽度/高度。
 */
 int terminal_width = 80;
 int terminal_height = 24;

/*
 * 用于跟踪是否自动设置了宽度/高度的标志。
 */
 char using_automatic_width = 0;
 char using_automatic_height = 0;

/*
 * 以返回光标到可见模式和退出应用程序的转义序列。
 */
 void finish() {
 	if (clear_screen) {
 		printf("\033[?25h\033[0m\033[H\033[2J");
 	} else {
 		printf("\033[0m\n");
 	}
 	exit(0);
 }

/*
 * 在独立模式下，我们希望处理中断信号
 * (^C)这样，我们可以恢复光标并清除终端。
 */
 void SIGINT_handler(int sig){
 	(void)sig;
 	finish();
 }

/*
 * 处理闹钟，如果我们没有收到终端，则我们将其命中结束
 * 处理。
 */
 void SIGALRM_handler(int sig) {
 	(void)sig;
 	alarm(0);
 	longjmp(environment, 1);
 	/* 不可达 */
 }

/*
 * 处理stdout的丢失，这将是在
 * telnet模式下，客户端断开连接的情况。
 */
 void SIGPIPE_handler(int sig) {
 	(void)sig;
 	finish();
 }

 void SIGWINCH_handler(int sig) {
 	(void)sig;
 	struct winsize w;
 	ioctl(0, TIOCGWINSZ, &w);
 	terminal_width = w.ws_col;
 	terminal_height = w.ws_row;

 	if (using_automatic_width) {
 		min_col = (FRAME_WIDTH - terminal_width/2) / 2;
 		max_col = (FRAME_WIDTH + terminal_width/2) / 2;
 	}

 	if (using_automatic_height) {
 		min_row = (FRAME_HEIGHT - (terminal_height-1)) / 2;".max_row = (FRAME_HEIGHT + (terminal_height-1)) / 2;
}

signal(SIGWINCH, SIGWINCH_handler);
}

/*
 * Telnet 要求我们发送特定的序列
 * 用于换行 (\r\000\n)，让我们让它满意。
 */
void newline(int n) {
	int i = 0;
	for (i = 0; i < n; ++i) {
		/* 我们将向客户端发送 `n` 个换行符 */
		if (telnet) {
			/* 发送 telnet 换行序列 */
			putc('\r', stdout);
			putc(0, stdout);
			putc('\n', stdout);
		} else {
			/* 发送普通的换行符 */
			putc('\n', stdout);
		}
	}
}

/*
 * 这些是我们要用作
 * 一个 Telnet 服务器的选项。这些选项在 set_options() 中设置。
 */
unsigned char telnet_options[256] = { 0 };
unsigned char telnet_willack[256] = { 0 };

/*
 * 这些是我们在握手期间设置或同意的值。
 * 这些选项在 send_command(...) 中设置。
 */
unsigned char telnet_do_set[256]  = { 0 };
unsigned char telnet_will_set[256]= { 0 };

/*
 * 为 Telnet 服务器设置默认选项。
 */
void set_options() {
	/* 我们不会回显输入 */
	telnet_options[ECHO] = WONT;
	/* 我们将设置图形模式 */
	telnet_options[SGA]  = WILL;
	/* 我们不会设置新环境 */
	telnet_options[NEW_ENVIRON] = WONT;

	/* 客户端应该回显它自己的输入 */
	telnet_willack[ECHO]  = DO;
	/* 客户端可以设置图形模式 */
	telnet_willack[SGA]   = DO;
	/* 客户端不应该更改但应该告诉我们其窗口大小 */
	telnet_willack[NAWS]  = DO;
	/* 客户端应该告诉我们其终端类型（非常重要） */
	telnet_willack[TTYPE] = DO;
	/* 没有线模式 */
	telnet_willack[LINEMODE] = DONT;
	/* 客户端可以设置新环境 */
	telnet_willack[NEW_ENVIRON] = DO;
}

/*
 * 向 Telnet 客户端发送命令 (cmd)
 * 还执行 DO/DONT/WILL/WONT 的特殊处理
 */
void send_command(int cmd, int opt) {
	/* 向 Telnet 客户端发送命令 */
	if (cmd == DO || cmd == DONT) {
		/* DO 命令说明客户端应该执行什么操作。 */
		if (((cmd == DO) && (telnet_do_set[opt] != DO)) ||...((cmd == DONT) && (telnet_do_set[opt] != DONT))) {
	/* 如果存在分歧，那么我们只发送DONT命令 */
	telnet_do_set[opt] = cmd;
	printf("%c%c%c", IAC, cmd, opt);
}
} else if (cmd == WILL || cmd == WONT) {
/* 类似的，WILL命令说服服务器需要做什么。 */
	if (((cmd == WILL) && (telnet_will_set[opt] != WILL)) ||
			((cmd == WONT) && (telnet_will_set[opt] != WONT))) {
			/* 仅在存在分歧时发送 */
		telnet_will_set[opt] = cmd;
		printf("%c%c%c", IAC, cmd, opt);
	}
} else {
	/* 其他命令原样发送 */
	printf("%c%c", IAC, cmd);
}
}

/*
 * 输出使用/帮助文本，描述选项
 */
void usage(char * argv[]) {
	printf(
			"终端Nyancat\n"
			"\n"
			"usage: %s [-hitn] [-f \033[3mframes\033[0m]\n"
			"\n"
			" -i --intro      \033[3m启动时显示介绍/有关信息。\033[0m\n"
			" -t --telnet     \033[3mTelnet模式。\033[0m\n"
			" -n --no-counter \033[3m不显示计时器。\033[0m\n"
			" -s --no-title   \033[3m不设置标题栏文字。\033[0m\n"
			" -e --no-clear   \033[3m不在帧之间清除显示。\033[0m\n"
			" -d --delay      \033[3m将图像渲染延迟10毫秒至1000毫秒。\033[0m\n"
			" -f --frames     \033[3m显示所请求的帧数，然后退出。\033[0m\n"
			" -r --min-rows   \033[3m从顶部裁剪动画。\033[0m\n"
			" -R --max-rows   \033[3m从底部裁剪动画。\033[0m\n"
			" -c --min-cols   \033[3m从左侧裁剪动画。\033[0m\n"
			" -C --max-cols   \033[3m从右侧裁剪动画。\033[0m\n"
			" -W --width      \033[3m将动画裁剪为给定宽度。\033[0m\n"
			" -H --height     \033[3m将动画裁剪为给定高度。\033[0m\n"
			" -h --help       \033[3m显示此帮助消息。\033[0m\n",
			argv[0]);
}

int main(int argc, char ** argv) {

	char *term = NULL;
	unsigned int k;
	int ttype;
	uint32_t option = 0, done = 0, sb_mode = 0;
	/* 用于Telnet通信的各种参数 */
	unsigned char  sb[1024] = {0};unsigned short sb_len = 0;

/* 是否显示MOTD简介 */
char show_intro = 0;
char skip_intro = 0;

/* 长选项名称 */
static struct option long_opts[] = {
    {"help",       no_argument,       0, 'h'},
    {"telnet",     no_argument,       0, 't'},
    {"intro",      no_argument,       0, 'i'},
    {"skip-intro", no_argument,       0, 'I'},
    {"no-counter", no_argument,       0, 'n'},
    {"no-title",   no_argument,       0, 's'},
    {"no-clear",   no_argument,       0, 'e'},
    {"delay",      required_argument, 0, 'd'},
    {"frames",     required_argument, 0, 'f'},
    {"min-rows",   required_argument, 0, 'r'},
    {"max-rows",   required_argument, 0, 'R'},
    {"min-cols",   required_argument, 0, 'c'},
    {"max-cols",   required_argument, 0, 'C'},
    {"width",      required_argument, 0, 'W'},
    {"height",     required_argument, 0, 'H'},
    {0,0,0,0}
};

/* 以毫秒为单位的时间延迟 */
int delay_ms = 90; // 默认值为原始值

/* 处理参数*/
int index, c;
while ((c = getopt_long(argc, argv, "eshiItnd:f:r:R:c:C:W:H:", long_opts, &index)) != -1) {
    if (!c) {
        if (long_opts[index].flag == 0) {
            c = long_opts[index].val;
        }
    }
    switch (c) {
        case 'e':
            clear_screen = 0;
            break;
        case 's':
            set_title = 0;
            break;
        case 'i': /* 显示简介 */
            show_intro = 1;
            break;
        case 'I':
            skip_intro = 1;
            break;
        case 't': /* 期望使用telnet */
            telnet = 1;
            break;
        case 'h': /* 显示帮助并退出 */
            usage(argv);
            exit(0);
            break;
        case 'n':
            show_counter = 0;
            break;
        case 'd':
            if (10 <= atoi(optarg) && atoi(optarg) <= 1000)
                delay_ms = atoi(optarg);
            break;
        case 'f':
            frame_count = atoi(optarg);
            break;
        case 'r':
            min_row = atoi(optarg);
            break;
        case 'R':
            max_row = atoi(optarg);
            break;
        case 'c':
            min_col = atoi(optarg);
            break;
        case 'C':
            max_col = atoi(optarg);
            break;
        case 'W':
            min_col = (FRAME_WIDTH - atoi(optarg)) / 2;max_col = (FRAME_WIDTH + atoi(optarg)) / 2;
				break;
			case 'H':
				min_row = (FRAME_HEIGHT - atoi(optarg)) / 2;
				max_row = (FRAME_HEIGHT + atoi(optarg)) / 2;
				break;
			default:
				break;
		}
	}

	if (telnet) {
		/* Telnet 模式 */

		/* 如果 skip_intro 未设置，则 show_intro 为默认值 */
		show_intro = (skip_intro == 0) ? 1 : 0;

		/* 设置默认选项 */
		set_options();

		/* 让客户端知道我们的使用情况 */
		for (option = 0; option < 256; option++) {
			if (telnet_options[option]) {
				send_command(telnet_options[option], option);
				fflush(stdout);
			}
		}
		for (option = 0; option < 256; option++) {
			if (telnet_willack[option]) {
				send_command(telnet_willack[option], option);
				fflush(stdout);
			}
		}

		/* 设置alarm处理程序以执行longjmp */
		signal(SIGALRM, SIGALRM_handler);

		/* 协商选项 */
		if (!setjmp(environment)) {
			/* 一秒后停止处理选项 */
			alarm(1);

			/* 让我们开始吧 */
			while (!feof(stdin) && done < 2) {
				/* 获取IAC（开始命令）或常规字符（中断，除非在SB模式下） */
				unsigned char i = getchar();
				unsigned char opt = 0;
				if (i == IAC) {
					/* 如果是IAC，则获取命令 */
					i = getchar();
					switch (i) {
						case SE:
							/* 扩展选项模式结束 */
							sb_mode = 0;
							if (sb[0] == TTYPE) {
								/* 这是对TTYPE命令的响应，意味着
								 * 这应该是一个终端类型 */
								alarm(2);
								term = strndup((char *)&sb[2], sizeof(sb)-2);
								done++;
							}
							else if (sb[0] == NAWS) {
								/* 这是对NAWS命令的响应，意味着
								 * 这应该是一个窗口大小 */
								alarm(2);
								terminal_width = (sb[1] << 8) | sb[2];
								terminal_height = (sb[3] << 8) | sb[4];
								done++;
							}
							break;
						case NOP:
							/* No Op */
							send_command(NOP, 0);
							fflush(stdout);
							break;
						case WILL:
						case WONT:"。/* 是否协商 */
							opt = getchar();
							if (!telnet_willack[opt]) {
								/* 默认为 WONT */
								telnet_willack[opt] = WONT;
							}
							send_command(telnet_willack[opt], opt);
							fflush(stdout);
							if ((i == WILL) && (opt == TTYPE)) {
								/* WILL TTYPE? 那太好了，现在就做! */
								printf("%c%c%c%c%c%c", IAC, SB, TTYPE, SEND, IAC, SE);
								fflush(stdout);
							}
							break;
						case DO:
						case DONT:
							/* 协商 Do / Don't */
							opt = getchar();
							if (!telnet_options[opt]) {
								/* 默认为DONT */
								telnet_options[opt] = DONT;
							}
							send_command(telnet_options[opt], opt);
							fflush(stdout);
							break;
						case SB:
							/* 开始扩展选项模式 */
							sb_mode = 1;
							sb_len  = 0;
							memset(sb, 0, sizeof(sb));
							break;
						case IAC: 
							/* IAC IAC? 大概不对吧。 */
							done = 2;
							break;
						default:
							break;
					}
				} else if (sb_mode) {
					/* 扩展选项模式 -> 接受字符 */
					if (sb_len < sizeof(sb) - 1) {
						/* 将此字符追加到SB字符串中，
						 * 但只有在不超过我们的限制时才这样做;
						 * 说实话，我们不应该达到这个限制，
						 * 因为我们只是为终端类型或窗口大小收集字符，
						 * 但安全起见，先处理好 (并且不容易受攻击)。
						 */
						sb[sb_len] = i;
						sb_len++;
					}
				}
			}
		}
		alarm(0);
	} else {
		/* 我们正在独立运行，从环境中检索终端类型。*/
		term = getenv("TERM");

		/* 还要获取列数 */
		struct winsize w;
		ioctl(0, TIOCGWINSZ, &w);
		terminal_width = w.ws_col;
		terminal_height = w.ws_row;
	}

	/* 默认的ttype */
	ttype = 2;

	if (term) {
		/* 全部转换为小写 */
		for (k = 0; k < strlen(term); ++k) {
			term[k] = tolower(term[k]);
		}

		/* 进行终端检测 */
		if (strstr(term, "xterm")) {"ttype = 1; /* 256种颜色，空格 */
		} else if (strstr(term, "toaru")) {
			ttype = 1; /* 模拟xterm */
		} else if (strstr(term, "linux")) {
			ttype = 3; /*空格和闪烁属性*/
		} else if (strstr(term, "vtnt")) {
			ttype = 5; /* 扩展ASCII回退 == Windows */
		} else if (strstr(term, "cygwin")) {
			ttype = 5; /* 扩展ASCII回退 == Windows */
		} else if (strstr(term, "vt220")) {
			ttype = 6; /* 没有颜色支持 */
		} else if (strstr(term, "fallback")) {
			ttype = 4; /* Unicode回退 */
		} else if (strstr(term, "rxvt-256color")) {
			ttype = 1; /* xterm 256-color 兼容*/
		} else if (strstr(term, "rxvt")) {
			ttype = 3; /* 接受 LINUX 模式 */
		} else if (strstr(term, "vt100") && terminal_width == 40) {
			ttype = 7; /* 没有颜色支持，仅有40列 */
		} else if (!strncmp(term, "st", 2)) {
			ttype = 1; /* suckless simple terminal 兼容 xterm-256color */
		}
	}

	int always_escape = 0; /* 用于文本模式 */

	/* Accept ^C -> restore cursor */  
    /* 接受 ^ C -> 恢复光标 */
	signal(SIGINT, SIGINT_handler);

	/* Handle loss of stdout */ 
    /* 处理 stdout 丢失*/
	signal(SIGPIPE, SIGPIPE_handler);

	/* Handle window changes */ 
    /* 处理 窗口 变化*/
	if (!telnet) {
		signal(SIGWINCH, SIGWINCH_handler);
	}

	switch (ttype) {
		case 1:
			colors[',']  = "\033[48;5;17m";  /* 蓝色背景 */
			colors['.']  = "\033[48;5;231m"; /* 白色星星 */
			colors['\''] = "\033[48;5;16m";  /* 黑色边框 */
			colors['@']  = "\033[48;5;230m"; /* 黄色长条糖果 */
			colors['$']  = "\033[48;5;175m"; /* 粉红色长条糖果 */
			colors['-']  = "\033[48;5;162m"; /* 红色长条糖果 */
			colors['>']  = "\033[48;5;196m"; /* 红色彩虹 */
			colors['&']  = "\033[48;5;214m"; /* 橙色彩虹 */
			colors['+']  = "\033[48;5;226m"; /* 黄色彩虹 */
			colors['#']  = "\033[48;5;118m"; /* 绿色彩虹 */
			colors['=']  = "\033[48;5;33m";  /* 浅蓝色彩虹 */
			colors[';']  = "\033[48;5;19m";  /* 深蓝色彩虹 */
			colors['*']  = "\033[48;5;240m"; /* 猫脸的灰色 */
			colors['%']  = "\033[48;5;175m"; /* 粉红色脸颊 */
			break;
		case 2:".
format: 只返回翻译后的内容，不包括原始文本。colors[','] = "\033[104m"; /* 蓝色背景 */
            colors['.'] = "\033[107m"; /* 白色星星 */
            colors['\''] = "\033[40m"; /* 黑色边框 */
            colors['@'] = "\033[47m"; /* 淡棕色流行饼 */
            colors['$'] = "\033[105m"; /* 粉色流行饼 */
            colors['-'] = "\033[101m"; /* 红色流行饼 */
            colors['>'] = "\033[101m"; /* 红色彩虹 */
            colors['&'] = "\033[43m"; /* 橙色彩虹 */
            colors['+'] = "\033[103m"; /* 黄色彩虹 */
            colors['#'] = "\033[102m"; /* 绿色彩虹 */
            colors['='] = "\033[104m"; /* 浅蓝色彩虹 */
            colors[';'] = "\033[44m"; /* 深蓝色彩虹 */
            colors['*'] = "\033[100m"; /* 灰色猫脸 */
            colors['%'] = "\033[105m"; /* 粉红色脸颊 */
            break;
        case 3:
            colors[','] = "\033[25;44m"; /* 蓝色背景 */
            colors['.'] = "\033[5;47m"; /* 白色星星 */
            colors['\''] = "\033[25;40m"; /* 黑色边框 */
            colors['@'] = "\033[5;47m"; /* 淡棕色流行饼 */
            colors['$'] = "\033[5;45m"; /* 粉色流行饼 */
            colors['-'] = "\033[5;41m"; /* 红色流行饼 */
            colors['>'] = "\033[5;41m"; /* 红色彩虹 */
            colors['&'] = "\033[25;43m"; /* 橙色彩虹 */
            colors['+'] = "\033[5;43m"; /* 黄色彩虹 */
            colors['#'] = "\033[5;42m"; /* 绿色彩虹 */
            colors['='] = "\033[25;44m"; /* 浅蓝色彩虹 */
            colors[';'] = "\033[5;44m"; /* 深蓝色彩虹 */
            colors['*'] = "\033[5;40m"; /* 灰色猫脸 */
            colors['%'] = "\033[5;45m"; /* 粉红色脸颊 */
            break;
        case 4:
            colors[','] = "\033[0;34;44m"; /* 蓝色背景 */
            colors['.'] = "\033[1;37;47m"; /* 白色星星 */
            colors['\''] = "\033[0;30;40m"; /* 黑色边框 */
            colors['@'] = "\033[1;37;47m"; /* 淡棕色流行饼 */
            colors['$'] = "\033[1;35;45m"; /* 粉色流行饼 */
            colors['-'] = "\033[1;31;41m"; /* 红色流行饼 */
            colors['>'] = "\033[1;31;41m"; /* 红色彩虹 */colors['&']  = "\033[0;33;43m";  /* 橙色彩虹 */
			colors['+']  = "\033[1;33;43m";  /* 黄色彩虹 */
			colors['#']  = "\033[1;32;42m";  /* 绿色彩虹 */
			colors['=']  = "\033[1;34;44m";  /* 浅蓝色彩虹 */
			colors[';']  = "\033[0;34;44m";  /* 深蓝色彩虹 */
			colors['*']  = "\033[1;30;40m";  /* 灰色猫脸 */
			colors['%']  = "\033[1;35;45m";  /* 粉色腮红 */
			output = "██";
			break;
		case 5:
			colors[',']  = "\033[0;34;44m";  /* 蓝色背景 */
			colors['.']  = "\033[1;37;47m";  /* 白色星星 */
			colors['\''] = "\033[0;30;40m";  /* 黑色边框 */
			colors['@']  = "\033[1;37;47m";  /* 棕色流质饼干 */
			colors['$']  = "\033[1;35;45m";  /* 粉色流质饼干 */
			colors['-']  = "\033[1;31;41m";  /* 红色流质饼干 */
			colors['>']  = "\033[1;31;41m";  /* 红色彩虹 */
			colors['&']  = "\033[0;33;43m";  /* 橙色彩虹 */
			colors['+']  = "\033[1;33;43m";  /* 黄色彩虹 */
			colors['#']  = "\033[1;32;42m";  /* 绿色彩虹 */
			colors['=']  = "\033[1;34;44m";  /* 浅蓝色彩虹 */
			colors[';']  = "\033[0;34;44m";  /* 深蓝色彩虹 */
			colors['*']  = "\033[1;30;40m";  /* 灰色猫脸 */
			colors['%']  = "\033[1;35;45m";  /* 粉色腮红 */
			output = "\333\333";
			break;
		case 6:
			colors[',']  = "::";             /* 蓝色背景 */
			colors['.']  = "@@";             /* 白色星星 */
			colors['\''] = "  ";             /* 黑色边框 */
			colors['@']  = "##";             /* 棕色流质饼干 */
			colors['$']  = "??";             /* 粉色流质饼干 */
			colors['-']  = "<>";             /* 红色流质饼干 */
			colors['>']  = "##";             /* 红色彩虹 */
			colors['&']  = "==";             /* 橙色彩虹 */
			colors['+']  = "--";             /* 黄色彩虹 */
			colors['#']  = "++";             /* 绿色彩虹 */
			colors['=']  = "~~";             /* 浅蓝色彩虹 */
			colors[';']  = "$$";             /* 深蓝色彩虹 */
			colors['*']  = ";;";             /* 灰色猫脸 */.colors['%']  = "()"；             /* 粉红色的脸颊 */
			always_escape = 1;
			break;
		case 7:
			colors[',']  = "。";             /* 蓝色的背景 */
			colors['.']  = "@";             /* 白色的星星 */
			colors['\''] = " ";             /* 黑色的边框 */
			colors['@']  = "#";             /* 褐色的蛋糕 */
			colors['$']  = "?";             /* 粉色的蛋糕 */
			colors['-']  = "O";             /* 红色的蛋糕 */
			colors['>']  = "#";             /* 红色的彩虹 */
			colors['&']  = "=";             /* 橙色的彩虹 */
			colors['+']  = "-";             /* 黄色的彩虹 */
			colors['#']  = "+";             /* 绿色的彩虹 */
			colors['=']  = "~";             /* 淡蓝色的彩虹 */
			colors[';']  = "$";             /* 深蓝色的彩虹 */
			colors['*']  = ";";             /* 灰色的猫脸 */
			colors['%']  = "o";             /* 粉红色的脸颊 */
			always_escape = 1;
			terminal_width = 40;
			break;
		default:
			break;
	}

	if (min_col == max_col) {
		min_col = (FRAME_WIDTH - terminal_width/2) / 2;
		max_col = (FRAME_WIDTH + terminal_width/2) / 2;
		using_automatic_width = 1;
	}

	if (min_row == max_row) {
		min_row = (FRAME_HEIGHT - (terminal_height-1)) / 2;
		max_row = (FRAME_HEIGHT + (terminal_height-1)) / 2;
		using_automatic_height = 1;
	}

	/* 尝试设置终端标题 */
	if (set_title) {
		printf("\033kNyanyanyanyanyanyanya...\033\134");
		printf("\033]1;Nyanyanyanyanyanyanya...\007");
		printf("\033]2;Nyanyanyanyanyanyanya...\007");
	}

	if (clear_screen) {
		/* 清空屏幕 */
		printf("\033[H\033[2J\033[?25l");
	} 
	else {
		printf("\033[s");
	}

	if (show_intro) {
		/* 显示消息 */
		unsigned int countdown_clock = 5;
		for (k = 0; k < countdown_clock; ++k) {
			newline(3);
			printf("                             \033[1mNyancat Telnet Server\033[0m");
			newline(2);
			printf("                   written and run by \033[1;32mK. Lange\033[1;34m @_klange\033[0m");
			newline(2);
			printf("        如果出现问题，请尝试：");
			newline(1);
        }
    }printf("                TERM=fallback telnet ..."); /* 打印 "                TERM=fallback telnet ..." */
			newline(2); /* 换行两次 */
			printf("        Or on Windows:"); /* 打印 "        Or on Windows:" */
			newline(1); /* 换行一次 */
			printf("                telnet -t vtnt ..."); /* 打印 "                telnet -t vtnt ..." */
			newline(2); /* 换行两次 */
			printf("        Problems? Check the website:"); /* 打印 "        Problems? Check the website:" */
			newline(1); /* 换行一次 */
			printf("                \033[1;34mhttp://nyancat.dakko.us\033[0m"); /* 打印 "                \033[1;34mhttp://nyancat.dakko.us\033[0m" */
			newline(2); /* 换行两次 */
			printf("        This is a telnet server, remember your escape keys!"); /* 打印 "        This is a telnet server, remember your escape keys!" */
			newline(1); /* 换行一次 */
			printf("                \033[1;31m^]quit\033[0m to exit"); /* 打印 "                \033[1;31m^]quit\033[0m to exit" */
			newline(2); /* 换行两次 */
			printf("        Starting in %d...                \n", countdown_clock-k); /* 打印 "        Starting in %d...                \n", countdown_clock-k */

			fflush(stdout);
			usleep(400000);
			if (clear_screen) {
				printf("\033[H"); /* 重置光标 */
			} else {
				printf("\033[u");
			}
		}

		if (clear_screen) {
			/* 再次清屏 */
			printf("\033[H\033[2J\033[?25l");
		}
	}

	/* 存储开始时间 */
	time_t start, current;
	time(&start);

	int playing = 1;    /* 动画应继续 [此处留作修改] */
	size_t i = 0;       /* 当前帧数 */
	unsigned int f = 0; /* 已经经过的帧数 */
	char last = 0;      /* 上一个渲染的颜色索引 */
	int y, x;        /* 正在绘制的 x/y 坐标 */
	while (playing) {
		/* 重置光标 */
		if (clear_screen) {
			printf("\033[H");
		} else {
			printf("\033[u");
		}
		/* 渲染帧 */
		for (y = min_row; y < max_row; ++y) {
			for (x = min_col; x < max_col; ++x) {
				char color;
				if (y > 23 && y < 43 && x < 0) {
					/*
					 * 生成彩虹尾巴。
					 *
					 * 这是通过相当简单的方波实现的。
					 */
					int mod_x = ((-x+2) % 16) / 8;
					if ((i / 2) % 2) {
						mod_x = 1 - mod_x;
					}
					/*
					 * 我们的彩虹，带有一些间距。
					 */
					const char *rainbow = ",,>>&&&+++###==;;;,,";
					color = rainbow[mod_x + y-23];
					if (color == 0) color = ',';
				} else if (x < 0 || y < 0 || y >= FRAME_HEIGHT || x >= FRAME_WIDTH) {
					/* 填充所有其他区域的背景 */
					color = ',';
				} else {"。/* 否则，从动画帧获取颜色。 */
				color = frames[i][y][x];
				}
				if (always_escape) {
					/* 文本模式（或“始终发送颜色转义”） */
					printf("%s", colors[(int)color]);
				} else {
					if (color != last && colors[(int)color]) {
						/* 正常模式，发送转义（因为颜色已更改） */
						last = color;
						printf("%s%s", colors[(int)color], output);
					} else {
						/* 颜色相同，只发送输出字符 */
						printf("%s", output);
					}
				}
			}
			/* 行尾，发送换行符 */
			newline(1);
		}
		if (show_counter) {
			/* 获取当前时间以获取“您已经nyaned...”字符串 */
			time(&current);
			double diff = difftime(current, start);
			/* 现在计算时间差的长度，以便使其居中 */
			int nLen = digits((int)diff);
			/*
			 * 29=字符串的其余长度;
			 * XXX：将此替换为实际检查从sprintf或其他调用中编写的字节
			 */
			int width = (terminal_width - 29 - nLen) / 2;
			/* 吐出一些空格，以便我们实际上是居中的 */
			while (width > 0) {
				printf(" ");
				width--;
			}
			/* 您已经nyaned了[n]秒！
			 * \ 033[J确保其余的行具有深蓝色
			 * 背景，\ 033 [1; 37m确保我们的文本是亮白色的。
			 * \ 033 [0m防止Apple ][翻转所有内容，
			 * 但使vt220上的整个nyancat变得不那么明亮
			 */
			printf("\ 033 [1; 37m您已经nyaned了%0.0f秒！\ 033 [J\ 033 [0m", diff);
		}
		/* 重置上一个颜色以使转义序列重写 */ 
		last = 0;
		/* 更新帧计数 */ 
		++f;
		if (frame_count！= 0 && f == frame_count) {
			finish();
		}
		++i;
		if (!frames[i]) {
			/* 循环动画 */
			i = 0;
		}
		/* 等待 */
		usleep(1000 * delay_ms);
	}
	return 0;
}