var db = require(__dirname + '/db');

function main(fn) {
    fn();
}

void main(function () {

	db.countSiteResources("http://www.qq.com/", true, function (err, count) {
	
		if (err) return console.log(err);
		console.log("Total "+count+" resources.");
	});
});
