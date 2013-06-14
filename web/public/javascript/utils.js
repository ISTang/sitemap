/**
 * Created with JetBrains WebStorm.
 * User: 教兵
 * Date: 13-5-3
 * Time: 上午10:41
 * To change this template use File | Settings | File Templates.
 */

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

String.prototype.contains = contains;
