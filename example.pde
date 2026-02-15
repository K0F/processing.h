// Very simple processing sketch

void setup(){
	size(640,480);
	
}

void draw(){
	background(0,0,0);	
	stroke(255,255,255,255);
	line(0,frameCount%width,0,frameCount%width);
}
