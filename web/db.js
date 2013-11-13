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
exports.getAllSites = getAllSites;
exports.getPages = getPages;
//exports.getResources = getResources;
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
 * 获取所有站点信息
 */
function getAllSites(handleResult) {

    log("查找所有站点...");

    var root = {id: "root", name: "根", children: []};
    db.collection("site", {safe: false}, function (err, collection) {

        if (err) return handleResult(err);

        collection.find().toArray(function (err, sites) {

            if (err) return handleResult(err);

            log("共找到 " + sites.length + " 个站点");

            async.forEachSeries(sites, function (site, callback) {

                    var siteName = url.parse(site.url).hostname;
                    if (siteName.indexOf("www.") == 0) siteName = siteName.substring(4); // remove www. prefix

                    db.collection("page", {safe: false}, function (err, collection) {

                        if (err) return callback(err);

                        log("找到站点 " + siteName + "，获取其首页信息...");
                        collection.findOne({url: site.url}, function (err, homepage) {

                            if (err) return callback(err);

                            if (!homepage) {

                                log("无站点 " + siteName + " 的首页信息");

                                var o = {id: site._id, name: siteName + "[无首页信息]", children: []};
                                root.children.push(o);

                                return callback();
                            }

                            var homepageId = homepage._id;
                            var homepageTitle = homepage.title;

                            log("站点 " + siteName + " 的首页信息找到：标题为\"" + homepageTitle + "\"。");

                            var childSites = [];
                            var urlSet = new hashes.HashSet();
                            var hostSet = new hashes.HashSet();
                            urlSet.add(site.url);
                            hostSet.add(siteName);

                            /*async.forEachSeries(homepage.links_page, function (childPage, callback) {

                                log("分析子页面 " + childPage.url + "...");

                                scanChildPages(siteName, childPage.url, urlSet, hostSet, childSites, function (err) {

                                    callback(err);
                                });
                            }, function (err) {

                                if (err) return callback(err);

                                var o = {id: homepageId, name: siteName + "[" + homepageTitle + "]", children: childSites};
                                root.children.push(o);

                                callback();
                            });*/

                            getChildSites(siteName, site.url, urlSet, hostSet, childSites, function (err) {

                                if (err) return callback(err);

                                var o = {id: homepageId, name: siteName + "[" + homepageTitle + "]", children: childSites};
                                root.children.push(o);

                                callback();
                            });
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
 * 获取页面信息
 * @param parentId
 * @param handleResult
 */
function getPages(parentId, handleResult) {
    var pageUrl = null;
    var pages = [];
    async.series([
        function (callback) {
            db.collection("page", {safe: false}, function (err, collection) {
                if (err) return callback(err);
                log("Finding page with id " + parentId + "...");
                collection.findOne({_id: new BSON.ObjectID(parentId)}, function (err, page) {
                    if (err) return callback(err);
                    if (page != null) {
                        log("Found " + page.url + "(" + page.title + ").");
                        makeSiteTable(page.url, page.links_page, function (err, siteTable) {
                            if (err) return callback(err);
                            for (var i in siteTable) {
                                pages.push({id: siteTable[i].key, name: siteTable[i].value, children: []});
                            }
                            callback();
                        });
                    } else {
                        callback();
                    }
                });
            });
        }],
        function (err) {
            handleResult(err, pages);
        });

    function makeSiteTable(parentUrl, pageInfos, callback) {
        var parentName = url.parse(parentUrl).hostname;
        var hashTable = new hashes.HashTable();
        var hashSet = new hashes.HashSet();
        async.forEachSeries(pageInfos, function (pageInfo, callback) {
                var siteName = url.parse(pageInfo.url).hostname;
                if (parentName == siteName || hashSet.contains(siteName)) return callback();
                db.collection("page", {safe: false}, function (err, collection) {
                    if (err) return callback(err);
                    collection.findOne({url: pageInfo.url}, function (err, page) {
                        if (err) return callback(err);
                        if (page) {
                            db.collection("site", {safe: false}, function (err, collection2) {
                                if (err) return callback(err);
                                collection2.findOne({url: new RegExp(siteName)}, function (err, site) {
                                    if (err) return callback(err);
                                    if (site) return callback();
                                    hashTable.add(page._id, siteName + "[" + page.title + "]");
                                    hashSet.add(siteName);
                                    callback();
                                });
                            });
                        } else {
                            callback();
                        }
                    });
                });
            },
            function (err) {
                callback(err, hashTable.getKeyValuePairs());
            });
    }
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

function getChildSites(siteName, homepageUrl, urlSet, hostSet, childSites, callback) {

    log("获取站点 " + siteName + " 中的子站点...");

    var siteTag;

    async.series([
        function (callback) {

            db.collection("site", {safe: false}, function (err, collection) {

                if (err) return callback(err);

                log("获取站点标记...");

                collection.findOne({url: homepageUrl}, function (err, site) {

                    if (err) return callback(err);

                    if (site == null) return callback("站点 " + siteName + " 不存在！")

                    siteTag = site.tag;
                    log("站点标记为 "+siteTag);

                    callback();
                });
            });
        },
        function (callback) {
            redisPool.acquire(function (err, redis) {

                if (err) return callback(err);

                var pageCount;
                async.series([
                    function (callback) {

                        log("获取站点 " + siteName + " 的页面数量...");
                        redis.zcard("site:" + siteTag + ":links_page", function (err, count) {

                            if (err) return callback(err);

                            pageCount = count;
                            log("站点 " + siteName + " 总共有 "+pageCount+" 个页面");
                            callback();
                        });
                    },
                    function (callback) {

                        var nextPageScore = 1;
                        const MAX_PAGES = 1024;
                        var nextChildSiteId = 0;
                        async.whilst(
                            function () {
                                return (nextPageScore<=pageCount);
                            },
                            function (callback) {

                                log("获取站点 " + siteName + " 中的第 "+(nextPageScore)+" 至 "+(nextPageScore+MAX_PAGES-1)+" 个页面...");

                                redis.zrangebyscore("site:" + siteTag + ":links_page", nextPageScore, nextPageScore+MAX_PAGES-1, function (err, pageUrls) {

                                    if (err) return callback(err);

                                    nextPageScore += MAX_PAGES;

                                    async.forEachSeries(pageUrls, function (pageUrl, callback) {

                                        var hostName = url.parse(pageUrl).hostname;
                                        if (hostName.indexOf("www.") == 0) hostName = hostName.substring(4); // remove www. prefix

                                        if (hostSet.contains(hostName)) return callback();

                                        if (!new RegExp('.*' + siteName + '$').test(hostName)) return callback();
                                        log("扫描到新的站点: " + hostName);
                                        hostSet.add(hostName);

                                        var o = {id: siteName + "_child_" + nextChildSiteId++, name: hostName, children: []};
                                        childSites.push(o);

                                        callback();
                                    }, function (err) {

                                        callback(err);
                                    });
                                });
                            },
                            function (err) {

                                callback(err);
                            }
                        );
                    }
                ], function (err) {

                    redisPool.release(redis);
                    callback(err);
                });
            });
        }],
        function (err) {

            callback(err);
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
    } else if (!new RegExp('.*' + siteName + '$').test(hostName)) {

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
