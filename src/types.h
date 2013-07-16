// Larbin
// Sebastien Ailleret
// 12-01-00 -> 10-12-01

#ifndef TYPES_H
#define TYPES_H

// Size of the HashSize (max number of urls that can be fetched)
#define hashSize 64000000

// Size of the duplicate hashTable
#define dupSize hashSize
#define dupFile "dupfile.bak"

// Size of the arrays of Sites in main memory
#define namedSiteListSize 20000
#define IPSiteListSize 10000

// Max number of urls in ram
#define ramUrls 100000
#define maxIPUrls 80000  // this should allow less dns call

// Max number of urls per site in Url
#define maxUrlsBySite 254  // must fit in uint8_t

// time out when reading a page (in sec)
#define timeoutPage 30   // default time out
#define timeoutIncr 2000 // number of bytes for 1 more sec

// How long do we keep dns answers and robots.txt
#define dnsValidTime 2*24*3600

// Maximum size of a page
#define maxPageSize    1000000
#define nearlyFullPage  900000

// Maximum size of a robots.txt that is read
// the value used is min(maxPageSize, maxRobotsSize)
#define maxRobotsSize 10000

// How many forbidden items do we accept in a robots.txt
#define maxRobotsItem 100

// file name used for storing urls on disk
#define fifoFile "fifo"
#define fifoFileWait "fifowait"

// number of urls per file on disk
// should be equal to ramUrls for good interaction with restart
#define urlByFile ramUrls

// Size of the buffer used to read sockets
#define BUF_SIZE 16384
#define STRING_SIZE 1024

// Max size for a url
#define maxUrlSize 1024
#define maxSiteSize 40    // max size for the name of a site

// max size for cookies
#define maxCookieSize 128

// Standard size of a fifo in a Site
#define StdVectSize maxRobotsItem

// maximum number of input connections
#define maxInput 5

// if we save files, how many files per directory and where
#define filesPerDir 2000
#define saveDir "save/"
#define indexFile "index.html"    // for MIRROR_SAVE
#define nbDir 1000                // for MIRROR_SAVE

// options for SPECIFICSEARCH (except with DEFAULT_SPECIFIC)
#define specDir "specific/"
#define maxSpecSize 5000000

// Various reasons of error when getting a page
#define nbAnswers 16
enum FetchError
{
  success,
  noDNS,
  noConnection,
  forbiddenRobots,
  timeout,
  badType,
  tooBig,
  err30X,
  err40X,
  earlyStop,
  duplicate,
  fastRobots,
  fastNoConn,
  fastNoDns,
  tooDeep,
  urlDup
};

// Tag type
enum TagType
{
  ttUnknow,
  ttBase,
  ttAnchor,
  ttScript,
  ttLink,
  ttFrame,
  ttIFrame,
  ttImage
};

// standard types
typedef	unsigned int uint;


enum TargetType {
	DOMAIN_TARGET/*域名*/,
	ROBOTS_TARGET/*robots.txt*/,
	URL_TARGET/*网址*/,
};

enum TargetAction {
	DOMAIN_SUBMIT/*提交域名解析查询*/,
	DOMAIN_CNAME/*根据 CNAME 查 IP 地址*/,
	DOMAIN_OK/*域名解析成功*/,
	DOMAIN_ERROR/*域名解析失败*/,
	//
	ROBOTS_GETTING/*正下载 robots.txt*/,
	ROBOTS_GETERR/*robots.txt 下载失败*/,
	ROBOTS_GETOK/*已下载 robots.txt*/,
	ROBOTS_PARSEOK/*已成功解析 robots.txt*/,
	ROBOTS_PARSEERR/*robots.txt 解析失败*/,
	//
	URL_IGNORED/*URL 被忽略*/,
	URL_QUEUE/*URL 加入队列*/,
	URL_GETTING/*正下载页面*/,
	URL_GETERR/*页面下载失败*/,
	URL_GETOK/*已下载页面*/,
	URL_PARSEOK/*已成功解析页面*/,
	URL_PARSEERR/*页面解析失败*/,
};

#endif // TYPES_H
