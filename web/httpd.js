/**
 * Created with JetBrains WebStorm.
 * User: joebin
 * Date: 13-4-26
 * Time: 下午9:35
 */
var fs = require('fs');
var express = require('express');
var querystring = require("querystring");
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

    db.openDatabase(function(err) {
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
           var siteId = querystring.unescape(req.params.id);
           if (siteId=="root") {
               log("获取主站点...");
                db.getMainSites(function (err, root) {
                    res.setHeader("Content-Type", "application/json;charset=utf-8");
                    if (err) res.json({id:"root",name:"根", children:[{id:"1",name:err}]});
                    else res.json(root);
                });
            } else {
                log("获取主站点 "+siteId+" 的子站点...");
                var hosts = [];
                db.getChildSites(siteId, hosts, function (err, siteName) {

                    // 子域名分析
                    var reversedHosts = [];
                    var reversedHostIds = [];
                    for (var i in hosts) {
                        var host = hosts[i].name;
                        var reversedHost = host.split('.').reverse().join('.');
                        reversedHosts.push(reversedHost);
                        reversedHostIds[reversedHost] =  hosts[i].id;
                    }
                    reversedHosts.sort();
                    //
                    var root = {id: "root", name: siteName.split('.').reverse().join('.'), children:[]};
                    var nodeStack = [];
                    nodeStack.push(root);
                    for (var j in reversedHosts) {
                        var reversedHost = reversedHosts[j];
                        var o = {id:reversedHostIds[reversedHost], name:reversedHost};
                        do {
                            var node = nodeStack.pop();
                            if (reversedHost.indexOf(node.name)==0) {
                                if (!node.children) node.children = [];
                                node.children.push(o);
                                nodeStack.push(node);
                                break;
                            }
                        } while(true);
                    }
                    var childSites = nodeStack.shift().children;

                    res.setHeader("Content-Type", "application/json;charset=utf-8");
                    if (err) res.json({children:[]});
                    else res.json({children:childSites});
                });
            }
        });

        webapp.get('/resources/:pageId', function (req, res) {
            var pageId = req.params.pageId;
            var includeChildPages = req.query.includeChildPages==="true"?true:false;
            var includedUrlString = req.query.includedUrlString?querystring.unescape(req.query.includedUrlString):"";
            var exportFile = req.query.export==="true"?true:false;
            db.getResources(pageId, includeChildPages, includedUrlString, function (err, resources) {
                if (!exportFile) {
                    res.setHeader("Content-Type", "application/json;charset=utf-8");
                    if (err) res.json([{id:0,row:1,url:err,type:"错误"}]);
                    else res.json(resources);
                } else {
                    res.setHeader("Content-Disposition", "attachment; filename=\"" + pageId + "\"");
                    if (err) {
                        res.write(err);
                    } else {
                        for (var i in resources) {
                            var resource = resources[i];
                            res.write("#"+resource.row+"["+resource.type+"] "+resource.url+"\r\n");
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
