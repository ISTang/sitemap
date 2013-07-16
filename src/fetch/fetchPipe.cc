// Larbin
// Sebastien Ailleret
// 15-11-99 -> 19-11-01

#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>

#include "options.h"

#include "types.h"
#include "global.h"
#include "utils/url.h"
#include "utils/text.h"
#include "utils/string.h"
#include "utils/connexion.h"
#include "fetch/site.h"
#include "fetch/file.h"
#include "interf/output.h"

#include "interf/useroutput.h"

#include "utils/debug.h"

static void pipeRead(Connexion *conn);
static void pipeWrite(Connexion *conn);
static void endOfFile(Connexion *conn);

/** Check timeout
 */
void checkTimeout() {
	//char buf[3072];
	for (uint i = 0; i < global::nb_conn; i++) {
		Connexion *conn = global::connexions + i;
		if (conn->state != emptyC) {
			if (conn->timeout > 0) {
				if (conn->timeout > timeoutPage) {
					conn->timeout = timeoutPage;
				} else {
					conn->timeout--;
				}
			} else {
				// This server doesn't answer (time out)
				if (conn->parser->isRobots) {
					NamedSite *server = ((robots*) conn->parser)->server;
					char hostInfo[64];
					sprintf(hostInfo, "%s:%d", server->name, server->port);
					outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETERR, "下载超时");
					//sprintf(buf, "从服务器 %s:%d 下载 robots.txt 超时", server->name,
					//		server->port);
				} else {
					url *u = ((html *) conn->parser)->getUrl();
					outputDetails(URL_TARGET, u->getUrl(), URL_GETERR, "下载超时");
					//sprintf(buf, "下载页面  %s 超时[%ld]", u->getUrl(), global::now);
				}
				//syslog(LOG_WARNING, buf);
				//
				conn->err = timeout;
				endOfFile(conn);
			}
		}
	}
}

/** read all data available
 * fill fd_set for next select
 * give back max fds
 */
void checkAll() {
	// read and write what can be
	for (uint i = 0; i < global::nb_conn; i++) {
		Connexion *conn = global::connexions + i;
		switch (conn->state) {
		case connectingC:
		case writeC:
			if (global::ansPoll[conn->socket]) {
				// trying to finish the connection
				pipeWrite(conn);
			}
			break;
		case openC:
			if (global::ansPoll[conn->socket]) {
				// The socket is open, let's try to read it
				pipeRead(conn);
			}
			break;
		}
	}

	// update fd_set for the next select
	for (uint i = 0; i < global::nb_conn; i++) {
		int n = (global::connexions + i)->socket;
		switch ((global::connexions + i)->state) {
		case connectingC:
		case writeC:
			global::setPoll(n, POLLOUT);
			break;
			case openC:
			global::setPoll(n, POLLIN);
			break;
		}
	}
}

/** The socket is finally open !
 * Make sure it's all right, and write the request
 */
static void pipeWrite(Connexion *conn) {
	int res = 0;
	int wrtn, len;
	socklen_t size = sizeof(int);
	//
	//char buf[3072];
	switch (conn->state) {
	case connectingC:
		// not connected yet
		getsockopt(conn->socket, SOL_SOCKET, SO_ERROR, &res, &size);
		if (res) {
			// Unable to connect
			if (conn->parser->isRobots) {
				NamedSite *server = ((robots*) conn->parser)->server;
				char hostInfo[64];
				sprintf(hostInfo, "%s:%d", server->name, server->port);
				outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETERR, "不能创建连接");
				//sprintf(buf, "无法从服务器  %s:%d 下载 robots.txt: 不能创建连接[%ld]",
				//		server->name, server->port, global::now);
			} else {
				url *u = ((html *) conn->parser)->getUrl();
				outputDetails(URL_TARGET, u->getUrl(), URL_GETERR, "不能创建连接");
				//sprintf(buf, "无法下载页面 %s: 不能创建连接[%ld]", u->getUrl(),
				//		global::now);
			}
			//syslog(LOG_WARNING, buf);
			//
			conn->err = noConnection;
			endOfFile(conn);
			return;
		}
		// Connection succesfull
		if (conn->parser->isRobots) {
			NamedSite *server = ((robots*) conn->parser)->server;
			char hostInfo[64];
			sprintf(hostInfo, "%s:%d", server->name, server->port);
			outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETERR, "已创建连接");
			//sprintf(buf, "准备从服务器 %s:%d 下载 robots.txt(已创建连接)...[%ld]",
			//		server->name, server->port, global::now);
		} else {
			url *u = ((html *) conn->parser)->getUrl();
			outputDetails(URL_TARGET, u->getUrl(), URL_GETTING, "已创建连接");
			//sprintf(buf, "准备下载页面 %s (已创建连接)...[%ld]", u->getUrl(), global::now);
		}
		//syslog(LOG_INFO, buf);
		//
		conn->state = writeC;
		// no break
	case writeC:
		// writing the first string
		len = strlen(conn->request.getString());
		wrtn = write(conn->socket, conn->request.getString() + conn->pos,
				len - conn->pos);
		if (wrtn >= 0) {
			addWrite(wrtn);
#ifdef MAXBANDWIDTH
			global::remainBand -= wrtn;
#endif // MAXBANDWIDTH
			conn->pos += wrtn;
			if (conn->pos < len) {
				// Some chars of this string are not written yet
				return;
			}
		} else {
			if (errno == EAGAIN || errno == EINTR || errno == ENOTCONN) {
				// little error, come back soon
				return;
			} else {
				// unrecoverable error, forget it
				if (conn->parser->isRobots) {
					NamedSite *server = ((robots*) conn->parser)->server;
					char hostInfo[64];
					sprintf(hostInfo, "%s:%d", server->name, server->port);
					outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETERR, "发送请求失败");
					//sprintf(buf, "从服务器  %s:%d 下载 robots.txt 失败: 发送请求失败[%ld]",
					//		server->name, server->port, global::now);
				} else {
					url *u = ((html *) conn->parser)->getUrl();
					outputDetails(URL_TARGET, u->getUrl(), URL_GETERR, "发送请求失败");
					//sprintf(buf, "无法下载页面 %s: 发送请求失败[%ld]", u->getUrl(),
					//		global::now);
				}
				//syslog(LOG_WARNING, buf);
				//
				conn->err = earlyStop;
				endOfFile(conn);
				return;
			}
		}
		// All the request has been written
		if (conn->parser->isRobots) {
			NamedSite *server = ((robots*) conn->parser)->server;
			char hostInfo[64];
			sprintf(hostInfo, "%s:%d", server->name, server->port);
			outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETTING, "请求已发送");
			//sprintf(buf, "准备从服务器 %s:%d 下载 robots.txt(请求已发送)[%ld]",
			//		server->name, server->port, global::now);
		} else {
			url *u = ((html *) conn->parser)->getUrl();
			outputDetails(URL_TARGET, u->getUrl(), URL_GETTING, "请求已发送");
			//sprintf(buf, "准备下载页面 %s (请求已发送)[%ld]", u->getUrl(), global::now);
		}
		//syslog(LOG_INFO, buf);
		//
		conn->state = openC;
	}
}

