// Sitemap

#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>
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
  std::cout << "Fetched : ";
  page->getUrl()->print();
  std::cout << page->getHeaders() << "\n" << page->getPage() << "\n";
#endif // BIGSTATS

  char *pageUrl = page->getUrl()->giveUrl();

  // skip duplicate page
#ifndef NDEBUG
  std::cout<<"Checking page url: "<<pageUrl<<std::endl;
#endif
  bool pageExists = (cc.count("sitemap.page", BSON("url"<<pageUrl)) > 0);

  if (pageExists) {
#ifndef NDEBUG
    std::cerr<<"Page url "<<pageUrl<<" exists"<<std::endl;
#endif
    return;
  }

  // detect page encoding
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
          strncpy(pageEncoding, pCharsetBegin, std::min(MAX_ENCODING, pCharsetEnd-pCharsetBegin));
        }
      }
      delete[] contentType;
#ifndef NDEBUG
      std::cout<<"Page encoding of url "<<pageUrl<<": "<<pageEncoding<<std::endl;
#endif // NDEBUG
    }
  } else {
    char *pCharsetBegin = strcasestr(page->getPage(), "<meta charset=\"");
    if (pCharsetBegin) {
      pCharsetBegin += 15;
      char *pCharsetEnd = strchr(pCharsetBegin, '\"');
      if (pCharsetEnd) {
        strncpy(pageEncoding, pCharsetBegin, std::min(MAX_ENCODING, pCharsetEnd-pCharsetBegin));
      }
    }
  }

  // page title
#ifndef NDEBUG
  std::cout<<"Getting page title for url: "<<pageUrl<<std::endl;
#endif
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
      if (-1 == (int)hIconv ) {
        std::cerr<<"Cannot convert page title from "<<pageEncoding<<" to UTF-8 for url "<<pageUrl<<std::endl;
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
  }

#ifndef NDEBUG
  std::cout<<"Saving page info for url "<<pageUrl<<"..."<<std::endl;
#endif
  mongo::BSONObjBuilder b;
  b.append("url", pageUrl);
  b.append("title", (pageTitle==NULL?"":pageTitle));

  mongo::BSONArrayBuilder b2;
  Vector<LinkInfo> *links = page->getLinks();
  for (unsigned i=0; i<links->getLength(); i++) {

    LinkInfo *linkInfo = (*links)[i];
    char *linkUrl = linkInfo->url;
    TagType linkType = linkInfo->type;

    if (strcmp(pageUrl, linkUrl)!=0) {
      b2.append(BSON("url"<<linkUrl<<"type"<<linkTypes[linkType]));
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
  std::cout << "fetched failed (" << (int)reason << ") : ";
  u->print();
#endif // BIGSTATS

  char *url = u->giveUrl();

  cc.insert("sitemap.failed", BSON("url"<<url<<"reason"<<(int)reason));

  delete url;
}

/** initialisation function
 */
void initUserOutput () {

  try {
    cc.connect("127.0.0.1");
    //std::cout << "Mogodb connected ok" << std::endl;
  } catch( const mongo::DBException &e ) {
    std::cout << "连接 mongo 数据库失败: " << e.what() << std::endl;
    exit(2);
    return;
  }
}

static void outputPage(mongo::DBClientConnection &cc, int fds, int pageIndex, char *pageUrl, int indent, std::vector<const char*>& pageUrlStack, bool site, bool handleChildPages) {

#ifndef NDEBUG
  std::cout<<"Page #"<<pageIndex<<": "<<pageUrl<<std::endl;
#endif
  ecrire(fds, "<p>");
  for (int i=0; i<indent*2; i++) ecrire(fds, "&nbsp;");
  ecrire(fds, (char *)(site?"站点":"页面"));
  if (pageIndex) ecrireInt(fds, pageIndex);
  ecrire(fds, ": ");

  std::auto_ptr<mongo::DBClientCursor> cursor = cc.query("sitemap.page", QUERY("url"<<pageUrl));
  if (!cursor->more()) return;

  mongo::BSONObj obj = cursor->next();

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
    ecrire(fds, pageUrl);
    ecrire(fds, "\">");
    ecrire(fds, "详细");
    ecrire(fds, "</a>");
  }
  ecrire(fds, "</p>");

  if (!handleChildPages || indent+1>OUTDEPTH)  return;

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
      ecrire(fds, "] <a href=\"");
      ecrire(fds, (char *)linkUrl);
      ecrire(fds, "\">");
      ecrire(fds, (char*)linkUrl);
      ecrire(fds, "</a>");
      ecrire(fds, "</br>");

      if (!strcmp(type, "anchor")||!strcmp(type, "iframe")||!strcmp(type, "frame")) {

        pageUrls.push_back(linkUrl);
      }

      int pageIndex = 1;
      for (unsigned i=0; i<pageUrls.size(); i++) {

        bool rescure = false;
        std::vector<const char*>::const_iterator it=pageUrlStack.begin();
        for (; it!=pageUrlStack.end(); it++) {

          if (!strcmp(*it, pageUrls[i].c_str())) {
            rescure = true;
            break;
          }
        }
        if (rescure) {

          pageUrlStack.push_back(pageUrl);
          outputPage(cc, fds, pageIndex++, (char*)pageUrls[i].c_str(), indent, pageUrlStack, false, true);
          pageUrlStack.pop_back();
        }
      }
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
#ifndef NDEBUG
    std::cout << "连接 mongo 数据库失败: " << e.what() << std::endl;
#endif
    ecrire(fds, "</pre>");
    ecrire(fds, "无法连接数据库！");
    ecrire(fds, "<pre>");
    return;
  }

  std::vector<const char *> pageUrlStack;

  if (queryParams) {
#ifndef NDEBUG
    std::cout<<"outputStats: "<<queryParams<<std::endl;
#endif
    ecrire(fds, "</pre>");

    outputPage(cc, fds, 0, (char *)queryParams, 0, pageUrlStack, true, true);

    ecrire(fds, "<pre>");

    return;
  }

  ecrire(fds, "</pre>");

#ifndef NDEBUG
  std::cout<<"Getting site count..."<<std::endl;
#endif
  int siteCount = cc.count("sitemap.site");
#ifndef NDEBUG
  std::cout<<"Total "<<siteCount<<" sites..."<<std::endl;
#endif
  ecrire(fds, "<p>总共 <strong>");
  ecrireInt(fds, siteCount);
  ecrire(fds, "</strong> 个站点。</p>");

  std::auto_ptr<mongo::DBClientCursor> cursor = cc.query("sitemap.site");
  int i=0;
  while (cursor->more()) {
    mongo::BSONObj obj = cursor->next();
    char *pageUrl = (char *)obj.getStringField("url");
    outputPage(cc, fds, i+1, pageUrl, 0, pageUrlStack, true, false);

    i++;
  }

  ecrire(fds, "<pre>");
}
