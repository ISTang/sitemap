var fs = require('fs');
var db = require(__dirname + '/db');
var config = require(__dirname + '/config');
var utils = require(__dirname + '/utils');

const LOG_ENABLED = config.LOG_ENABLED;

var logStream = LOG_ENABLED ? fs.createWriteStream("logs/httpd.log", {"flags": "a"}) : null;

Date.prototype.Format = utils.DateFormat;

/*
 * Print log
 */
function log(msg) {

    var now = new Date();
    var strDatetime = now.Format("yyyy-MM-dd HH:mm:ss");
    var buffer = "[" + strDatetime + "] " + msg + "[count]";
    if (logStream != null) logStream.write(buffer + "\r\n");
    console.log(buffer);
}

function main(fn) {
    fn();
}

void main(function () {

	if (process.argv.length<3) {

		console.log("用法: "+process.argv[0]+" "+process.argv[1]+" <siteUrl>");
		process.exit(1);
	}

	db.openDatabase(function(err) {

		if (err) process.exit(2);

		db.countSite2(process.argv[2], function (err, pageCount, resCount) {
	
			if (err) log(err);
			else log("总共 "+pageCount+" 个页面，"+resCount+" 个资源。");

			process.exit(0);
		});
	});
});
