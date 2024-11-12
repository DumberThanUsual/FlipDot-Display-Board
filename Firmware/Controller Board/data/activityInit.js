print("hiiii");

const attrHandler = {
  get: function(target, prop, receiver) {
    return getAttribute(target.pointer, prop);
  },
  set: function(target, prop, value, receiver) {
    setAttribute(target.pointer, prop, value)
    return;
  }
};

function Child(type, pointer, children, attributes) {
  this.type = type;
  this.pointer = pointer;
  this._children = [];
  this.children = children;
  this.attributes = new Proxy(this, attrHandler);
  var entries = Object.keys(attributes);
  var i, n;
  for (i = 0, n = entries.length; i < n; i++) {
    print(i.toString());
    this.attributes[entries[i]] = attributes[entries[i]];
  }
}

Child.prototype = {
  set children(children) {
    var i, n;
    for (i = 0, n = children.length; i < n; i++) {
      this._children.push(
        new Child(
          children[i].type,
          createElement(this.pointer, children[i].type),
          children[i].children,
          children[i].attributes
        )
      );
    }
  }
};

const activity = {
  _children: [],
  set children(children) {
    var i, n;
    for (i = 0, n = children.length; i < n; i++) {
      this._children.push(
        new Child(
          children[i].type,
          createElement(objectPointer, children[i].type),
          children[i].children,
          children[i].attributes
        )
      );
    }
  },
  get children(children) {
    return this._children;
  }
}