var fs = require('fs');
var db = require(__dirname + '/db');
var config = require(__dirname + '/config');
var utils = require(__dirname + '/utils');

const LOG_ENABLED = config.LOG_ENABLED;

var logStream = LOG_ENABLED ? fs.createWriteStream("logs/sites.log", {"flags": "a"}) : null;

Date.prototype.Format = utils.DateFormat;

/*
 * Print log
 */
function log(msg) {

    var now = new Date();
    var strDatetime = now.Format("yyyy-MM-dd HH:mm:ss");
    var buffer = "[" + strDatetime + "] " + msg + "[sites]";
    if (logStream != null) logStream.write(buffer + "\r\n");
    console.log(buffer);
}

function main(fn) {
    fn();
}

void main(function () {

	db.openDatabase(function(err) {

		if (err) process.exit(1);

                log("获取所有站点...");
                db.getAllSites(function (err, root) {

                    for (siteIndex in root.children) {

                        var site = root.children[siteIndex];
                        log("站点 "+site.name+" 有以下子站点:");
                        for (hostIndex in site.children) {

                            var host = site.children[hostIndex];
                            log("[#"+(parseInt(hostIndex,10)+1)+"] "+host.name);
                        }
                    }

    		    process.exit(0);
                });
	});
});
