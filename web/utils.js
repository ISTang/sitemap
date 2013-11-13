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
exports.NumberFormat = NumberFormat;

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
    var date = new Date(parseInt(fields[1]), parseInt(fields[2]) - 1, parseInt(fields[3]),
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

/**
 * Formats the number according to the ‘format’ string; adherses to the american number standard where a comma is inserted after every 3 digits.
 * note: there should be only 1 contiguous number in the format, where a number consists of digits, period, and commas
 *        any other characters can be wrapped around this number, including ‘$’, ‘%’, or text
 *        examples (123456.789):
 *          ‘0′ - (123456) show only digits, no precision
 *          ‘0.00′ - (123456.78) show only digits, 2 precision
 *          ‘0.0000′ - (123456.7890) show only digits, 4 precision
 *          ‘0,000′ - (123,456) show comma and digits, no precision
 *          ‘0,000.00′ - (123,456.78) show comma and digits, 2 precision
 *          ‘0,0.00′ - (123,456.78) shortcut method, show comma and digits, 2 precision
 *
 * @method format
 * @param format {string} the way you would like to format this text
 * @return {string} the formatted number
 * @public
 */
function NumberFormat(format) {
    if (typeof format!=='string') {
        return '';
    } // sanity check
    var hasComma = -1 &lt; format.indexOf(','),
        psplit = format.stripNonNumeric().split('.'),
        that = this;
// compute precision
    if (1 < psplit.length) {
        // fix number precision
        that = that.toFixed(psplit[1].length);
    }
// error: too many periods
    else if (2 < psplit.length) {
        throw('NumberFormatException: invalid format, formats should have no more than 1 period: ' + format);
    }
// remove precision
    else {
        that = that.toFixed(0);
    }
// get the string now that precision is correct
    var fnum = that.toString();
// format has comma, then compute commas
    if (hasComma) {
        // remove precision for computation
        psplit = fnum.split('.');
        var cnum = psplit[0],
            parr = [],
            j = cnum.length,
            m = Math.floor(j / 3),
            n = cnum.length % 3 || 3; // n cannot be ZERO or causes infinite loop
        // break the number into chunks of 3 digits; first chunk may be less than 3
        for (var i = 0; i < j; i += n) {
            if (i != 0) {
                n = 3;
            }
            parr[parr.length] = cnum.substr(i, n);
            m -= 1;
        }
        // put chunks back together, separated by comma
        fnum = parr.join(',');
        // add the precision back in
        if (psplit[1]) {
            fnum += '.' + psplit[1];
        }
    }
// replace the number portion of the format with fnum
    return format.replace(/[d,?.?]+/, fnum);
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
