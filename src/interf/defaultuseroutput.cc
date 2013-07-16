// Sitemap

#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <iconv.h>
#include <cstdlib>
#include <syslog.h>
#include <time.h>

#include <hiredis/hiredis.h>
#include <mongo/client/dbclient.h>

#include "options.h"

#include "types.h"
#include "global.h"
#include "fetch/file.h"
#include "utils/text.h"
#include "utils/debug.h"
#include "interf/output.h"
#include "utils/mypthread.h"

#include "interf/useroutput.h"

#define MAX_ENCODING 20
#define MAX_TEXT 4096

redisContext *c;
mongo::DBClientConnection cc, cc2;
pthread_mutex_t lock;

static const char *linkTypes[] = {
	"??",
	"base",
	"anchor",
	"script",
	"link",
	"frame",
	"iframe",
	"image"
};

static const char *fetchErrors[] = {
  "success",
  "noDNS",
  "noConnection",
  "forbiddenRobots",
  "timeout",
  "badType",
  "tooBig",
  "err30X",
  "err40X",
  "earlyStop",
  "duplicate",
  "fastRobots",
  "fastNoConn",
  "fastNoDns",
  "tooDeep",
  "urlDup"
};

static const char *targetTypeNames[] = {
	/*MAIN_TARGET*/ "域名",
	/*ROBOTS_TARGET*/ "robots.txt",
	/*URL_TARGET*/ "网址",
};

static const char *targetActionNames[] = {
	/*DOMAIN_SUBMIT*/ "提交域名解析查询",
	/*DOMAIN_CNAME*/ "根据 CNAME 查 IP 地址",
	/*DOMAIN_OK*/ "域名解析成功",
	/*DOMAIN_ERROR*/ "域名解析失败",
	//
	/*ROBOTS_GETTING*/ "正下载 robots.txt",
	/*ROBOTS_GETERR*/ "robots.txt 下载失败",
	/*ROBOTS_GETOK*/ "已下载 robots.txt",
	/*ROBOTS_PARSEOK*/ "已成功解析 robots.txt",
	/*ROBOTS_PARSEERR*/ "robots.txt 解析失败",
	//
	/*URL_IGNORED*/ "URL 被忽略",
	/*URL_QUEUE*/ "URL 加入队列",
	/*URL_GETTING*/ "正下载页面",
	/*URL_GETERR*/ "页面下载失败",
	/*URL_GETOK*/ "已下载页面",
	/*URL_PARSEOK*/ "已成功解析页面",
	/*URL_PARSEERR*/ "页面解析失败"
};

static void encodeToUTF8(char **pstr, char *fromEncoding) {
	iconv_t hIconv = iconv_open("UTF-8", fromEncoding);
	if (-1 == (long) hIconv) {
		sprintf(*pstr, "<<Unsupported %s encoding>>", fromEncoding);
	} else {
		char inBuf[MAX_TEXT + 1];
		char outBuf[MAX_TEXT * 2 + 1];
		memset(inBuf, 0, sizeof inBuf);
		memset(outBuf, 0, sizeof outBuf);
		strncpy(inBuf, *pstr, MAX_TEXT);
		size_t inLen = strlen(inBuf);
		size_t outLen = sizeof outBuf - 1;
		char *s = inBuf;
		char *d = outBuf;
		iconv(hIconv, (char**) (&s), &inLen, (char**) (&d), &outLen);

		delete[] *pstr;
		*pstr = new char[outLen + 1];
		strcpy(*pstr, outBuf);

		iconv_close(hIconv);
	}
}

/** A page has been loaded successfully
 * @param page the page that has been fetched
 */
