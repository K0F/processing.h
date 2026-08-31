// Regression fixture: String instance methods, string += concatenation, and
// the == / != string operators must each transpile and link cleanly.
String s1;
String joined;

void setup() {
  size(200, 200);
  s1 = "Hello World";
  s1 += "!";
  String upper = s1.toUpperCase();
  String lower = s1.toLowerCase();
  String head = s1.substring(0, 5);
  String tail = s1.substring(6);
  int at = s1.indexOf("World");
  String replaced = s1.replace("World", "There");
  boolean same = s1.equals("Hello World!");
  boolean diff = s1 != "Something else";
  int n = s1.length();
  String trimmed = "  pad  ".trim();

  joined = "result:";
  joined += upper;
  joined += lower;

  if (same && diff && n > 0 && at > 0) {
    println(joined);
    println(head);
    println(tail);
    println(replaced);
    println(trimmed);
  }
  noLoop();
}

void draw() {
  background(10);
}
