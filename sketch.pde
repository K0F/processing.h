
void setup(){
  size(814,576);
}

void draw(){
  PVector pos = new PVector(0,0);
  PVector vel = new PVector(100,100);
  pos.add(vel);
  
  background(255);
  
  pushMatrix();
  translate(width/2,height/2);
  
  stroke(0);
  line(0,0,pos.x,pos.y);
  
  popMatrix();
}
