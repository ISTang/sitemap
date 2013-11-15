/**
 * Created with JetBrains WebStorm.
 * User: joebin
 * Date: 13-3-18
 * Time: 下午11:01
 */
// 引入依赖包
var fs = require('fs');
var async = require('async');
var _redis = require("redis");
var mongo = require('mongodb');
var BSON = mongo.BSONPure;
var hashes = require('hashes');
var url = require('url');
//
var poolModule = require('generic-pool');
//
var config = require(__dirname + '/config');
var utils = require(__dirname + '/utils');

exports.openDatabase = openDatabase;
exports.getMainSites = getMainSites;
exports.getSiteByName = getSiteByName;
exports.getSiteHosts = getSiteHosts;
exports.getFailedPages = getFailedPages;
exports.countSite = countSite;

const REDIS_SERVER = config.REDIS_SERVER;
const REDIS_PORT = config.REDIS_PORT;

const MONGO_SERVER = config.MONGO_SERVER;
const MONGO_PORT = config.MONGO_PORT;
//
const LOG_ENABLED = config.LOG_ENABLED;

var logStream = fs.createWriteStream("logs/db.log", {"flags": "a"});

var redisPool;
var db;

String.prototype.contains = utils.contains;
Date.prototype.format = utils.DateFormat;
Number.prototype.format = utils.NumberFormat;

var nextUnknownSiteId = 0;

/*
 * Print log
 */
function log(msg) {

    var now = new Date();
    var strDatetime = now.format("yyyy-MM-dd HH:mm:ss");
    var buffer = "[" + strDatetime + "] " + msg + "[db]";
    if (logStream != null) logStream.write(buffer + "\r\n");
    if (LOG_ENABLED) console.log(buffer);
}

/**
 * 获取主站点
 */
function getMainSites(handleResult) {

    var root = {id: "root", name: "根", children: []};

    log("查找主站点...");
    db.collection("site", {safe: false}, function (err, collection) {

        if (err) return handleResult(err);

        collection.find().toArray(function (err, sites) {

            if (err) return handleResult(err);

            log("共找到 " + sites.length + " 主个站点");

            async.forEachSeries(sites, function (site, callback) {

                    var siteId = site._id.toString();
                    var siteTag = site.tag;
                    var siteName = url.parse(site.url).hostname;
                    if (siteName.indexOf("www.") == 0) siteName = siteName.substring(4); // remove www. prefix
                    var homepageTitle;

                    db.collection("page", {safe: false}, function (err, collection) {

                        if (err) return callback(err);

                        log("找到主站点 " + siteName + "，获取其首页信息...");
                        collection.findOne({url: site.url}, function (err, homepage) {

                            if (err) return callback(err);

                            if (!homepage) {

                                log("无主站点 " + siteName + " 的首页信息");
                            } else {

                                homepageTitle = homepage.title;
                                log("主站点 " + siteName + " 的首页标题: " + homepageTitle + "");
                            }

                            var o = {id: siteTag + ":" + siteName, name: siteName + "[" + (homepageTitle ? homepageTitle : "无首页信息") + "]", children: []};
                            root.children.push(o);

                            callback();
                        });
                    });
                },

                function (err) {

                    handleResult(err, root);
                });
        });
    });
}

/**
 * 根据名字获取站点ID
 */
function getSiteByName(siteName, callback) {

    if (siteName.indexOf("www.") == 0) siteName = siteName.substring(4); // remove www. prefix

    log("查找主站点 " + siteName + "...");
    db.collection("site", {safe: false}, function (err, collection) {

        if (err) return callback(err);

        collection.find().toArray(function (err, sites) {

            if (err) return callback(err);

            var site = null;
            for (var siteIndex in sites) {

                var thisSite = sites[siteIndex];
                var thisSiteName = url.parse(thisSite.url).hostname;
                if (thisSiteName.indexOf("www.") == 0) thisSiteName = thisSiteName.substring(4); // remove www. prefix
                if (siteName === thisSiteName) {

                    site = {id: thisSite._id.toString(), url: thisSite.url, tag: thisSite.tag};
                    log("主站点 " + siteName + " 的 ID 为" + site.id);
                    break;
                }
            }

            if (!site) log("主站点 " + siteName + " 未找到");
            callback(null, site);
        });
    });
}

