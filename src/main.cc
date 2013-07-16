//  Sitemap

#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <iostream>

#include "options.h"

#include "global.h"
#include "utils/text.h"
#include "fetch/checker.h"
#include "fetch/sequencer.h"
#include "fetch/fetchOpen.h"
#include "fetch/fetchPipe.h"
#include "interf/input.h"
#include "interf/output.h"
#include "utils/mypthread.h"

#include "utils/debug.h"
#include "utils/webserver.h"
#include "utils/histogram.h"

static void cron ();

///////////////////////////////////////////////////////////
#ifdef PROF
static bool stop = false;
static void handler (int i) {
  stop = true;
}
#endif // PROF

// wait to limit bandwidth usage
#ifdef MAXBANDWIDTH
static void waitBandwidth (time_t *old) {
  while (global::remainBand < 0) {
    poll(NULL, 0, 10);
    global::now = time(NULL);
    if (*old != global::now) {
      *old = global::now;
      cron ();
    }
  }
}
#else
#define waitBandwidth(x) ((void) 0)
#endif // MAXBANDWIDTH

#ifndef NDEBUG
static uint count = 0;
#endif // NDEBUG

void daemonize();

///////////////////////////////////////////////////////////
// If this thread terminates, the whole program exits
int main (int argc, char *argv[]) {

  global glob(argc, argv);

  if (global::daemonize) {
    daemonize();

    char buf[64];
    sprintf(buf, "Sitemap 已经启动...[%ld]", global::now);
    syslog(LOG_INFO, buf);
  }

#ifdef PROF
  signal (2, handler);
#endif // PROF

#ifndef NOWEBSERVER
  // launch the webserver if needeed
  if (global::httpPort != 0)
    startThread(startWebserver, NULL);
#endif // NOWEBSERVER

  // Start the search
  time_t old = global::now;

  for (;;) {

    // update time
    global::now = time(NULL);
    if (old != global::now) {
      // this block is called every second
      old = global::now;
      cron();
    }

    stateMain(-count);
    waitBandwidth(&old);
	
    stateMain(1);
    for (int i=0; i<global::maxFds; i++)
      global::ansPoll[i] = 0;
    for (uint i=0; i<global::posPoll; i++)
      global::ansPoll[global::pollfds[i].fd] = global::pollfds[i].revents;
    global::posPoll = 0;
    
	stateMain(2);
	/*
	    初始化input监听端口，准备接收手动/额外输入的url。
		当接收到请求连接发送过来的url时，保存到相应的URL队列中（比如URLsDisk或URLsPriority）。
	*/
	input();
	
    stateMain(3);
    /*
    	在满足url个数限制之后，通过调用canGetUrl函数按优先级从各个URL队列（比如URLsPriorityWait，URLsPriority，URLsDiskWait，URLsDisk）
	    获取url保存到某个NamedSite（通过url的hash值）中，如：global::namedSiteList[u->hostHashCode()].putPriorityUrlWait(u);
        putPriorityUrlWait，putPriorityUrl等函数实际调用putGenericUrl函数处理url：忽略掉 url（forgetUrl），或者重新放回URL队列，
		或者放入global::dnsSites NamedSite队列中，或者放到某个IPSite中。
	*/
	sequencer();
	
    stateMain(4);
    /*
	    从global::dnsSites 获取NamedSite实例请求DNS获取url的IP地址。
		当从DNS获取到IP地址后，该url被放入到某个IPSite中（通过NamedSite::dnsOK（）方法）。
        dnsOK中创建连接conn，生成GET请求，用来获取robots.txt。
	    当获取/并解析完robots.txt（NamedSite::robotsResult调用transfer(u)将url保存到某个IPSite中，同时将IPSite保存到global::okSites中）。
	*/
	fetchDns();
	
    stateMain(5);
    /*
	    从global::okSites中找到一个IPSite实例，调用其方法函数fetch()创建连接conn，生成GET请求，用来下载url文件。
		下载完后用html实例分析html文件中的url，并保存到URL队列中等待下一轮的处理。
		
		当下载/解析完html文件后，endOfFile->manageHtml->endOfLoad->loaded等函数被调用，用来保存html文件。
		loaded函数被不同模块实现，用来控制保存html的方法（网站镜像文件保存或者其他。。。）
	*/
	fetchOpen();
	
    stateMain(6);
    /*
	    处理所有连接发生的读/写事件，重新注册事件到pollfds数组中。
	*/
	checkAll();
	
    // select
    stateMain(count++);
    poll(global::pollfds, global::posPoll, 10);
    stateMain(7);
  }
}