void loaded(html *page) {
	// Here should be the code for managing everything
	// page->getHeaders() gives a char* containing the http headers
	// page->getPage() gives a char* containing the page itself
	// those char* are statically allocated, so you should copy
	// them if you want to keep them
	// in order to accept \000 in the page, you can use page->getLength()
	char *pageUrl = page->getUrl()->giveUrl();

	char pageInfo[64];
	sprintf(pageInfo, "页面头部大小: %dB, 体部大小: %dB",
			strlen(page->getHeaders()), strlen(page->getPage()));
	outputDetails(URL_TARGET, pageUrl, URL_PARSEOK, pageInfo);

	// skip duplicate page
//#ifndef NDEBUG
//  std::cout<<"检查页面 "<<pageUrl<<"..."<<std::endl;
//#endif
	bool pageExists = (cc.count("sitemap.page", BSON("url" << pageUrl)) > 0);

	if (pageExists) {
//#ifndef NDEBUG
//    std::cerr<<"页面 "<<pageUrl<<" 已经存在"<<std::endl;
//#endif
		return;
	}

	// detect page encoding
//#ifndef NDEBUG
//      std::cout<<"检测页面 "<<pageUrl<<" 编码..."<<std::endl;
//#endif // NDEBUG
	char pageEncoding[MAX_ENCODING + 1];
	memset(pageEncoding, 0, sizeof pageEncoding);
	char *pContentTypeBegin = strcasestr(page->getPage(),
			"<meta http-equiv=\"Content-Type\" content=\"");
	if (pContentTypeBegin) {
		pContentTypeBegin += 41;
		char *pContentTypeEnd = strcasestr(pContentTypeBegin, "\"");
		if (pContentTypeEnd) {
			char *contentType =
					new char[pContentTypeEnd - pContentTypeBegin + 1];
			memset(contentType, 0, pContentTypeEnd - pContentTypeBegin + 1);
			strncpy(contentType, pContentTypeBegin,
					pContentTypeEnd - pContentTypeBegin);
			//
			char *pCharsetBegin = strcasestr(contentType, "charset=");
			if (pCharsetBegin) {
				pCharsetBegin += 8;
				char *pCharsetEnd = strchr(pCharsetBegin, ';');
				if (!pCharsetEnd)
					pCharsetEnd = pCharsetBegin + strlen(contentType);
				if (strncmp(pCharsetBegin, "text/html", 9)) {
					strncpy(pageEncoding, pCharsetBegin,
							std::min(MAX_ENCODING,
									int(pCharsetEnd - pCharsetBegin)));
				}
			}
			delete[] contentType;
//#ifndef NDEBUG
//      std::cout<<"页面 "<<pageUrl<<" 编码: "<<pageEncoding<<std::endl;
//#endif // NDEBUG
		}
	} else {
		char *pCharsetBegin = strcasestr(page->getPage(), "<meta charset=\"");
		if (pCharsetBegin) {
			pCharsetBegin += 15;
			char *pCharsetEnd = strchr(pCharsetBegin, '\"');
			if (pCharsetEnd) {
				strncpy(pageEncoding, pCharsetBegin,
						std::min(MAX_ENCODING,
								int(pCharsetEnd - pCharsetBegin)));
			}
		}
	}

	// page title
//#ifndef NDEBUG
//  std::cout<<"获取页面 "<<pageUrl<<" 标题..."<<std::endl;
//#endif
	char *pageTitle = NULL;
	char *p = strcasestr(page->getPage(), "<title>");
	if (p) {
		char *q = strcasestr(p + 7, "</title>");
		if (q) {
			pageTitle = new char[q - p - 7 + 1];
			strncpy(pageTitle, p + 7, q - p - 7);
			*(pageTitle + (q - p - 7)) = '\0';
		}
	}
	if (pageTitle && *pageEncoding != '\0' && strcasecmp(pageEncoding, "utf-8")) {
		encodeToUTF8(&pageTitle, pageEncoding);
	}

	if (page->getUrl()->tag > 0
			&& cc.count("sitemap.site", BSON("tag" << page->getUrl()->tag))
					== 0) {

		cc.insert("sitemap.site",
				BSON("url" << pageUrl << "tag" << page->getUrl()->tag));
	}

	mongo::BSONObjBuilder b;
	b.append("url", pageUrl);
	b.append("title", (pageTitle == NULL ? "" : pageTitle));

	mongo::BSONArrayBuilder b2, b3/*, b4, b5*/;
	Vector<LinkInfo> *links = page->getLinks();
	std::set < std::string > appendedlinkUrls;
	for (unsigned i = 0; i < links->getLength(); i++) {

		LinkInfo *linkInfo = (*links)[i];
		char *linkUrl = linkInfo->url;
		TagType linkType = linkInfo->type;

		if (*pageEncoding != '\0' && strcasecmp(pageEncoding, "utf-8")) {
			encodeToUTF8(&linkUrl, pageEncoding);
		}

		if (strcmp(pageUrl, linkUrl) != 0
				&& appendedlinkUrls.count(linkUrl) == 0) {

			if (linkType != ttAnchor && linkType != ttFrame
					&& linkType != ttIFrame) {

				if (page->getUrl()->tag > 0) {
					//b4.append(linkUrl);
					redisReply *reply = (redisReply *) redisCommand(c,
							"sadd site:%d:links_res [%s]%s", page->getUrl()->tag,
							linkTypes[linkType], linkUrl);
					freeReplyObject(reply);
				}
				b2.append(
						BSON(
								"url" << linkUrl << "type"
										<< linkTypes[linkType]));
			} else {

				if (page->getUrl()->tag > 0) {
					//b5.append(linkUrl);
					redisReply *reply = (redisReply *) redisCommand(c,
							"sadd site:%d:links_page %s", page->getUrl()->tag,
							linkUrl);
					freeReplyObject(reply);
				}
				b3.append(
						BSON(
								"url" << linkUrl << "type"
										<< linkTypes[linkType]));
			}
			appendedlinkUrls.insert(linkUrl);
		}

		delete[] linkUrl;
	}

	b.append("links_res", b2.arr());
	b.append("links_page", b3.arr());
	cc.insert("sitemap.page", b.obj());
	//cc.ensureIndex("sitemap.page", BSON("url" << 1));
	//cc.update("sitemap.site", QUERY("tag"<<page->getUrl()->tag), BSON("$addToSet"<<BSON("links_res"<<BSON("$each"<<b4.arr()))), true);
	//cc.update("sitemap.site", QUERY("tag"<<page->getUrl()->tag), BSON("$addToSet"<<BSON("links_page"<<BSON("$each"<<b5.arr()))), true);

	delete[] pageUrl;
}