function getSiteHosts(siteTag, siteName, callback, onCacheBuilt) {

    var homepageUrl;
    var hosts;

    async.series([
        function (callback) {

            db.collection("site", {safe: false}, function (err, collection) {

                if (err) return callback(err);

                log("获取标记为 " + siteTag + "   的站点基本信息...");
                collection.findOne({tag: siteTag}, function (err, site) {

                    if (err) return callback(err);

                    if (site == null) return callback("站点标记 " + siteTag + " 不存在！")

                    homepageUrl = site.url;
                    log("站点标记: " + siteTag + ", 首页URL: " + homepageUrl);

                    callback();
                });
            });
        },
        function (callback) {
            redisPool.acquire(function (err, redis) {

                if (err) return callback(err);

                log("判断站点 " + siteTag + " 的主机信息是否已经存在...");
                redis.exists("site:" + siteTag + ":hosts", function (err, exists) {

                    if (err) {
                        redisPool.release(redis);
                        return callback(err);
                    }

                    log("站点 " + siteTag + " 的主机信息" + (exists ? "已经" : "不") + "存在...");

                    var pageCount;
                    var lastPageCount;
                    async.series([
                        function (callback) {

                            log("获取站点 " + siteTag + " 的最新页面数量...");
                            redis.zcard("site:" + siteTag + ":links_page", function (err, count) {

                                if (err) return callback(err);

                                pageCount = count;
                                log("站点 " + siteTag + " 总共有 " + pageCount + " 个页面");
                                callback();
                            });
                        },
                        function (callback) {

                            log("获取站点 " + siteTag + " 的已分析页面数量...");
                            redis.get("site:" + siteTag + ":page_count", function (err, value) {

                                if (err) return callback(err);
                                lastPageCount = value || 0;

                                log("站点 " + siteTag + "  已分析的页面数为" + lastPageCount);
                                callback();
                            });
                        },
                        function (callback) {

                            log("从缓存获取站点 " + siteTag + " 的主机信息...");
                            redis.smembers("site:" + siteTag + ":hosts", function (err, list) {

                                if (err) return callback(err);

                                hosts = [];
                                for (var i in list) {

                                    var host = list[i];
                                    var o = {id: siteTag + ":" + host, name: host}
                                    hosts.push(o);
                                }

                                callback();
                            });
                        },
                        function (callback) {

                            var rebuild = false;
                            if (exists) {

                                if (lastPageCount < pageCount) {

                                    log("需要为站点 " + siteTag + " 重建缓存");
                                    rebuild = true;
                                }
                            } else {
                                log("需要为站点 " + siteTag + " 建立缓存");
                            }

                            if (!exists || rebuild) {
                                redis.get("site:" + siteTag + ":building_cache", function (err, buildingCache) {

                                    if (err) return callback(err);
                                    if (buildingCache == 1) return callback("后台正在" + (exists ? "重建" : "建立") + "缓存，请稍后再试...");

                                    setTimeout(function () {

                                        log("正在为站点 " + siteTag + (exists ? " 重建" : " 建立") + "缓存...");
                                        makeSiteHosts(siteTag, homepageUrl, pageCount, function (err) {

                                            if (err) log(err);
                                            else log("已经为站点 " + siteTag + (exists ? " 重建" : " 建立") + "缓存");
                                            if (onCacheBuilt) onCacheBuilt(err);
                                        });
                                    }, 1000);
                                    if (onCacheBuilt) {
                                        hosts = null;
                                        callback();
                                    }
                                    else callback("后台开始" + (exists ? "重建" : "建立") + "缓存，请稍后再试...");
                                });
                            } else {
                                callback();
                            }
                        }
                    ], function (err) {

                        redisPool.release(redis);
                        callback(err);
                    });
                });
            });
        }],
        function (err) {

            //if (err) log(err);
            callback(err, hosts);
        });

    function makeSiteHosts(siteTag, homepageUrl, pageCount, callback) {

        log("生成站点 " + siteTag + " 的主机信息...");

        var urlSet = new hashes.HashSet();
        var hostSet = new hashes.HashSet();

        urlSet.add(homepageUrl);
        hostSet.add(siteName);

        var nextPageIndex = 0;
        const MAX_PAGES = 2048;
        var nextChildSiteId = 0;

        redisPool.acquire(function (err, redis) {

            if (err) return callback(err);

            redis.set("site:" + siteTag + ":building_cache", 1);

            log("清除站点 " + siteTag + " 的主机信息缓存...");
            redis.del("site:" + siteTag + ":hosts");
            redis.del("site:" + siteTag + ":page_count");

            async.whilst(
                function () {
                    return (nextPageIndex <= pageCount);
                },
                function (callback) {

                    //var leftPageCount = pageCount-(nextPageIndex+MAX_PAGES);
                    //if (leftPageCount<0) leftPageCount = 0;
                    //log("获取站点 " + siteName + " 中的第 "+(nextPageIndex)+" 至 "+(nextPageIndex+MAX_PAGES-1)+" 个页面(还剩 "+leftPageCount+")...");

                    redis.zrange("site:" + siteTag + ":links_page", nextPageIndex, nextPageIndex + MAX_PAGES - 1, function (err, pageUrls) {

                        if (err) return callback(err);

                        nextPageIndex += MAX_PAGES;

                        async.forEachSeries(pageUrls, function (pageUrl, callback) {

                            var hostName = url.parse(pageUrl).hostname;
                            if (hostName.indexOf("www.") == 0) hostName = hostName.substring(4); // remove www. prefix

                            if (hostSet.contains(hostName)) return callback();

                            if (!new RegExp('.*?\\.' + siteName.replace('.', '\\.') + '$').test(hostName)) return callback();
                            log("扫描到新的站点: " + hostName);
                            hostSet.add(hostName);

                            redis.sadd("site:" + siteTag + ":hosts", hostName); // save to cache

                            callback();
                        }, function (err) {

                            callback(err);
                        });
                    });
                },
                function (err) {

                    log("记录站点 " + siteTag + " 的页面数量...");
                    redis.set("site:" + siteTag + ":page_count", pageCount);
                    redis.set("site:" + siteTag + ":building_cache", 0);

                    redisPool.release(redis);

                    callback(err);
                }
            );
        });
    }
}

