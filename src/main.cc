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

  if (global::daemonize) daemonize();

  printf("Sitemap已经启动。");

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
    input();
    stateMain(3);
    sequencer();
    stateMain(4);
    fetchDns();
    stateMain(5);
    fetchOpen();
    stateMain(6);
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
  if ((global::now % 300) == 0) {
    global::readPriorityWait = global::URLsPriorityWait->getLength();
    global::readWait = global::URLsDiskWait->getLength();
  }
  if ((global::now % 300) == 150) {
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
    printf("\n%sURL : %d  (比率 : %d)\n页面 : %d  (比率 : %d)\n成功 : %d  (比率 : %d)\n",
           ctime(&global::now), urls, urlsRate, pages, pagesRate,
           answers[success], successRate);
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
		std::cerr<<"Failed to fork first children."<<std::endl;
		exit(1);
	} else if (childpid > 0)
		exit(0); /* terminate parent, continue in child */

	/* dissociate from process group */
	if (setsid()<0) {
		std::cerr<<"Failed to become process group leader."<<std::endl;
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

