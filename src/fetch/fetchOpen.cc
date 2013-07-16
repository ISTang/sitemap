// Larbin
// Sebastien Ailleret
// 15-11-99 -> 04-12-01

#include <iostream>
#include <errno.h>
#include <sys/types.h>

#include <adns.h>

#include "options.h"

#include "global.h"
#include "utils/Fifo.h"
#include "utils/debug.h"
#include "fetch/site.h"

/* Opens sockets
 * Never block (only opens sockets on already known sites)
 * work inside the main thread
 */
void fetchOpen() {
	static time_t next_call = 0;
	if (global::now < next_call) { // too early to come back
		return;
	}
	int cont = 1;
	while (cont && global::freeConns->isNonEmpty()) {
		IPSite *s = global::okSites->tryGet();
		if (s == NULL) {
			cont = 0;
		} else {
			next_call = s->fetch(); // 创建连接并发送请求
			cont = (next_call == 0);
		}
	}
}

/* Opens sockets
 * this function perform dns calls, using adns
 */
void fetchDns() {
	// Submit queries
	while (global::nbDnsCalls < global::dnsConn
			&& global::freeConns->isNonEmpty() && global::IPUrl < maxIPUrls) { // try to avoid too many dns calls
		// 尝试取出一个命名站点
		NamedSite *site = global::dnsSites->tryGet();
		if (site == NULL) {
			// 没有了
			break;
		} else {
			// 取到了(发起DNS查询)
			site->newQuery();
		}
	}

	// Read available answers
	while (global::nbDnsCalls && global::freeConns->isNonEmpty()) {
		NamedSite *site;
		adns_query quer = NULL;
		adns_answer *ans;
		// 检查DNS查询执行情况
		int res = adns_check(global::ads, &quer, &ans, (void**) &site);
		if (res == ESRCH || res == EAGAIN) {
			// No more query or no more answers
			break;
		}
		global::nbDnsCalls--;
		// 处理DNS查询响应
		site->dnsAns(ans);
		free(ans); // ans has been allocated with malloc
	}
}
