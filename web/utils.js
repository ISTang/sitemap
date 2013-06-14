/**
 * Created with JetBrains WebStorm.
 * User: joebin
 * Date: 13-3-15
 * Time: 下午6:56
 */
//var crypto = require('crypto');
//var randomstring = require("randomstring");

// 导出函数
exports.DateFormat = DateFormat;
exports.DateParse = DateParse;
exports.StringFormat = StringFormat;
exports.StringTrim = StringTrim;
//exports.md5 = md5;
exports.contains = contains;

const DATE_FORMAT_REGEX = /(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})/g;

// 对Date的扩展，将 Date 转化为指定格式的String
// 月(M)、日(d)、小时(h)、分(m)、秒(s)、季度(q) 可以用 1-2 个占位符，
// 年(y)可以用 1-4 个占位符，毫秒(S)只能用 1 个占位符(是 1-3 位的数字)
// 例子：
// (new Date()).Format("yyyy-MM-dd hh:mm:ss.S") ==> 2006-07-02 08:09:04.423
// (new Date()).Format("yyyy-M-d h:m:s.S")			==> 2006-7-2 8:9:4.18
function DateFormat(fmt) { //author: meizz
    var o = {
        "M+": this.getMonth() + 1, //月份
        "d+": this.getDate(), //日
        "H+": this.getHours(), //小时
        "m+": this.getMinutes(), //分
        "s+": this.getSeconds(), //秒
        "q+": Math.floor((this.getMonth() + 3) / 3), //季度
        "S": this.getMilliseconds() //毫秒
    };
    if (/(y+)/.test(fmt)) fmt = fmt.replace(RegExp.$1, (this.getFullYear() + "").substr(4 - RegExp.$1.length));
    for (var k in o)
        if (new RegExp("(" + k + ")").test(fmt)) fmt = fmt.replace(RegExp.$1, (RegExp.$1.length == 1) ? (o[k]) : (("00" + o[k]).substr(("" + o[k]).length)));
    return fmt;
}

function DateParse(str) {
    var fields = str.split(DATE_FORMAT_REGEX);
    var date = new Date(parseInt(fields[1]), parseInt(fields[2])-1, parseInt(fields[3]),
        parseInt(fields[4]), parseInt(fields[5]), parseInt(fields[6]));
    return date;
}

function StringFormat() {
    var args = arguments;
    return this.replace(/\{(\d+)\}/g,
        function (m, i) {
            return args[i];
        });
}

function StringTrim() {
    return this.replace(/(^\s*)|(\s*$)/g, "");
}

/*function md5(str) {
    var md5sum = crypto.createHash('md5');
    md5sum.update(str);
    str = md5sum.digest('hex').toUpperCase();
    return str;
}*/

/*
 *
 *substr:子字符串
 *isIgnoreCase:忽略大小写
 */
function contains(substr, isIgnoreCase) {
    var string = this;
    if (isIgnoreCase) {
        string = string.toLowerCase();
        substr = substr.toLowerCase();
    }
    var startChar = substr.substring(0, 1);
    var strLen = substr.length;
    for (var j = 0; j < string.length - strLen + 1; j++) {
        if (string.charAt(j) == startChar)//如果匹配起始字符,开始查找
        {
            if (string.substring(j, j + strLen) == substr)//如果从j开始的字符与str匹配，那ok
            {
                return true;
            }
        }
    }
    return false;
}

function main(fn) {
    fn();
}

void main(function () {

    //var guid = Guid.Empty;//NewGuid();
    //console.log("GUID: "+guid.toString("N"));

    //console.log("random string: "+randomstring.generate(8));

    /*var a = new Date();  var aa=a.getTime();
    var b = DateParse("20130327225800"); var bb= b.getTime();
    console.log(aa);
    console.log(bb);
    console.log(''+a+','+b+','+(a-b)/1000);*/
});