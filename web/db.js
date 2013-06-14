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
var hashes = require('hashes');
var url = require('url');
//
var config = require(__dirname + '/config');
var utils = require(__dirname + '/utils');

exports.getAllSites = getAllSites;
exports.getPages = getPages;
exports.getResources = getResources;

const REDIS_SERVER = config.REDIS_SERVER;
const REDIS_PORT = config.REDIS_PORT;
//
const LOG_ENABLED = config.LOG_ENABLED;

var logStream = LOG_ENABLED ? fs.createWriteStream("logs/db.log", {"flags": "a"}) : null;

var redis;

String.prototype.contains = utils.contains;

/*
 * Print log
 */
function log(msg) {

    var now = new Date();
    var strDatetime = now.Format("yyyy-MM-dd HH:mm:ss");
    var buffer = "[" + strDatetime + "] " + msg + "[db]";
    if (logStream != null) logStream.write(buffer + "\r\n");
    console.log(buffer);
}

/**
 *获取页面中的资源
 * @param pageUrl
 * @param nextResourceId
 * @param includeChildPages
 * @param includedUrlString
 * @param includedPages
 * @param handleResult
 */
function getResourcesInPage(pageUrl, nextResourceId, includeChildPages, includedUrlString, includedPages, handleResult) {
    var linkCount = 0;
    var result = [];
    async.series([
        function (callback) {
            //log("Counting links of page "+pageUrl+"...");
            redis.zcard("links_of_"+pageUrl, function (err, count) {
                if (err) return callback(err);
                linkCount = count;
                //log("There are "+linkCount+" link(s) on page "+pageUrl);
                callback();
            });
        },
        function (callback) {
            //log("Getting all link(s) url(s) of page "+pageUrl+"...");
            redis.zrange("links_of_"+pageUrl, 1, linkCount, function (err, linkUrls) {
                if (err) return callback(err);
                async.forEachSeries(linkUrls, function (linkUrl, callback) {
                    //log("Finding type for link url "+linkUrl+"...");
                    redis.get("linktype:"+linkUrl, function (err, type) {
                        if (err) return callback(err);
                        //log("Type of link "+linkUrl+" is "+type);
                        if (!includedUrlString || includedUrlString=="" || linkUrl.contains(includedUrlString, true)) {
                            result.push({id:nextResourceId,row:nextResourceId+1,url:linkUrl, type:type});
                            nextResourceId++;
                        }
                        //
                        if (!includeChildPages) return callback();
                        //
                        var isPageUrl = false;
                        async.series([
                            function (callback) {
                                //log("Finding page title for link url "+linkUrl+"...");
                                redis.hget("page:"+linkUrl, "title", function (err, title) {
                                    if (err) return callback(err);
                                    if (!title) {
                                        //log("Link "+linkUrl+" is not a page");
                                        return callback();
                                    }
                                    isPageUrl = true;
                                    //log("Link "+linkUrl+" is a page");
                                    callback();
                                });
                            },
                            function (callback) {
                                if (!isPageUrl || includedPages.contains(linkUrl)) return callback();
                                //获取子页面中的资源
                                includedPages.add(linkUrl);
                                getResourcesInPage(linkUrl, nextResourceId, true, includedUrlString, includedPages, function (err, resources) {
                                    if (err) return callback(err);
                                    result = result.concat(resources);
                                    nextResourceId += resources.length;
                                    callback();
                                });
                            }
                        ], function (err) {
                            if (err) return callback(err);
                            callback(err);
                        });
                    });
                }, function (err) {
                    if (err) return callback(err);
                    callback();
                });
            });
        }
    ], function (err) {
        handleResult(err, result);
    });
}

/**
 * 获取页面资源
 * @param idFields
 * @params includeChildPages
 * @param includedUrlString
 * @param handleResult
 */