/** Is there something to read on this socket
 * (which is open)
 */
static void pipeRead(Connexion *conn) {
	int p = conn->parser->pos;
	int size = read(conn->socket, conn->buffer + p, maxPageSize - p - 1);
	//char buf[3072];
	switch (size) {
	case 0:
		// End of file
		if (conn->parser->isRobots) {
			NamedSite *server = ((robots *) conn->parser)->server;
			char hostInfo[64];
			sprintf(hostInfo, "%s:%d", server->name, server->port);
			outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETOK, NULL);
			//sprintf(buf, "已从服务器 %s:%d 下载 robots.txt[%ld]", server->name,
			//		server->port, global::now);
		} else {
			url *u = ((html *) conn->parser)->getUrl();
			outputDetails(URL_TARGET, u->getUrl(), URL_GETOK, "已下载");
			//sprintf(buf, "已下载页面 %s[%ld]",
			//		((html *) conn->parser)->getUrl()->getUrl(), global::now);
		}
		//syslog(LOG_INFO, buf);
		//
		if (conn->parser->endInput()) {

			if (conn->parser->isRobots) {
				NamedSite *server = ((robots *) conn->parser)->server;
				char hostInfo[64], errmsg[32];
				sprintf(hostInfo, "%s:%d", server->name, server->port);
				sprintf(errmsg, "错误代码: %d", errno);
				outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_PARSEERR, errmsg);
				//sprintf(buf, "无法解析从服务器 %s:%d 下载的 robots.txt: #%d[%ld]",
				//		server->name, server->port, errno, global::now);
			} else {
				url *u = ((html *) conn->parser)->getUrl();
				char errmsg[32];
				sprintf(errmsg, "错误代码: %d", errno);
				outputDetails(URL_TARGET, u->getUrl(), URL_PARSEERR, errmsg);
				//sprintf(buf, "无法解析页面 %s: #%d[%ld]",
				//		((html *) conn->parser)->getUrl()->getUrl(), errno,
				//		global::now);
			}
			//syslog(LOG_WARNING, buf);
			//
			conn->err = (FetchError) errno;
		} else {
			if (conn->parser->isRobots) {
				NamedSite *server = ((robots *) conn->parser)->server;
				char hostInfo[64];
				sprintf(hostInfo, "%s:%d", server->name, server->port);
				outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_PARSEOK, NULL);
				//sprintf(buf, "已成功解析从服务器 %s[%d] 下载的 robots.txt[%ld]",
				//		server->name, server->port, global::now);
			} else {
				//url *u = ((html *) conn->parser)->getUrl();
				//outputDetails(URL_TARGET, u->getUrl(), URL_PARSEOK, NULL);
				//sprintf(buf, "已成功解析页面 %s [%ld]",
				//		((html *) conn->parser)->getUrl()->getUrl(),
				//		global::now);
			}
			//syslog(LOG_INFO, buf);
		}
		//
		endOfFile(conn);
		break;
	case -1:
		switch (errno) {
		case EAGAIN:
			// Nothing to read now, we'll try again later
			break;
		default:
			// Error : let's forget this page
			if (conn->parser->isRobots) {
				NamedSite *server = ((robots *) conn->parser)->server;
				char hostInfo[64];
				sprintf(hostInfo, "%s:%d", server->name, server->port);
				outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETERR, "未能完整下载");
				//sprintf(buf, "未能从服务器 %s:%d 下载完整的 robots.txt[%ld]",
				//		server->name, server->port, global::now);
			} else {
				url *u = ((html *) conn->parser)->getUrl();
				outputDetails(URL_TARGET, u->getUrl(), URL_GETERR, "未能完整下载");
				//sprintf(buf, "未能下载完整的页面 %s[%ld]",
				//		((html *) conn->parser)->getUrl()->getUrl(),
				//		global::now);
			}
			//syslog(LOG_WARNING, buf);
			//
			conn->err = earlyStop;
			endOfFile(conn);
			break;
		}
		break;
	default:
		// Something has been read
		conn->timeout += size / timeoutIncr;
		addRead(size);
