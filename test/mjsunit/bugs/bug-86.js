var aList = [1, 2, 3];
var loopCount = 0;
var leftThroughFinally = false;
for (x in aList) {
  leftThroughFinally = false;
  try {
    throw "ex1";
  } catch(er1) {
    loopCount += 1;
  } finally {
    enteredFinally = true;
    continue;
  }
  leftThroughFinally = false;
}
assertEquals(loopCount, 3);
assertTrue(enteredFinally);
