// Larbin
// Sebastien Ailleret
// 07-12-01 -> 07-12-01

#include <iostream>
#include <stdlib.h>
#include <syslog.h>

#include "utils/mypthread.h"

/* Launch a new thread
 * return 0 in case of success
 */
void startThread (StartFun run, void *arg) {
  pthread_t t;
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0
      || pthread_create(&t, &attr, run, arg) != 0
      || pthread_attr_destroy(&attr) != 0
      || pthread_detach(t) != 0) {
    syslog(LOG_ERR, "无法启动新的线程");
    exit(1);
  }
}
