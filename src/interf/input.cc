// Larbin
// Sebastien Ailleret
// 03-02-00 -> 23-11-01

#include <unistd.h>
#include <iostream>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>

#include "options.h"

#include "types.h"
#include "global.h"
#include "utils/text.h"
#include "utils/debug.h"
#include "interf/input.h"

#define INIT -1
#define END -2

/* input connexion */
struct Input {
	int fds;
	uint pos;
	uint end_pos;
	uint end_posp;
	int priority;
	uint depth;
	uint test;
	char buffer[BUF_SIZE];
};

/** socket used for input */
static int inputFds;
/** number of opened input connections */
static int nbInput;
/** array for the opened input connections */
static Input *inputConns[maxInput];

// declaration of forward functions
static bool readMore(Input *in);
static char *readline(Input *in);

int input() {
	if (nbInput < 0) {
		// Input is disabled
		return -1;
	}
	int n = -1;
	if (nbInput < maxInput - 1 && global::ansPoll[inputFds]) {
		// test if there is a new connection
		struct sockaddr_in addr;
		int fdc;
		socklen_t len = sizeof(addr);
		fdc = accept(inputFds, (struct sockaddr *) &addr, &len);
		if (fdc != -1) {
			global::verifMax(fdc);
			fcntl(fdc, F_SETFL, O_NONBLOCK);
			inputConns[nbInput]->fds = fdc;
			inputConns[nbInput]->pos = 0;
			inputConns[nbInput]->end_pos = 0;
			inputConns[nbInput]->end_posp = 0;
			inputConns[nbInput]->priority = INIT;
			nbInput++;
#ifdef URL_TAGS
			ecrire(fdc,
					"欢迎使用 sitemap 输入系统!\n第一行看起来应该像这个样子 \"priority:0 depth:5 test:1\"\n后面各行应该包含 ID 和 URL，并用一个空格隔开\n例如，\"137 http://pauillac.inria.fr/~ailleret/prog/larbin/\"\n\n");
#else
			ecrire(fdc,
					"欢迎使用 sitemap 输入系统!\n第一行看起来应该像这个样子 \"priority:0 depth:5 test:1\"\n后面各行应该包含 URL(例如，http://pauillac.inria.fr/~ailleret/prog/larbin/)\n\n");
#endif // URL_TAGS
		}
	}
	if (nbInput < maxInput - 1) {
		n = inputFds;
		global::setPoll(inputFds, POLLIN);
	}
	// read open sockets
	int i = 0;
	while (i < nbInput) {
		Input *in = inputConns[i];
		if (global::ansPoll[in->fds] && readMore(in)) {
			char *line = readline(in);
			while (line != NULL) {
				if (in->priority == INIT) {
					// first line
					if (sscanf(line, "priority:%d depth:%u test:%u",
							&in->priority, &in->depth, &in->test) == 3) {
						line = readline(in);
					} else {
						ecrire(in->fds, "输入不正确\n");
						line = NULL;
						in->priority = END;
					}
				} else {
					// this is an url
					url *u = new url(line, in->depth);
					char buf[3072];
					if (u->isValid()) {
						// URL有效
						if (in->test) {
							// 需要判断该URL是否已经存在
							if (global::seen->testSet(u)) {
								// 该URL尚不存在
								hashUrls();
								// stats
								if (in->priority) {
									// 该URL优先爬取
									global::URLsPriority->put(u);
									//
									sprintf(buf, "主页 %s 已进入优先队列", line);
									syslog(LOG_INFO, buf);
								} else {
									// 该URL不需要优先爬取
									global::URLsDisk->put(u);
									//
									sprintf(buf, "主页 %s 已进入非优先队列", line);
									syslog(LOG_INFO, buf);
								}
							} else {
								// 该URL已经存在
								sprintf(buf, "主页 %s 已存在", line);
								syslog(LOG_WARNING, buf);
								//
								delete u;
							}
						} else {
							// 不需要判断该URL是否已经存在
							hashUrls();
							// stats
							global::seen->set(u);
							if (in->priority) {
								// 该URL优先爬取
								global::URLsPriority->put(u);
								//
								sprintf(buf, "主页 %s 已进入优先队列", line);
								syslog(LOG_INFO, buf);
							} else {
								// 该URL不需要优先爬取
								global::URLsDisk->put(u);
								//
								sprintf(buf, "主页 %s 已进入非优先队列", line);
								syslog(LOG_INFO, buf);
							}
						}
					} else {
						// URL无效
						sprintf(buf, "主页行  %s 无效", line);
						syslog(LOG_WARNING, buf);
						//
						delete u;
					}
					line = readline(in);
				}
			}
		}
		if (in->priority == END) {
			// forget this connection
			ecrire(in->fds, "再见...\n");
			close(in->fds);
			nbInput--;
			Input *tmp = inputConns[i];
			inputConns[i] = inputConns[nbInput];
			inputConns[nbInput] = tmp;
		} else { // go to next connection
			if (in->fds > n)
				n = in->fds;
			global::setPoll(in->fds, POLLIN);
			i++;
		}
	}
	return n;
}

static bool readMore(Input *in) {
	assert(in->end_posp == in->end_pos);
	if (in->end_posp - in->pos > maxUrlSize + 100) {
		// error -> stop connection
		ecrire(in->fds, "URL 太长\n");
		in->priority = END;
		return false;
	}
	if (2 * in->pos > BUF_SIZE) {
		in->end_pos -= in->pos;
		in->end_posp = in->end_pos;
		memmove(in->buffer, in->buffer + in->pos, in->end_pos);
		in->pos = 0;
	}
	int nb = read(in->fds, in->buffer + in->end_pos, BUF_SIZE - in->end_pos);
	if (nb == -1 && errno == EAGAIN) {
		return false;
	} else if (nb <= 0) {
		in->priority = END;
		return false;
	} else {
		in->end_pos += nb;
		return true;
	}
}

/* no allocation */
static char *readline(Input *in) {
	while (in->end_posp < in->end_pos && in->buffer[in->end_posp] != '\n') {
		in->end_posp++;
	}
	if (in->end_posp == in->end_pos) {
		return NULL;
	} else {
		if (in->buffer[in->end_posp - 1] == '\r') {
			in->buffer[in->end_posp - 1] = 0;
		} else {
			in->buffer[in->end_posp] = 0;
		}
		char *res = in->buffer + in->pos;
		in->pos = ++in->end_posp;
		return res;
	}
}

////////////////////////////////////////////////////
/** init everything */
void initInput() {
	if (global::inputPort != 0) {
		int allowReuse = 1;
		struct sockaddr_in addr;
		memset((void *) &addr, 0, sizeof(addr));
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(global::inputPort);
		if ((inputFds = socket(AF_INET, SOCK_STREAM, 0)) == -1
				|| setsockopt(inputFds, SOL_SOCKET, SO_REUSEADDR,
						(char*) &allowReuse, sizeof(allowReuse))
				|| bind(inputFds, (struct sockaddr *) &addr, sizeof(addr)) != 0
				|| listen(inputFds, 4) != 0) {
			syslog(LOG_ERR, "不能为输入子系统获得套结字");
			exit(1);
		}
		fcntl(inputFds, F_SETFL, O_NONBLOCK);
		for (int i = 0; i < maxInput; i++) {
			inputConns[i] = new Input;
		}
		nbInput = 0;
	} else {
		nbInput = -1;
	}
}
