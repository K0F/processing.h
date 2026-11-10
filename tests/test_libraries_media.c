/* Unit tests for Domain 2: Multimedia & Creative Coding Libraries
 * Tests video.h (Capture, Movie, GSPipeline), minim.h (Minim, AudioPlayer,
 * AudioInput, AudioSample, SineWave, FFT), midi.h (MidiBus), and arduino.h.
 */

#include "processing.h"
#include "libraries/video.h"
#include "libraries/minim.h"
#include "libraries/midi.h"
#include "libraries/arduino.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...) do { \
  checks++; \
  if (!(cond)) { \
    failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
  } \
} while (0)

static void test_video(void) {
  // Test Capture_list()
  const char **devs = Capture_list();
  CHECK(devs != NULL, "Capture_list() returns non-null pointer");

  // Test Capture constructor
  Capture cam = Capture_ctor(NULL, 640, 480, 30);
  CHECK(cam.width == 640, "cam.width=%d want 640", cam.width);
  CHECK(cam.height == 480, "cam.height=%d want 480", cam.height);
  CHECK(cam.fps == 30, "cam.fps=%d want 30", cam.fps);
  CHECK(cam.pixels != NULL, "cam.pixels is allocated");
  CHECK(cam.data != NULL, "cam.data is allocated");

  // Test video state helpers
  CHECK(_pde_video_available(&cam) == true, "video_available initially true");
  _pde_video_read(&cam);
  CHECK(_pde_video_available(&cam) == false, "video_available false after read");

  _pde_video_play(&cam);
  CHECK(cam.isPlaying == true, "cam.isPlaying true after play()");

  _pde_video_pause(&cam);
  CHECK(cam.isPaused == true && cam.isPlaying == false, "cam.isPaused true after pause()");

  _pde_video_loop(&cam);
  CHECK(cam.isLooping == true && cam.isPlaying == true, "cam.isLooping true after loop()");

  _pde_video_stop(&cam);
  CHECK(cam.isPlaying == false && cam.isLooping == false, "cam.isPlaying false after stop()");
  CHECK(_pde_video_time(&cam) == 0.0f, "video_time is 0.0 after stop()");
  CHECK(_pde_video_duration(&cam) == 0.0f, "video_duration is 0.0 for live capture");

  // Test Capture_ctor_dev
  Capture camDev = Capture_ctor_dev(NULL, 1280, 720, "/dev/video0", 60);
  CHECK(camDev.width == 1280, "camDev.width=%d want 1280", camDev.width);
  CHECK(camDev.height == 720, "camDev.height=%d want 720", camDev.height);
  CHECK(camDev.fps == 60, "camDev.fps=%d want 60", camDev.fps);
  CHECK(strcmp(camDev.deviceOrPath, "/dev/video0") == 0, "camDev path is /dev/video0");

  // Test Movie and GSPipeline reuse common video state
  Movie mov = Movie_ctor("sample.mp4");
  CHECK(mov.width == 320 && mov.height == 240, "Movie dimensions 320x240");
  CHECK(strcmp(mov.deviceOrPath, "sample.mp4") == 0, "Movie path matches");
  CHECK(_pde_video_duration(&mov) == 10.0f, "Movie duration is 10.0f");

  GSPipeline pipe = GSPipeline_ctor("autovideosrc");
  CHECK(pipe.width == 320 && pipe.height == 240, "GSPipeline dimensions 320x240");
  CHECK(_pde_video_available(&pipe) == true, "GSPipeline available");

  if (cam.data) free(cam.data);
  if (camDev.data) free(camDev.data);
  if (mov.data) free(mov.data);
  if (pipe.data) free(pipe.data);
}

static void test_minim(void) {
  // Test Minim initialization
  Minim minim = Minim_ctor(NULL);

  // Test AudioPlayer loading and lifecycles
  AudioPlayer player = minim.loadFile("nonexistent_test.wav");
  CHECK(player.bufferSize() == 1024, "player bufferSize is 1024");
  CHECK(player.sampleRate() == 44100.0f, "player sampleRate is 44100.0f");
  CHECK(AudioPlayer_isPlaying(&player) == false, "player initially not playing");

  AudioPlayer_play(&player);
  CHECK(AudioPlayer_isPlaying(&player) == true, "player playing after play()");

  AudioPlayer_pause(&player);
  CHECK(AudioPlayer_isPlaying(&player) == false, "player not playing after pause()");

  AudioPlayer_loop(&player);
  CHECK(AudioPlayer_isPlaying(&player) == true, "player playing after loop()");
  CHECK(player.isLoopingState == true, "player.isLoopingState true");

  AudioPlayer_rewind(&player);
  CHECK(AudioPlayer_position(&player) == 0, "player position is 0 after rewind()");
  CHECK(AudioPlayer_length(&player) >= 0, "player length is >= 0");

  AudioPlayer_close(&player);
  CHECK(AudioPlayer_isPlaying(&player) == false, "player not playing after close()");

  // Test AudioInput
  AudioInput input = minim.getLineIn();
  CHECK(input.mix.samples != NULL, "AudioInput mix buffer allocated");
  CHECK(input.bufferSize() == 1024, "AudioInput bufferSize 1024");
  AudioInput_close(&input);

  // Test AudioSample
  AudioSample sample = minim.loadSample("beep.wav");
  AudioSample_trigger(&sample);
  CHECK(sample.isPlayingState == true, "sample triggered");
  AudioSample_stop(&sample);
  CHECK(sample.isPlayingState == false, "sample stopped");

  // Test SineWave
  SineWave sw = SineWave_ctor(440.0f, 0.8f, 44100.0f);
  CHECK(sw.frequency == 440.0f, "SineWave freq 440.0f");
  CHECK(sw.amplitude == 0.8f, "SineWave amp 0.8f");

  // Test FFT analysis
  FFT fft = FFT_ctor(512, 44100.0f);
  CHECK(FFT_specSize(&fft) == 257, "FFT specSize 257 for timeSize 512 (want 257, got %d)", FFT_specSize(&fft));

  // Synthesize a 1000 Hz tone and forward through FFT
  float testBuf[512];
  float freq = 1000.0f;
  float srate = 44100.0f;
  for (int i = 0; i < 512; i++) {
    testBuf[i] = sinf(2.0f * (float)M_PI * freq * (float)i / srate);
  }
  FFT_forward(&fft, testBuf);

  // 1000 Hz with 512 window at 44100 Hz corresponds to bin index round(1000 * 512 / 44100) = ~12
  int targetBin = (int)roundf(freq * 512.0f / srate);
  float peakAmp = FFT_getBand(&fft, targetBin);
  CHECK(peakAmp > 0.5f, "FFT peak amplitude at target bin %d is %f (want > 0.5)", targetBin, peakAmp);

  FFT_close(&fft);

  // Stop Minim
  Minim_stop(&minim);
}

