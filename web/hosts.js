var fs = require('fs');
var db = require(__dirname + '/db');
var config = require(__dirname + '/config');
var utils = require(__dirname + '/utils');

const LOG_ENABLED = config.LOG_ENABLED;

var logStream = LOG_ENABLED ? fs.createWriteStream("logs/hosts.log", {"flags": "a"}) : null;

Date.prototype.Format = utils.DateFormat;

/*
 * Print log
 */
function log(msg) {

    var now = new Date();
    var strDatetime = now.Format("yyyy-MM-dd HH:mm:ss");
    var buffer = "[" + strDatetime + "] " + msg + "[hosts]";
    if (logStream != null) logStream.write(buffer + "\r\n");
    console.log(buffer);
}

function main(fn) {
    fn();
}

void main(function () {

    if (process.argv.length < 4) {

        console.log("用法: " + process.argv[0] + " " + process.argv[1] + " <siteTag> <siteName>");
        process.exit(1);
    }

    db.openDatabase(function (err) {

        if (err) {
            process.exit(2);
        }

        var siteTag = process.argv[2];
        var siteName = process.argv[3];

        db.getChildSites(siteTag, siteName, function (err, childSites) {

            if (err) {
                log(err);
                process.exit(3);
            }

            log("站点 " + siteName + " 有以下子站点:");
            for (var hostIndex in childSites) {

                var host = childSites[hostIndex];
                log("[#" + (parseInt(hostIndex, 10) + 1) + "] " + host.name);
            }

            process.exit(0);
        });
    });
});
