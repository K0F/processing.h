
void setup(){
  size(320,240);
}

void draw(){
  PVector pos = new PVector(0,0);
  PVector vel = new PVector(10,10);
  pos.add(vel);
  
  background(255);
  pushMatrix();
  translate(width/2,height/2);
  stroke(255);
  line(0,0,pos.x,pos.y);
  popMatrix();
}