function getPageResources(idFields, includeChildPages, includedUrlString, handleResult) {
    var idPrefix = idFields.join("-");
    var pageUrl = null;
    var linkCount = 0;
    var result = [];
    async.series([
        function (callback) {
            var siteField = true;
            async.forEachSeries(idFields, function (idField, callback) {
                if (siteField) {
                    siteField = false;
                    //
                    var siteIndex = idFields[0];
                    //log("Getting site url with index "+siteIndex);
                    redis.zrange("pages", siteIndex, siteIndex, function (err, siteUrls) {
                        if (err) return callback(err);
                        if (siteUrls.length==0) {
                            log("No site with index "+siteIndex);
                            return callback("No site with index "+siteIndex);
                        }
                        pageUrl = siteUrls[0];
                        callback();
                    });
                } else {
                    var linkIndex = idField;
                    //log("Getting link url with id "+linkId+"...");
                    redis.zrange("links_of_"+pageUrl, linkIndex, linkIndex, function (err, linkUrls) {
                        if (err) return callback(err);
                        if (linkUrls.length==0) {
                            log("No link with index "+idPrefix);
                            return callback("No link with index "+linkIndex);
                        }
                        pageUrl = linkUrls[0];
                        callback();
                    });
                }
            }, function (err) {
                callback(err);
            });
        },
        function (callback) {
            var includedPages = new hashes.HashSet();
            includedPages.add(pageUrl);
            getResourcesInPage(pageUrl, 0, includeChildPages, includedUrlString, includedPages, function (err, resources) {
                if (err) return callback(err);
                result = resources;
                callback();
            });
        }
    ], function (err) {
        handleResult(err, result);
    });
}

/**
 * 获取站点资源信息
 * @param siteIndex
 * @param includeChildPages
 * @param includedUrlString
 * @param handleResult
 */
function getSiteResources(siteIndex, includeChildPages, includedUrlString, handleResult) {
    var siteUrl = null;
    var result = null;
    async.series([
        function (callback) {
            //log("Getting site url with index "+siteIndex);
            redis.zrange("pages", siteIndex, siteIndex, function (err, siteUrls) {
                if (err) return callback(err);
                if (siteUrls.length==0) {
                    log("No site with index "+siteIndex);
                    return callback("No site with index "+siteIndex);
                }
                siteUrl = siteUrls[0];
                callback();
            });
        },
        function (callback) {
            var includedPages = new hashes.HashSet();
            includedPages.add(siteUrl);
            getResourcesInPage(siteUrl, 0, includeChildPages, includedUrlString, includedPages, function (err, resources) {
                if (err) return callback(err);
                result = resources;
                callback();
            });
        }
    ], function (err) {
        handleResult(err, result);
    });
}

/**
 * 获取子页面信息
 * @param id
 * @param includeChildPages
 * @param includedUrlString
 * @param handleResult
 */
function getResources(id, includeChildPages, includedUrlString, handleResult) {
    var idFields = id.split("-");
    if (idFields.length==1) {
        getSiteResources(id, includeChildPages, includedUrlString, handleResult);
    } else {
        getPageResources(idFields, includeChildPages, includedUrlString, handleResult);
    }
}

/**
 * 获取子页面信息
 * @param idFields
 * @param handleResult
 */
function getChildPages(idFields, handleResult) {
    var idPrefix = idFields.join("-");
    var pageUrl = null;
    var linkCount = 0;
    var result = [];
    async.series([
        function (callback) {
            var siteField = true;
            async.forEachSeries(idFields, function (idField, callback) {
                if (siteField) {
                    siteField = false;
                    //
                    var siteIndex = idFields[0];
                    //log("Getting site url with index "+siteIndex);
                    redis.zrange("pages", siteIndex, siteIndex, function (err, siteUrls) {
                        if (err) return callback(err);
                        if (siteUrls.length==0) {
                            log("No site with index "+siteIndex);
                            return callback("No site with index "+siteIndex);
                        }
                        pageUrl = siteUrls[0];
                        callback();
                    });
                } else {
                    var linkIndex = idField;
                    //log("Getting link url with id "+linkId+"...");
                    redis.zrange("links_of_"+pageUrl, linkIndex, linkIndex, function (err, linkUrls) {
                        if (err) return callback(err);
                        if (linkUrls.length==0) {
                            log("No link with index "+idPrefix);
                            return callback("No link with index "+linkIndex);
                        }
                        pageUrl = linkUrls[0];
                        callback();
                    });
                }
            }, function (err) {
                callback(err);
            });
        },
        function (callback) {
            //log("Counting links of page "+pageUrl+"...");
            redis.zcard("links_of_"+pageUrl, function (err, count) {
                if (err) return callback(err);
                linkCount = count;
                //log("There are "+linkCount+" link(s) on page "+pageUrl);
                callback();
            });
        },
        function (callback) {
            //log("Getting all link(s) url(s) of page "+pageUrl+"...");
            redis.zrange("links_of_"+pageUrl, 1, linkCount, function (err, linkUrls) {
                if (err) return callback(err);
                var nextLinkId = 1;
                async.forEachSeries(linkUrls, function (linkUrl, callback) {
                    var nextId = idPrefix+"-"+(nextLinkId++);
                    var isPageUrl = false;
                    async.series([
                        function (callback) {
                            //log("Finding page title for link url "+linkUrl+"...");
                            redis.hget("page:"+linkUrl, "title", function (err, title) {
                                if (err) return callback(err);
                                if (!title) {
                                    //log("Link "+linkUrl+" is not a page");
                                    return callback();
                                }
                                isPageUrl = true;
                                //log("Link "+linkUrl+" is a page");
                                callback();
                            });
                        },
                        function (callback) {
                            if (!isPageUrl) return callback();
                            var page = {id:nextId,name:linkUrl,children:[]};
                            result.push(page);
                            callback();
                        }
                    ], function (err) {
                        if (err) return callback(err);
                        callback(err);
                    });
                }, function (err) {
                    if (err) return callback(err);
                    callback();
                });
            });
        }
    ], function (err) {
        handleResult(err, result);
    });
}

