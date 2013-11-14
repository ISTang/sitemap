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

        var siteTag = parseInt(process.argv[2], 10);
        var siteName = process.argv[3];

        db.getSiteHosts(siteTag, siteName, function (err, hosts) {
            if (err) {
                log(err);
                process.exit(3);
            }

            if (hosts) {
                log("站点 " + siteName + " 共有 " + hosts.length + " 个主机");
                for (var hostIndex in hosts) {

                    var host = hosts[hostIndex];
                    log("[#" + (parseInt(hostIndex, 10) + 1) + "] " + host.name);
                }

                process.exit(0);
            }
        }, function (err) {

            if (err) log(err);
            process.exit(0);
        });
    });
});
