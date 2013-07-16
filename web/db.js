/**
 * Created with JetBrains WebStorm.
 * User: joebin
 * Date: 13-3-18
 * Time: 下午11:01
 */
// 引入依赖包
var fs = require('fs');
var async = require('async');
var mongo = require('mongodb');
var hashes = require('hashes');
var url = require('url');
//
var config = require(__dirname + '/config');
var utils = require(__dirname + '/utils');

exports.openDatabase = openDatabase;
exports.getAllSites = getAllSites;
exports.getPages = getPages;
//exports.getResources = getResources;
exports.countSite = countSite;
exports.countSite2 = countSite2;

const MONGO_SERVER = config.MONGO_SERVER;
const MONGO_PORT = config.MONGO_PORT;
//
const LOG_ENABLED = config.LOG_ENABLED;

var logStream = LOG_ENABLED ? fs.createWriteStream("logs/db.log", {"flags": "a"}) : null;

var db;

String.prototype.contains = utils.contains;
Date.prototype.Format = utils.DateFormat;

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
 * 获取页面信息
 * @param pageUrl
 * @param handleResult
 */
function getPages(pageUrl,handleResult) {
  handleResult("Not implemented!");
}

/**
 * 获取所有站点信息
 * @param handleResult
 */
function getAllSites(handleResult) {
    var root = {id:"root",name:"根", children:[]};
    db.collection("site", {safe:false}, function(err, collection){
        if (err) return handleResult(err);
        collection.find().toArray(function (err, sites) {
            if (err) return handleResult(err);
            for (var siteIndex in sites) {

                var site = sites[siteIndex];

                var siteName = url.parse(site.url).hostname;
                var site = {id:site.url,name:siteName,children:[]};
                root.children.push(site);
                //
                //log("Found site: "+siteUrl);
            }
            handleResult(null, root);
        });
    });
}

function countChildPage(collection, pageUrls, resUrls, pageUrl, rescure, handleResult) {

	//log("正在查找页面 "+pageUrl+"...");
	collection.findOne({url: pageUrl}, function (err, page) {
	
		if (err) return handleResult(err);
	
		if (page==null)  {

			//log("页面 "+pageUrl+" 尚未爬出!");
			return handleResult(null, 0, 1, 0);
		}

		var okPageSum = 1, unknownPageSum = 0, resSum = 0;

		for (var linkIndex in page.links_res) {

			var link = page.links_res[linkIndex];

			if (resUrls.contains(link.url)) continue;

			resUrls.add(link.url);
			resSum++;
		}
		
		async.forEachSeries(page.links_page, function (childPage, callback) {
			
                        var childPageUrl = childPage.url;

                        if (pageUrls.contains(childPageUrl)) return callback();

                        pageUrls.add(childPageUrl);

			if (rescure) {

				countChildPage(collection, pageUrls, resUrls, childPageUrl, rescure, function (err, okPageCount, unknownPageCount, resCount) {
					
					if (err) return callback(err);

					okPageSum += okPageCount;
					unknownPageSum += unknownPageCount;
					resSum += resCount;

					callback();
				});
			} else {

				callback();
			}
		}, function (err) {
			
			handleResult(err, okPageSum, unknownPageSum, resSum);
		});

	});
}

function countSite(siteUrl, rescure, handleResult) {

    log("正在统计站点 "+siteUrl+" 页面/资源个数...");
    db.collection("page", {safe:false}, function(err, collection){

		if (err) return handleResult(err);

		var pageUrls = new hashes.HashSet();
                var resUrls = new hashes.HashSet();
		pageUrls.add(siteUrl);
		countChildPage(collection, pageUrls, resUrls, siteUrl, rescure, function (err, okPageCount, unknownPageCount, resCount) {
			
			handleResult(err, okPageCount, unknownPageCount, resCount);
		});
    });
}

function countSite2(siteUrl, handleResult) {

    log("正在统计站点 "+siteUrl+" 页面/资源个数...");
    db.collection("site", {safe:false}, function(err, collection){

        if (err) return handleResult(err);

        collection.findOne({url:siteUrl}, function (err, site) {

            if (err) return handleResult(err);
            if (site==null) return handleResult("该站点不存在！")
            handleResult(null, site.links_page.length, site.links_res.length);
        });
    });
}

function openDatabase(callback) {
    db = new mongo.Db("sitemap", new mongo.Server(MONGO_SERVER, MONGO_PORT), {safe:true});

    log("正在连接数据库...");
    db.open(function(err, p_client) {

      	if (err) {

		log("无法打开数据库: "+err);
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

});