#ifdef MAXBANDWIDTH
		global::remainBand -= size;
#endif // MAXBANDWIDTH
		if (conn->parser->inputHeaders(size) == 0) {
			// nothing special
			if (conn->parser->pos >= maxPageSize - 1) {
				// We've read enough...
				if (conn->parser->isRobots) {
					NamedSite *server = ((robots *) conn->parser)->server;
					char hostInfo[64];
					sprintf(hostInfo, "%s:%d", server->name, server->port);
					outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETERR, "内容过多");
					//sprintf(buf, "从服务器 %s:%d 下载的 robots.txt 内容过多[%ld]",
					//		server->name, server->port, global::now);
				} else {
					url *u = ((html *) conn->parser)->getUrl();
					outputDetails(URL_TARGET, u->getUrl(), URL_GETERR, "内容过多");
					//sprintf(buf, "页面 %s 内容过多[%ld]",
					//		((html *) conn->parser)->getUrl()->getUrl(),
					//		global::now);
				}
				//syslog(LOG_WARNING, buf);
				//
				conn->err = tooBig;
				endOfFile(conn);
			} else {
				if (p==0) {
					if (conn->parser->isRobots) {
						NamedSite *server = ((robots *) conn->parser)->server;
						char hostInfo[64], lenStr[32];
						sprintf(hostInfo, "%s:%d", server->name, server->port);
						sprintf(lenStr, "已下载 %d 字节", size);
						outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETTING, lenStr);
						//sprintf(buf, "正在从服务器 %s:%d 下载 robots.txt: %dB[%ld]",
						//		server->name, server->port, size, global::now);
					} else {
						url *u = ((html *) conn->parser)->getUrl();
						char lenStr[32];
						sprintf(lenStr, "已下载 %d 字节", size);
						outputDetails(URL_TARGET, u->getUrl(), URL_GETTING, lenStr);
						//sprintf(buf, "正在下载页面 %s: %dB[%ld]",
						//		((html *) conn->parser)->getUrl()->getUrl(), size,
						//		global::now);
					}
					//syslog(LOG_WARNING, buf);
				}
			}
		} else {
			// The parser does not want any more input (errno explains why)
			if (conn->parser->isRobots) {
				NamedSite *server = ((robots *) conn->parser)->server;
				char hostInfo[64];
				sprintf(hostInfo, "%s:%d", server->name, server->port);
				outputDetails(ROBOTS_TARGET, hostInfo, ROBOTS_GETERR, "过多的输入");
				//sprintf(buf, "从服务器 %s:%d 下载 robots.txt 时出错: 过多的输入(#%ld)[%ld]",
				//		server->name, server->port, (long) errno, global::now);
			} else {
				url *u = ((html *) conn->parser)->getUrl();
				outputDetails(URL_TARGET, u->getUrl(), URL_GETERR, "过多的输入");
				//sprintf(buf, "下载页面 %s 出错: 过多的输入(#%ld)[%ld]",
				//		((html *) conn->parser)->getUrl()->getUrl(),
				//		(long) errno, global::now);
			}
			//syslog(LOG_WARNING, buf);
			//
			conn->err = (FetchError) errno;
			endOfFile(conn);
		}
		break;
	}
}

/* What are we doing when it's over with one file ? */

#ifdef THREAD_OUTPUT
#define manageHtml() global::userConns->put(conn)
#else // THREAD_OUTPUT
#define manageHtml() \
    endOfLoad((html *)conn->parser, conn->err); \
    conn->recycle(); \
    global::freeConns->put(conn)
#endif // THREAD_OUTPUT
static void endOfFile(Connexion *conn) {
	//crash("文件结束");
	conn->state = emptyC;
	close(conn->socket);
	if (conn->parser->isRobots) {
		// That was a robots.txt
		robots *r = ((robots *) conn->parser);
		r->parse(conn->err != success);
		r->server->robotsResult(conn->err);
		conn->recycle();
		global::freeConns->put(conn);
	} else {
		// that was an html page
		manageHtml();
	}
}