/** The fetch failed
 * @param u the URL of the doc
 * @param reason reason of the fail
 */
void failure(url *u, FetchError reason) {
	// Here should be the code for managing everything
	char *url = u->giveUrl();

	cc.insert("sitemap.failed", BSON("url" << url << "reason" << fetchErrors[(int)reason]));

	delete url;
}

/** initialisation function
 */
void initUserOutput() {
	syslog(LOG_INFO, "连接数据库...");
	struct timeval timeout = { 1, 500000 }; // 1.5 seconds
	c = redisConnectWithTimeout((char*) "127.0.0.1", 6379, timeout);
	if (c == NULL || c->err) {
		if (c) {
			char buf[128];
			sprintf(buf, "连接 Redis 数据库失败: %s", c->errstr);
			syslog(LOG_ERR, buf);
			redisFree(c);
		} else {
			syslog(LOG_ERR, "不能分配 Redis 上下文");
		}
		exit(1);
	}

	try {
		cc.connect("127.0.0.1");
		//std::cout << "Mogodb connected ok" << std::endl;
	} catch (const mongo::DBException &e) {
		char buf[128];
		sprintf(buf, "连接 Mongodb 数据库失败: %s", e.what());
		syslog(LOG_ERR, buf);
		exit(2);
	}

	try {
		cc2.connect("127.0.0.1");
		//std::cout << "Mogodb connected ok" << std::endl;
	} catch (const mongo::DBException &e) {
		char buf[128];
		sprintf(buf, "连接 Mongodb 数据库失败: %s", e.what());
		syslog(LOG_ERR, buf);
		exit(3);
	}

	//syslog(LOG_INFO, "清理数据...");
	cc.remove("sitemap.details", mongo::BSONObj(), false);
	cc.remove("sitemap.failed", mongo::BSONObj(), false);
	cc.remove("sitemap.page", mongo::BSONObj(), false);
	cc.remove("sitemap.site", mongo::BSONObj(), false);

	mypthread_mutex_init(&lock, NULL);

	syslog(LOG_INFO, "已准备就绪。");
}