/**
 * 获取有问题的页面信息
 */
function getFailedPages(siteName, includeChildSites, includedUrlString, callback) {
    var result = [];
    async.series([
        function (callback) {
            db.collection("failed", {safe: false}, function (err, collection) {
                if (err) return callback(err);
                log("Finding failed pages from site " + siteName + "...");
                collection.find({url: new RegExp(siteName)}).limit(100).toArray(function (err, pages) {
                    if (err) return callback(err);
                    if (!pages) return callback();
                    log("Found " + pages.length + " failed page(s).");
                    for (var i in pages) {
                        var page = pages[i];
                        result.push({id: page._id.toString(), url: page.url, problem: page.reason});
                    }
                    callback();
                });
            });
        }],
        function (err) {
            callback(err, result);
        });
}

function countSite(siteUrl, handleResult) {

    log("正在统计站点 " + siteUrl + " 页面/资源个数...");
    var siteTag, pageCount, resCount;
    async.series([
        function (callback) {
            db.collection("site", {safe: false}, function (err, collection) {
                if (err) return callback(err);
                collection.findOne({url: siteUrl}, function (err, site) {
                    if (err) return callback(err);
                    if (site == null) return callback("该站点不存在！")
                    siteTag = site.tag;
                    callback();
                });
            });
        },
        function (callback) {
            redisPool.acquire(function (err, redis) {
                if (err) return callback(err);
                async.series([
                    function (callback) {
                        redis.zcard("site:" + siteTag + ":links_page", function (err, count) {
                            if (err) return callback(err);
                            pageCount = count;
                            callback();
                        });
                    },
                    function (callback) {
                        redis.zcard("site:" + siteTag + ":links_res", function (err, count) {
                            if (err) return callback(err);
                            resCount = count;
                            callback();
                        });
                    }],
                    function (err) {
                        redisPool.release(redis);
                        callback(err);
                    });
            });
        }],
        function (err) {
            handleResult(err, pageCount, resCount);
        });
}

