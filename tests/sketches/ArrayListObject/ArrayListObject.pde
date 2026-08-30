// Regression fixture: `ArrayList<Object>` + `new Object()` + a user class
// carrying an ArrayList<Object> field must each transpile and link cleanly.

class Bag {
  ArrayList<Object> items;
  Bag() {
    items = new ArrayList<Object>();
  }
  void put(Object o) {
    items.add(o);
  }
  Object take(int i) {
    return (Object) items.get(i);
  }
}

ArrayList<Object> objects;
Bag bag;

void setup() {
  objects = new ArrayList<Object>();
  bag = new Bag();
  objects.add(new Object());
  objects.add(new Object());
  println("objects size = " + objects.size());
  Object o = (Object) objects.get(0);
  println("first is ok");
  bag.put(new Object());
  Object got = bag.take(0);
  println("take ok");
  noLoop();
}

void draw() {
  background(10);
}