static void test_midi(void) {
  // Test MidiBus integer port constructor
  MidiBus bus = MidiBus_ctor(NULL, 0, 1);
  CHECK(bus.inDevice == 0, "bus.inDevice=%d want 0", bus.inDevice);
  CHECK(bus.outDevice == 1, "bus.outDevice=%d want 1", bus.outDevice);

  // Test sendNoteOn
  MidiBus_sendNoteOn(&bus, 1, 60, 100);
  CHECK(bus.lastChannel == 1, "bus.lastChannel=%d want 1", bus.lastChannel);
  CHECK(bus.lastPitch == 60, "bus.lastPitch=%d want 60", bus.lastPitch);
  CHECK(bus.lastVelocity == 100, "bus.lastVelocity=%d want 100", bus.lastVelocity);

  // Test sendNoteOff
  MidiBus_sendNoteOff(&bus, 1, 60, 0);
  CHECK(bus.lastVelocity == 0, "bus.lastVelocity=%d want 0", bus.lastVelocity);

  // Test sendControllerChange
  MidiBus_sendControllerChange(&bus, 1, 7, 127);
  CHECK(bus.lastControllerNum == 7, "bus.lastControllerNum=%d want 7", bus.lastControllerNum);
  CHECK(bus.lastControllerVal == 127, "bus.lastControllerVal=%d want 127", bus.lastControllerVal);

  // Test MidiBus string name constructor
  MidiBus busNamed = MidiBus_ctor(NULL, "MIDI Keyboard", "Synth Output");
  CHECK(strcmp(busNamed.inDeviceName, "MIDI Keyboard") == 0, "busNamed inDeviceName");
  CHECK(strcmp(busNamed.outDeviceName, "Synth Output") == 0, "busNamed outDeviceName");

  const char **midiDevs = MidiBus_list();
  CHECK(midiDevs != NULL, "MidiBus_list() returns non-null");

  MidiBus_close(&bus);
  MidiBus_close(&busNamed);
}

static void test_arduino(void) {
  // Verify standard Arduino constants
  CHECK(INPUT == 0, "INPUT == 0 (got %d)", INPUT);
  CHECK(OUTPUT == 1, "OUTPUT == 1 (got %d)", OUTPUT);
  CHECK(LOW == 0, "LOW == 0 (got %d)", LOW);
  CHECK(HIGH == 1, "HIGH == 1 (got %d)", HIGH);

  // Test Arduino constructor
  Arduino a = Arduino_ctor(NULL, "/dev/ttyUSB0", 57600);

  // Test pinMode
  Arduino_pinMode(&a, 13, OUTPUT);
  CHECK(a.pinModes[13] == OUTPUT, "pinModes[13] == OUTPUT");

  Arduino_pinMode(&a, 2, INPUT);
  CHECK(a.pinModes[2] == INPUT, "pinModes[2] == INPUT");

  // Test digitalWrite & digitalRead
  Arduino_digitalWrite(&a, 13, HIGH);
  CHECK(Arduino_digitalRead(&a, 13) == HIGH, "digitalRead(13) == HIGH");

  Arduino_digitalWrite(&a, 13, LOW);
  CHECK(Arduino_digitalRead(&a, 13) == LOW, "digitalRead(13) == LOW");

  // Test analogWrite & analogRead
  Arduino_analogWrite(&a, 9, 180);
  CHECK(Arduino_analogRead(&a, 9) == 180, "analogRead(9) == 180");

  // Test function pointers on Arduino struct
  a.digitalWrite(8, HIGH);
  CHECK(a.digitalRead(8) == HIGH, "a.digitalRead(8) == HIGH via function pointer");

  a.analogWrite(5, 255);
  CHECK(a.analogRead(5) == 255, "a.analogRead(5) == 255 via function pointer");
}

int main(void) {
  test_video();
  test_minim();
  test_midi();
  test_arduino();

  printf("%d checks, %d failures\n", checks, failures);
  return failures > 0 ? 1 : 0;
}