/**
 * 扫描子页面
 */
function scanChildPages(siteName, pageUrl, urlSet, hostSet, childSites, callback) {

    if (urlSet.contains(pageUrl)) {

        log("子页面 " + pageUrl + " 已经处理过");
        return callback();
    }
    urlSet.add(pageUrl);

    var hostName = url.parse(pageUrl).hostname;
    if (hostSet.contains(hostName)) {

        log("子页面 " + pageUrl + " 所属的主机已经存在");
    } else if (!new RegExp('.*?\\.' + siteName.replace('.', '\\.') + '$').test(hostName)) {

        log("主机 " + hostName + " 不属于站点 " + siteName);

        return callback();
    } else {

        log("扫描到新的主机: " + hostName);
        hostSet.add(hostName);
    }

    db.collection("page", {safe: false}, function (err, collection) {

            if (err) return callback(err);

            log("获取子页面 " + pageUrl + " 的信息...");
            collection.findOne({url: pageUrl}, function (err, page) {

                if (err) return callback(err);

                if (!page) {

                    log("无子页面 " + pageUrl + " 的信息");

                    var o = {id: "unknown_page_" + nextUnknownSiteId++, name: hostName + "[无页面信息]", children: []};
                    childSites.push(o);

                    return callback();
                }

                var pageId = page._id;
                var pageTitle = page.title;

                log("页面 " + pageUrl + " 的信息找到：标题为\"" + pageTitle + "\"，共 " + page.links_res.length + " 个资源，" + page.links_page.length + " 个子页面");

                async.forEachSeries(page.links_page, function (childPage, callback) {

                    log("分析子页面 " + childPage.url + "...");

                    scanChildPages(siteName, childPage.url, urlSet, hostSet, childSites, function (err) {

                        callback(err);
                    });
                }, function (err) {

                    if (err) return callback(err);

                    var o = {id: pageId, name: hostName, children: []};
                    childSites.push(o);

                    callback();
                });
            });
        },
        function (err) {
            callback(err, hashTable.getKeyValuePairs());
        });
}

function openDatabase(callback) {
    db = new mongo.Db("sitemap", new mongo.Server(MONGO_SERVER, MONGO_PORT), {safe: true});

    log("正在连接数据库...");
    db.open(function (err, p_client) {

        if (err) {

            log("无法打开数据库: " + err);
            return callback(err);
        } else {

            log("已连接到数据库。");
            callback();
        }
    });
}
function main(fn) {
    fn();
}

void main(function () {

    redisPool = poolModule.Pool({
        name: 'redis',
        create: function (callback) {
            var redis = _redis.createClient(REDIS_PORT, REDIS_SERVER);
            redis.on("ready", function (err) {
                callback(null, redis);
            });
            redis.on("error", function (err) {
                log(err);
                callback(err, null);
            });
        },
        destroy: function (redis) {
            redis.end();
        },
        max: 2,
        // optional. if you set this, make sure to drain() (see step 3)
        min: 0,
        // specifies how long a resource can stay idle in pool before being removed
        idleTimeoutMillis: 1000 * 30,
        // if true, logs via console.log - can also be a function
        log: false
    });
});
