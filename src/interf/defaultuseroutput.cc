// Sitemap

#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <iconv.h>
#include <cstdlib>
#include <mongo/client/dbclient.h>

#include "options.h"

#include "types.h"
#include "global.h"
#include "fetch/file.h"
#include "utils/text.h"
#include "utils/debug.h"
#include "interf/output.h"

static const char* linkTypes[] = {
  "??",
  "base",
  "anchor",
  "script",
  "link",
  "frame",
  "iframe",
  "image"
};

#define MAX_ENCODING 20
#define MAX_TITLE 2048

mongo::DBClientConnection cc;

/** A page has been loaded successfully
 * @param page the page that has been fetched
 */
void loaded (html *page) {
  // Here should be the code for managing everything
  // page->getHeaders() gives a char* containing the http headers
  // page->getPage() gives a char* containing the page itself
  // those char* are statically allocated, so you should copy
  // them if you want to keep them
  // in order to accept \000 in the page, you can use page->getLength()
#ifdef BIGSTATS
  std::cout << "已获取到 : ";
  page->getUrl()->print();
  std::cout << page->getHeaders() << "\n" << page->getPage() << "\n";
#endif // BIGSTATS

  char *pageUrl = page->getUrl()->giveUrl();

  // skip duplicate page
//#ifndef NDEBUG
//  std::cout<<"检查页面 "<<pageUrl<<"..."<<std::endl;
//#endif
  bool pageExists = (cc.count("sitemap.page", BSON("url"<<pageUrl)) > 0);

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
  char pageEncoding[MAX_ENCODING+1];
  memset(pageEncoding, 0, sizeof pageEncoding);
  char *pContentTypeBegin = strcasestr(page->getPage(), "<meta http-equiv=\"Content-Type\" content=\"");
  if (pContentTypeBegin) {
    pContentTypeBegin += 41;
    char *pContentTypeEnd = strcasestr(pContentTypeBegin, "\"");
    if (pContentTypeEnd) {
      char *contentType = new char[pContentTypeEnd-pContentTypeBegin+1];
      memset(contentType, 0, pContentTypeEnd-pContentTypeBegin+1);
      strncpy(contentType, pContentTypeBegin, pContentTypeEnd-pContentTypeBegin);
      //
      char *pCharsetBegin = strcasestr(contentType, "charset=");
      if (pCharsetBegin) {
        pCharsetBegin += 8;
        char *pCharsetEnd = strchr(pCharsetBegin, ';');
        if (!pCharsetEnd) pCharsetEnd = pCharsetBegin+strlen(contentType);
        if (strncmp(pCharsetBegin, "text/html", 9)) {
          strncpy(pageEncoding, pCharsetBegin, std::min(MAX_ENCODING, int(pCharsetEnd-pCharsetBegin)));
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
        strncpy(pageEncoding, pCharsetBegin, std::min(MAX_ENCODING, int(pCharsetEnd-pCharsetBegin)));
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
    char *q = strcasestr(p+7, "</title>");
    if (q) {
      pageTitle = new char[q-p-7+1];
      strncpy(pageTitle, p+7, q-p-7);
      *(pageTitle+(q-p-7)) = '\0';
    }
  }
  if (pageTitle) {
    if (*pageEncoding!='\0' && strcasecmp(pageEncoding, "utf-8")) {
      iconv_t hIconv = iconv_open("UTF-8", pageEncoding);
      if (-1 == (long)hIconv ) {
#ifndef NDEBUG
        std::cerr<<"无法将页面 "<<pageUrl<<" 的标题从编码 "<<pageEncoding<<" 转换成编码 UTF-8"<<std::endl;
#endif
        //exit(3);
      } else {
        char inBuf[MAX_TITLE+1];
        char outBuf[MAX_TITLE*2+1];
        memset(inBuf, 0, sizeof inBuf);
        memset(outBuf, 0, sizeof outBuf);
        strncpy(inBuf, pageTitle, MAX_TITLE);
        size_t inLen = strlen(inBuf);
        size_t outLen = sizeof outBuf - 1;
        char *s = inBuf;
        char *d = outBuf;
        iconv(hIconv, (char**)(&s), &inLen, (char**)(&d), &outLen);

        delete[] pageTitle;
        pageTitle = new char[outLen+1];
        strcpy(pageTitle, outBuf);

        iconv_close( hIconv );
      }
    }
  } else {
#ifndef NDEBUG
    std::cout<<"页面 "<<pageUrl<<" 无标题"<<std::endl;
#endif
  }

#ifndef NDEBUG
  std::cout<<"保存页面 "<<pageUrl<<"..."<<std::endl;
#endif
  mongo::BSONObjBuilder b;
  b.append("url", pageUrl);
  b.append("title", (pageTitle==NULL?"":pageTitle));

  mongo::BSONArrayBuilder b2;
  Vector<LinkInfo> *links = page->getLinks();
  std::set<std::string> appendedlinkUrls;
  for (unsigned i=0; i<links->getLength(); i++) {

    LinkInfo *linkInfo = (*links)[i];
    char *linkUrl = linkInfo->url;
    TagType linkType = linkInfo->type;

    if (strcmp(pageUrl, linkUrl)!=0 && appendedlinkUrls.count(linkUrl)==0) {
      b2.append(BSON("url"<<linkUrl<<"type"<<linkTypes[linkType]));
      appendedlinkUrls.insert(linkUrl);
    }

    delete[] linkUrl;
  }

  b.append("links", b2.arr());
  cc.insert("sitemap.page", b.obj());
  cc.ensureIndex("sitemap.page", BSON("url"<<1));

  if (page->getUrl()->tag>0) {

    cc.insert("sitemap.site", BSON("url"<<pageUrl<<"tag"<<page->getUrl()->tag));
  }

  delete[] pageUrl;
}

/** The fetch failed
 * @param u the URL of the doc
 * @param reason reason of the fail
 */
void failure (url *u, FetchError reason) {
  // Here should be the code for managing everything
#ifdef BIGSTATS
  std::cout << "获取失败 (" << (int)reason << ") : ";
  u->print();
#endif // BIGSTATS

  char *url = u->giveUrl();

  cc.insert("sitemap.failed", BSON("url"<<url<<"reason"<<(int)reason));

  delete url;
}

/** initialisation function
 */
void initUserOutput () {
#ifndef NDEBUG
  std::cout << "连接数据库 " << "..." << std::endl;
#endif
  try {
    cc.connect("127.0.0.1");
    //std::cout << "Mogodb connected ok" << std::endl;
  } catch( const mongo::DBException &e ) {
    std::cout << "连接数据库失败: " << e.what() << std::endl;
    exit(2);
    return;
  }

#ifndef NDEBUG
  std::cout << "清理数据..." << std::endl;
#endif
  cc.remove("sitemap.failed", mongo::BSONObj(), false);
  cc.remove("sitemap.page", mongo::BSONObj(), false);
  cc.remove("sitemap.site", mongo::BSONObj(), false);

#ifndef NDEBUG
  std::cout << "已准备就绪。" << std::endl;
#endif
}

static void outputPage(mongo::DBClientConnection &cc, int fds, char *pageUrl, int indent, bool site, int siteIndex) {

  std::auto_ptr<mongo::DBClientCursor> cursor = cc.query("sitemap.page", QUERY("url"<<pageUrl));
  if (!cursor->more()) {

    ecrire(fds, "页面 ");
    ecrire(fds, "<a target=\"_blank\" href=\"");
    ecrire(fds, (char *)pageUrl);
    ecrire(fds, "\">");
    ecrire(fds, (char *)pageUrl);
    ecrire(fds, "</a>&nbsp;&nbsp;尚未爬出！");
    return;
  }
  mongo::BSONObj obj = cursor->next();

  ecrire(fds, "<p>");
  for (int i=0; i<indent*2; i++) ecrire(fds, "&nbsp;");
  ecrire(fds, (char *)(site?"站点":"页面"));
  if (site) {
    ecrireInt(fds, siteIndex);
  }
  ecrire(fds, ": ");

  const char *title = obj.getStringField("title");
  int tag = (site?obj.getIntField("tag"):-1);

  ecrire(fds, "<a target=\"_blank\" href=\"");
  ecrire(fds, pageUrl);
  ecrire(fds, "\"");
  if (tag>0) {

    ecrire(fds, " title=\"tag: ");
    ecrireInt(fds, tag);
    ecrire(fds, "\"");
  }
  ecrire(fds, ">");
  ecrire(fds, (char *)(*title=='\0'?"<无标题>":title));
  ecrire(fds, "</a>");
  if (site) {

    ecrire(fds, "&nbsp;&nbsp;");
    ecrire(fds, "<a target=\"_blank\" href=\"/output.html?");
    ecrire(fds, (char *)pageUrl);
    ecrire(fds, "\">");
    ecrire(fds, "详细");
    ecrire(fds, "</a>");
  }
  ecrire(fds, "</p>");

  if (site) return;

  mongo::BSONElement linksElement = obj.getField("links");
  if (!linksElement.eoo() && linksElement.type()==mongo::Array) {

    std::vector<std::string> pageUrls;
    std::vector<mongo::BSONElement> links = linksElement.Array();
    int linkIndex = 1;
    for (std::vector<mongo::BSONElement>::iterator it=links.begin(); it!=links.end(); ++it) {

      const char *linkUrl = (*it)["url"].str().c_str();
      const char *type = (*it)["type"].str().c_str();

      ecrire(fds, "<br>");

      for (int i=0; i<indent*2; i++) ecrire(fds, "&nbsp;");

      ecrire(fds, "#");
      ecrireInt(fds, linkIndex++);
      ecrire(fds, ": [");
      ecrire(fds, (char *)type);
      ecrire(fds, "] <a target=\"_blank\" href=\"");
      ecrire(fds, (char *)linkUrl);
      ecrire(fds, "\">");
      ecrire(fds, (char*)linkUrl);
      ecrire(fds, "</a>");

      if (!strcmp(type, "anchor")||!strcmp(type, "iframe")||!strcmp(type, "frame")) {

        ecrire(fds, "&nbsp;&nbsp;");
        ecrire(fds, "<a target=\"_blank\" href=\"/output.html?");
        ecrire(fds, (char *)linkUrl);
        ecrire(fds, "\">");
        ecrire(fds, "详细");
        ecrire(fds, "</a>");
      }

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

  mongo::DBClientConnection cc;
  try {
    cc.connect("127.0.0.1");
  } catch( const mongo::DBException &e ) {
    ecrire(fds, "</pre>");
    ecrire(fds, "无法连接数据库！");
    ecrire(fds, "<pre>");
    return;
  }

  if (queryParams) {

    ecrire(fds, "</pre>");
    outputPage(cc, fds, (char *)queryParams, 0, false, 0);
    ecrire(fds, "<pre>");

    return;
  }

  ecrire(fds, "</pre>");

  int siteCount = cc.count("sitemap.site");
  ecrire(fds, "<p>总共 <strong>");
  ecrireInt(fds, siteCount);
  ecrire(fds, "</strong> 个站点。</p>");

  std::auto_ptr<mongo::DBClientCursor> cursor = cc.query("sitemap.site");
  int i = 0;
  while (cursor->more()) {

    mongo::BSONObj obj = cursor->next();
    char *pageUrl = (char *)obj.getStringField("url");
    outputPage(cc, fds, pageUrl, 0, true, i+1);

    i++;
  }

  ecrire(fds, "<pre>");
}