/**
 * 获取站点页面信息
 * @param siteIndex
 * @param handleResult
 */
function getSitePages(siteIndex, handleResult) {
    var siteUrl = null;
    var linkCount = 0;
    var result = [];
    async.series([
        function (callback) {
            //log("Getting site url with index "+siteIndex);
            redis.zrange("pages", siteIndex, siteIndex, function (err, siteUrls) {
                if (err) return callback(err);
                if (siteUrls.length==0) {
                    log("No site with index "+siteIndex);
                    return callback("No site with index "+siteIndex);
                }
                siteUrl = siteUrls[0];
                callback();
            });
        },
        function (callback) {
            //log("Counting links of page "+siteUrl+"...");
            redis.zcard("links_of_"+siteUrl, function (err, count) {
                if (err) return callback(err);
                linkCount = count;
                //log("There are "+linkCount+" link(s) on page "+siteUrl);
                callback();
            });
        },
        function (callback) {
            //log("Getting all link(s) url(s) of page "+siteUrl+"...");
            redis.zrange("links_of_"+siteUrl, 1, linkCount, function (err, linkUrls) {
                if (err) return callback(err);
                var nextLinkId = 1;
                async.forEachSeries(linkUrls, function (linkUrl, callback) {
                    var nextId = siteIndex+"-"+(nextLinkId++);
                    var isPageUrl = false;
                    async.series([
                        function (callback) {
                            //log("Finding page title for link url "+linkUrl+"...");
                            redis.hget("page:"+linkUrl, "title", function (err, title) {
                                if (err) return callback(err);
                                if (!title) {
                                    //log("Link "+linkUrl+" is not a page");
                                    return callback();
                                }
                                isPageUrl = true;
                                //log("Link "+linkUrl+" is a page");
                                callback();
                            });
                        },
                        function (callback) {
                            if (!isPageUrl) return callback();
                            var page = {id:nextId,name:linkUrl,children:[]};
                            result.push(page);
                            callback();
                        }
                    ], function (err) {
                        if (err) return callback(err);
                        callback(err);
                    });
                }, function (err) {
                    if (err) return callback(err);
                    callback();
                });
            });
        }
    ], function (err) {
        handleResult(err, result);
    });
}

/**
 * 获取页面信息
 * @param id
 * @param handleResult
 */
function getPages(id, handleResult) {
    var idFields = id.split("-");
    if (idFields.length==1) {
        getSitePages(id, handleResult);
    } else {
        getChildPages(idFields, handleResult);
    }
}

/**
 * 获取所有站点信息
 * @param handleResult
 */
function getAllSites(handleResult) {
    var siteCount;
    var root = {id:"root",name:"根", children:[]};
    async.series([
        function (callback) {
            //log("Counting sites...");
            redis.zcard("pages", function (err, count) {
                if (err) return callback(err);
                siteCount = count;
                //log("Total "+siteCount+" sites");
                callback();
            });
        },
        function (callback) {
            //log("Getting all sites urls...");
            redis.zrange("pages", 1, siteCount, function (err, siteUrls) {
                if (err) return callback(err);
                var nextId = 1;
                async.forEachSeries(siteUrls, function (siteUrl, callback) {
                    var currentId = nextId++;
                    var siteName = url.parse(siteUrl).hostname;
                    var site = {id:currentId+"",name:siteName,children:[]};
                    root.children.push(site);
                    //
                    //log("Found site: "+siteUrl);
                    callback();
                }, function (err) {
                    if (err) return callback(err);
                    callback();
                });
            });
        }
    ], function (err) {
        handleResult(err, root);
    });
}

function main(fn) {
    fn();
}

void main(function () {

    redis = _redis.createClient(REDIS_PORT, REDIS_SERVER);
    redis.on("error", function (err) {
        log("Error " + err);
    });
});