// a lot of stats and profiling things
static void cron () {
  // shall we stop
#ifdef PROF
  if (stop) exit(2);
#endif // PROF

#ifdef EXIT_AT_END
  if (global::URLsDisk->getLength() == 0
      && global::URLsDiskWait->getLength() == 0
      && debUrl == 0)
    exit(0);
#endif // EXIT_AT_END

  // look for timeouts
  checkTimeout();

  // see if we should read again urls in fifowait
  if ((global::now % 30) == 0) {
    global::readPriorityWait = global::URLsPriorityWait->getLength();
    global::readWait = global::URLsDiskWait->getLength();
  }
  if ((global::now % 30) == 15) {
    global::readPriorityWait = 0;
    global::readWait = 0;
  }

#ifdef MAXBANDWIDTH
  // give bandwidth
  if (global::remainBand > 0) {
    global::remainBand = MAXBANDWIDTH;
  } else {
    global::remainBand = global::remainBand + MAXBANDWIDTH;
  }
#endif // MAXBANDWIDTH

#ifndef NOSTATS
  histoHit(pages, answers[success]);

  if ((global::now & 7) == 0) {
  
    urlsRate = (urls - urlsPrev) >> 3; urlsPrev = urls;
    pagesRate = (pages - pagesPrev) >> 3; pagesPrev = pages;
    successRate = (answers[success] - successPrev) >> 3;
    successPrev = answers[success];
    siteSeenRate = (siteSeen - siteSeenPrev) >> 3; siteSeenPrev = siteSeen;
    siteDNSRate = (siteDNS - siteDNSPrev) >> 3; siteDNSPrev = siteDNS;
#ifndef NDEBUG
    readRate = (byte_read - readPrev) >> 3; readPrev = byte_read;
    writeRate = (byte_write - writePrev) >> 3; writePrev = byte_write;
#endif // NDEBUG

#ifdef STATS
    char buf[1024];
    sprintf(buf, "%s URL : %d; 页面 : %d; 成功 : %d",
           ctime(&global::now), urls, pages, answers[success]);
	syslog(LOG_NOTICE, buf);
#endif // STATS
  }
#endif // NOSTATS
}

void daemonize()
{
	int childpid, fd, fdtablesize;

	/* ignore terminal I/O, stop signals */
	signal(SIGTTOU,SIG_IGN);
	signal(SIGTTIN,SIG_IGN);
	signal(SIGTSTP,SIG_IGN);

	/* fork to put us in the background (whether or not the user specified '&' on the command line */
	if ((childpid = fork()) < 0) {
		syslog(LOG_EMERG, "Failed to fork first children.");
		exit(1);
	} else if (childpid > 0)
		exit(0); /* terminate parent, continue in child */

	/* dissociate from process group */
	if (setsid()<0) {
		syslog(LOG_EMERG, "Failed to become process group leader.");
		exit(1);
	}

	/* close any open file descriptors */
	fdtablesize = getdtablesize();
	for (fd = 0; fd < fdtablesize; fd++)
		close(fd);

	/* set working directory to allow filesystems to be unmounted */
	//chdir("/");

	/* clear the inherited umask */
	umask(0);
}

