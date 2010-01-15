Object.extend = function (dest, source) {
	for (property in source) dest[property] = source[property];
	return dest;
};

Object.extend ( Function.prototype,
{
	wrap : function (wrapper) {
		var method = this;
		var bmethod = (function(_method) {
			return function () {
				this.$$$parentMethodStore$$$ = this.$proceed;
				this.$proceed = function() { return _method.apply(this, arguments); };
			};
		})(method);
		var amethod = function () {
			this.$proceed = this.$$$parentMethodStore$$$;
			if (this.$proceed == undefined) delete this.$proceed;
			delete this.$$$parentMethodStore$$$;
		};
		var value = function() { bmethod.call(this); retval = wrapper.apply(this, arguments); amethod.call(this); return retval; };
		return value;
	}
});

String.prototype.cap = function() {
	return this.charAt(0).toUpperCase() + this.substring(1).toLowerCase();
};

String.prototype.cap = String.prototype.cap.wrap(
	function(each) {
		if (each && this.indexOf(" ") != -1) {
			return this.split(" ").map(
				function (value) {
					return value.cap();
				}
			).join(" ");
		} else {
			return this.$proceed();
	}
});

Object.extend( Array.prototype,
{
	map : function(fun) {
		if (typeof fun != "function") throw new TypeError();
		var len = this.length;
		var res = new Array(len);
		var thisp = arguments[1];
		for (var i = 0; i < len; i++) { if (i in this) res[i] = fun.call(thisp, this[i], i, this); }
		return res;
	}
});
assertEquals("Test1 test1", "test1 test1".cap());
assertEquals("Test2 Test2", "test2 test2".cap(true));

