(function () {
  "use strict";

  // Override removeChild to prevent React exceptions when nodes are reparented
  const originalRemoveChild = Node.prototype.removeChild;
  Node.prototype.removeChild = function (child) {
    if (child && child.parentNode && child.parentNode !== this) {
      return originalRemoveChild.call(child.parentNode, child);
    }
    return originalRemoveChild.apply(this, arguments);
  };

  // Override insertBefore to prevent React exceptions when nodes are reparented
  const originalInsertBefore = Node.prototype.insertBefore;
  Node.prototype.insertBefore = function (newNode, referenceNode) {
    if (
      referenceNode &&
      referenceNode.parentNode &&
      referenceNode.parentNode !== this
    ) {
      return originalInsertBefore.call(
        referenceNode.parentNode,
        newNode,
        referenceNode
      );
    }
    return originalInsertBefore.apply(this, arguments);
  };
})();