static void outputPage(mongo::DBClientConnection &cc, int fds, char *pageUrl,
		int indent, bool site, int siteIndex) {

	std::auto_ptr < mongo::DBClientCursor > cursor = cc.query("sitemap.page",
			QUERY("url" << pageUrl));
	if (!cursor->more()) {

		ecrire(fds, "页面 ");
		ecrire(fds, "<a target=\"_blank\" href=\"");
		ecrire(fds, (char *) pageUrl);
		ecrire(fds, "\">");
		ecrire(fds, (char *) pageUrl);
		ecrire(fds, "</a>&nbsp;&nbsp;尚未爬出！");
		return;
	}
	mongo::BSONObj obj = cursor->next();

	ecrire(fds, "<p>");
	for (int i = 0; i < indent * 2; i++)
		ecrire(fds, "&nbsp;");
	ecrire(fds, (char *) (site ? "站点" : "页面"));
	if (site) {
		ecrireInt(fds, siteIndex);
	}
	ecrire(fds, ": ");

	const char *title = obj.getStringField("title");
	int tag = (site ? obj.getIntField("tag") : -1);

	ecrire(fds, "<a target=\"_blank\" href=\"");
	ecrire(fds, pageUrl);
	ecrire(fds, "\"");
	if (tag > 0) {

		ecrire(fds, " title=\"tag: ");
		ecrireInt(fds, tag);
		ecrire(fds, "\"");
	}
	ecrire(fds, ">");
	ecrire(fds, (char *) (*title == '\0' ? "<无标题>" : title));
	ecrire(fds, "</a>");
	if (site) {

		ecrire(fds, "&nbsp;&nbsp;");
		ecrire(fds, "<a target=\"_blank\" href=\"/output.html?");
		ecrire(fds, (char *) pageUrl);
		ecrire(fds, "\">");
		ecrire(fds, "详细");
		ecrire(fds, "</a>");
	}
	ecrire(fds, "</p>");

	if (site)
		return;

	mongo::BSONElement pageLinksElement = obj.getField("links_page");
	if (!pageLinksElement.eoo() && pageLinksElement.type() == mongo::Array) {

		std::vector < mongo::BSONElement > links = pageLinksElement.Array();
		int linkIndex = 1;
		for (std::vector<mongo::BSONElement>::iterator it = links.begin();
				it != links.end(); ++it) {

			const char *linkUrl = (*it)["url"].str().c_str();
			const char *type = (*it)["type"].str().c_str();

			ecrire(fds, "<br>");

			for (int i = 0; i < indent * 2; i++)
				ecrire(fds, "&nbsp;");

			ecrire(fds, "页面 #");
			ecrireInt(fds, linkIndex++);
			ecrire(fds, ": [");
			ecrire(fds, (char *) type);
			ecrire(fds, "] <a target=\"_blank\" href=\"");
			ecrire(fds, (char *) linkUrl);
			ecrire(fds, "\">");
			ecrire(fds, (char*) linkUrl);
			ecrire(fds, "</a>");

			ecrire(fds, "&nbsp;&nbsp;");
			ecrire(fds, "<a target=\"_blank\" href=\"/output.html?");
			ecrire(fds, (char *) linkUrl);
			ecrire(fds, "\">");
			ecrire(fds, "详细");
			ecrire(fds, "</a>");

			ecrire(fds, "</br>");
		}
	}

	mongo::BSONElement resLinksElement = obj.getField("links_res");
	if (!resLinksElement.eoo() && resLinksElement.type() == mongo::Array) {

		std::vector < mongo::BSONElement > links = resLinksElement.Array();
		int linkIndex = 1;
		for (std::vector<mongo::BSONElement>::iterator it = links.begin();
				it != links.end(); ++it) {

			const char *linkUrl = (*it)["url"].str().c_str();
			const char *type = (*it)["type"].str().c_str();

			ecrire(fds, "<br>");

			for (int i = 0; i < indent * 2; i++)
				ecrire(fds, "&nbsp;");

			ecrire(fds, "资源 #");
			ecrireInt(fds, linkIndex++);
			ecrire(fds, ": [");
			ecrire(fds, (char *) type);
			ecrire(fds, "] <a target=\"_blank\" href=\"");
			ecrire(fds, (char *) linkUrl);
			ecrire(fds, "\">");
			ecrire(fds, (char*) linkUrl);
			ecrire(fds, "</a>");

			ecrire(fds, "</br>");
		}
	}
}

/** stats, called in particular by the webserver
 * the webserver is in another thread, so be careful
 * However, if it only reads things, it is probably not useful
 * to use mutex, because incoherence in the webserver is not as critical
 * as efficiency
 */
void outputStats(int fds, const char *queryParams) {

	mongo::DBClientConnection cc3;
	try {
		cc3.connect("127.0.0.1");
	} catch (const mongo::DBException &e) {
		ecrire(fds, "</pre>");
		ecrire(fds, "无法连接数据库！");
		ecrire(fds, "<pre>");
		return;
	}

	if (queryParams) {

		ecrire(fds, "</pre>");
		outputPage(cc3, fds, (char *) queryParams, 0, false, 0);
		ecrire(fds, "<pre>");

		return;
	}

	ecrire(fds, "</pre>");

	int siteCount = cc3.count("sitemap.site");
	ecrire(fds, "<p>总共 <strong>");
	ecrireInt(fds, siteCount);
	ecrire(fds, "</strong> 个站点。</p>");

	std::auto_ptr < mongo::DBClientCursor > cursor = cc3.query("sitemap.site");
	int i = 0;
	while (cursor->more()) {

		mongo::BSONObj obj = cursor->next();
		char *pageUrl = (char *) obj.getStringField("url");
		outputPage(cc3, fds, pageUrl, 0, true, i + 1);

		i++;
	}

	ecrire(fds, "<pre>");
}

void outputDetails(TargetType type, const char *target, TargetAction action,
		const char *desc) {
	char timeStr[32];
	struct timespec time = {0L, 0L};
	clock_gettime(CLOCK_REALTIME, &time);
	sprintf(timeStr, "%ld.%ld", time.tv_sec, time.tv_nsec);

	mypthread_mutex_lock (&lock);
	cc2.insert("sitemap.details",
			BSON("moment" << timeStr <<
					"type" << targetTypeNames[(int)type] <<
					"target" << target <<
					"action" << targetActionNames[(int)action] <<
					"desc" << (desc==NULL?"":desc)));
	//cc2.ensureIndex("sitemap.details", BSON("target" << 1));
	mypthread_mutex_unlock(&lock);
}
