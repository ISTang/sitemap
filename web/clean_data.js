// 引入依赖包
var fs = require('fs');
var async = require('async');
var redis = require('redis');
//
var utils = require(__dirname + '/utils');

// 系统参数
const REDIS_HOST = 'localhost';
const REDIS_PORT = 6379;
//
const GRACE_EXIT_TIME = 1500;
const LOG_ENABLED = true;

var logStream = fs.createWriteStream(__dirname+"/logs/clean_data.log", {"flags":"a"});

Date.prototype.Format = utils.DateFormat;
String.prototype.trim = utils.StringTrim;
String.prototype.format = utils.StringFormat;

/*
 * Print log
 */
function log(msg) {
    var now = new Date();
    var strDatetime = now.Format("yyyy-MM-dd HH:mm:ss");
    var buffer = "[" + strDatetime + "] " + msg + "[clean_data]";
    if (logStream!=null) logStream.write(buffer + "\r\n");
    if ( LOG_ENABLED) console.log(buffer);
}

function cleanData(client, keysPattern, callback) {
    log("Finding keys \""+keysPattern+"\"...");
    client.keys(keysPattern, function (err, keys) {
        if (err) return callback(err);
        log("Total "+keys.length+" key(s) found.");
        async.forEach(keys, function(key, callback) {
            log("Deleting key "+key+"...");
            client.del(key);
            callback();
        }, function (err) {
            callback(err);
        });
    });
}

function main(fn) {
    fn();
}

var exitTimer = null;
function aboutExit() {

    if (exitTimer) return;

    exitTimer = setTimeout(function () {

        log("Exitting...");

        process.exit(0);

    }, GRACE_EXIT_TIME);
}

void main(function () {
  process.on('SIGINT', aboutExit);
  process.on('SIGTERM', aboutExit);

  if (process.argv.length<3) {
    log("Usage: clean_data <keysPattern> [redis_port]");
    process.exit(0);
  }

  log("Connecting...");
  var client = redis.createClient((process.argv.length>3?parseInt(process.argv[3]):REDIS_PORT), REDIS_HOST);
  client.on("error", function (err) {
    log(err);
  });
  client.on("connect", function () {
    log('Cleanning data...');
    cleanData(this, process.argv[2], function(err){
      if (err) log(err);
      else log('Done.');
      client.end();
      process.exit(0);
    });
  });
});
