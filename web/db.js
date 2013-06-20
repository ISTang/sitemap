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

exports.getAllSites = getAllSites;
exports.getPages = getPages;
//exports.getResources = getResources;
exports.countSiteResources = countSiteResources;

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

function countChildPageResources(collection, pageUrl, rescure, handleResult) {

	collection.findOne({url: pageUrl}).toArray(function (err, page) {
	
		if (err) return handleResult(err);
	
		if (!page || !page.links) return handleResult(null, 0);
		
		var childPageUrls = [];
		for (var linkIndex in page.links) {

			var link = page.links[linkIndex];
			if (link.type in ["anchor", "frame", "iframe"]) {
			
				childPageUrls.push(link.url);
			}
		}
		
		var sum = page.links.length;
		if (childPageUrls.length!=0 && rescure) {
		
			asyns.forEachSeriers(childPageUrls, function (childPageUrl, callback) {
			
				countChildPageResources(collection, childPageUrl, function (err, count) {
				
					if (err) return callback(err);
					sum += count;
					callback();
				});
			}, function (err) {
			
				handleResult(err, sum);
			});

		} else {
		
			handleResult(null, sum);
		}
	});
}

function countSiteResources(siteUrl, rescure, handleResult) {

    db.collection("page", {safe:false}, function(err, collection){

		if (err) return handleResult(err);
		countChildPageResources(collection, siteUrl, rescure, function (err, count) {
			
			handleResult(err, 1+count);
		});
    });
}

function main(fn) {
    fn();
}

void main(function () {

    db = new mongo.Db("sitemap", new mongo.Server(MONGO_SERVER, MONGO_PORT), {safe:true});

    log("正在连接数据库...");
    db.open(function(err, p_client) {
      if (err) {
        log(err);
        process.exit(1);
        return;
      }
      log("已连接到数据库。");
    });
});
