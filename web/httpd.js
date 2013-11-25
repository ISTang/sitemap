/**
 * Created with JetBrains WebStorm.
 * User: joebin
 * Date: 13-4-26
 * Time: 下午9:35
 */
var fs = require('fs');
var express = require('express');
var querystring = require("querystring");
var async = require('async');
//
var config = require(__dirname + '/config');
var utils = require(__dirname + '/utils');
var db = require(__dirname + '/db');
//

// 定义常量
const HTTPD_PORT = config.HTTPD_PORT;
const LOG_ENABLED = config.LOG_ENABLED;
const GRACE_EXIT_TIME = config.GRACE_EXIT_TIME;

var webapp = express();

// 定义共享环境
webapp.configure(function () {
    webapp.engine('.html', require('ejs').__express);
    webapp.set('views', __dirname + '/views');
    webapp.set('view engine', 'html');

    //webapp.use(express.logger());
    webapp.use(function (req, res, next) {
        /*var data = '';
         req.setEncoding('utf-8');
         req.on('data', function (chunk) {
         data += chunk;
         });
         req.on('end', function () {
         req.rawBody = data;
         next();
         });*/
        //res.setHeader("Content-Type", "application/json;charset=utf-8");
        next();
    });
    webapp.use(express.bodyParser()); // can't coexists with req.on(...)!
    webapp.use(express.methodOverride());
    webapp.use(webapp.router);
});
// 定义开发环境
webapp.configure('development', function () {
    webapp.use(express.static(__dirname + '/public'));
    webapp.use(express.errorHandler({dumpExceptions: true, showStack: true}));
});
// 定义生产环境
webapp.configure('production', function () {
    var oneYear = 31557600000;
    webapp.use(express.static(__dirname + '/public', { maxAge: oneYear }));
    webapp.use(express.errorHandler());
});

var logStream = fs.createWriteStream("logs/httpd.log", {"flags": "a"});

Date.prototype.Format = utils.DateFormat;

/*
 * Print log
 */
function log(msg) {

    var now = new Date();
    var strDatetime = now.Format("yyyy-MM-dd HH:mm:ss");
    var buffer = "[" + strDatetime + "] " + msg + "[httpd]";
    if (logStream != null) logStream.write(buffer + "\r\n");
    if (LOG_ENABLED) console.log(buffer);
}

function main(fn) {
    fn();
}

var exitTimer = null;
function aboutExit() {

    if (exitTimer) return;

    exitTimer = setTimeout(function () {

        log("web app exit...");

        process.exit(0);

    }, GRACE_EXIT_TIME);
}

// 程序入口
void main(function () {
    process.on('SIGINT', aboutExit);
    process.on('SIGTERM', aboutExit);

    db.openDatabase(function (err) {
        if (err) {
            process.exit(1);
        }

        webapp.get('/', function (req, res) {
            log("打开首页...");
            res.setHeader("Content-Type", "text/html");
            res.render('index', {
                pageTitle: '网站元素分析 - 首页'
            });
        });

        webapp.get('/sites/:id', function (req, res) {
            res.setHeader("Content-Type", "application/json;charset=utf-8");

            var siteId = querystring.unescape(req.params.id);
            if (siteId == "root") {
                log("获取主站点...");
                db.getMainSites(function (err, root) {
                    if (err) res.json({id: "root", name: "根", children: [
                        {id: "1", name: err}
                    ]});
                    else res.json(root);
                });
            } else {
                log("获取站点 " + siteId + " 的子站点...");
                var pos = siteId.indexOf(":");
                if (pos == -1) {
                    res.json({children: [
                        {id: siteId + "_1", name: err}
                    ]});
                    return;
                }
                var siteTag = parseInt(siteId.substring(0, pos), 10);
                var siteName = siteId.substring(pos + 1);
                db.getSiteHosts(siteTag, siteName, function (err, hosts) {
                    if (err) {
                        res.json({children: [
                            {id: siteId + "_1", name: err}
                        ]});
                        return;
                    }

                    utils.makeDomainTree(siteName, hosts, function (err, childSites) {
                        if (err) log(err);
                        else {
                            var result = [];
                            async.forEachSeries(childSites, function (childSite, callback) {
                                    db.countFailedPages(childSite.name, null, null, function (err, failedPageCount) {
                                       if (err) return callback(err);
                                        if (failedPageCount>0) {
                                            childSite.name += "("+failedPageCount+")";
                                            if (childSite.children) childSite.children = [];
                                            result.push(childSite);
                                        }
                                        callback();
                                    });
                                }, function (err) {
                                    if (err) res.json({children: [
                                        {id: siteId + "_1", name: err}
                                    ]});
                                    else res.json({children: result});
                                }
                            );
                        }
                    });
                });
            }
        });

        webapp.get('/failed_pages/:site_id', function (req, res) {
            var siteId = req.params.site_id;
            var pos = siteId.indexOf(":");
            if (pos == -1) {
                res.json({children: [
                    {id: siteId + "_1", name: err}
                ]});
                return;
            }
            //
            var keys = Object.keys(req.query);
            var sortBy = {};
            for (var i in keys) {
                var fieldName = keys[i]
                if (/sort\([\s-].+\)/g.test(fieldName)) {
                    var s = fieldName.substring(6, fieldName.length - 1);
                    if (s == 'id') s = '_id';
                    else if (s == 'problem') s = 'reason';
                    sortBy[s] = (fieldName.substr(5, 1) == "-" ? -1 : 1);
                    break;
                }

            }
            //var siteTag = parseInt(siteId.substring(0, pos), 10);
            var siteName = siteId.substring(pos + 1);
            var includeChildSites = req.query.includeChildSites === "false" ? false : true;
            var problem = req.query.problem ? querystring.unescape(req.query.problem) : "";
            var exportFile = req.query.export === "true" ? true : false;

            var range = req.headers["range"];
            if (range) {
                var indexOfDash = range.indexOf("-");
                var indexOfEqual = range.indexOf("=");
                var a = parseInt(range.substring(indexOfEqual + 1, indexOfDash), 10);
                var b = parseInt(range.substring(indexOfDash + 1), 10);
                range = {from: a, to: b}
            }

            db.getFailedPages(siteName, includeChildSites, problem, range, sortBy, function (err, totalRecords, pages) {
                if (!exportFile) {
                    res.setHeader("Content-Type", "application/json;charset=utf-8");
                    res.setHeader("Content-Range", "items " + range.from + "-" + range.to + "/" + totalRecords);
                    if (err) res.json([
                        {id: 0, row: 1, url: err, reason: "错误"}
                    ]);
                    else res.json(pages);
                } else {
                    res.setHeader("Content-Disposition", "attachment; filename=\"" + siteName + "\"");
                    if (err) {
                        res.write(err);
                    } else {
                        for (var i in pages) {
                            var page = pages[i];
                            res.write(page.id + ": " + page.url + " [" + page.reason + "]\r\n");
                        }
                    }
                    res.end();
                }
            });
        });

        webapp.listen(HTTPD_PORT);
        log('HTTPD process is running at port ' + HTTPD_PORT + '...');

    });

});
