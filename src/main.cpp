#include <Arduino.h>
#include <TFT_eSPI.h>
#include <format>

#include "nixie_small.h"

// Version
const char* VERSION = "0.1.0";

// Display configuration
struct DisplayConfiguration {
  uint32_t const WIDTH = 320;
  uint32_t const HEIGHT = 240;
} displayConfig;

struct StarConfiguration {
  uint32_t const STAR_COUNT = 500;
  float const Z_MAX = 400;
  float const Z_MIN = 1;
  float const SCALE = 100;
  float const SPEED = 0.05f;
 } starConfig;

struct Star {
  int32_t x, y;
  float z;
  int32_t prev_sx, prev_sy;
};

struct Configuration {
  bool showFPS = false;
} config;

// FPS tracking variables
uint32_t frameCount = 0;
uint32_t lastFpsTime = 0;
uint32_t currentFps = 0;

Star* stars = nullptr;

uint32_t prevMillis = 0;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);
TFT_eSprite nixieSprite = TFT_eSprite(&tft);
TFT_eSprite nixieSpriteTemplate = TFT_eSprite(&tft); 

// TODO: Get rid of this flag
bool bufferReady = false;

void resetStar(int index, bool init) {
  stars[index].x  = random(-displayConfig.WIDTH, displayConfig.WIDTH);
  stars[index].y  = random(-displayConfig.HEIGHT, displayConfig.HEIGHT);
  stars[index].z  = init ? random(50, starConfig.Z_MAX) : starConfig.Z_MAX;
  stars[index].prev_sx = 0;
  stars[index].prev_sy = 0;
}

void setupBuffer() {
  tft.printf("Provisioning 16-bit display buffer... ", displayConfig.WIDTH, displayConfig.HEIGHT);
  buffer.setColorDepth(16);
  bufferReady = (buffer.createSprite(displayConfig.WIDTH, displayConfig.HEIGHT) != nullptr);
  if(!bufferReady) {
    tft.printf("failed!\nProvisioning 8-bit display buffer... ");
    buffer.setColorDepth(8);
    bufferReady = (buffer.createSprite(displayConfig.WIDTH, displayConfig.HEIGHT) != nullptr);
  }
  if(!bufferReady) {
    tft.printf("failed!\n");
    exit(-1); // Epic fail! Time to die...
  }
  else {
    tft.printf("done!\n");
  }
}

void setupNixieSprite() {
  nixieSprite.setColorDepth(16);
  if(nixieSprite.createSprite(NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT) == nullptr) {
    tft.printf("Failed to create nixie sprite!\n");
    exit(-1); // Epic fail! Time to die...
  }
  else {
    // Set some default values
    nixieSprite.setTextColor(tft.color565(255, 120, 0)); // Nixie orange
    nixieSprite.setTextSize(1);
    nixieSprite.setTextDatum(MC_DATUM); // Middle center
    tft.printf("Nixie sprite created successfully!\n");
  }

  nixieSpriteTemplate.setColorDepth(16);
  if(nixieSpriteTemplate.createSprite(NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT) == nullptr) {
    tft.printf("Failed to create nixie sprite template!\n");
    exit(-1); // Epic fail! Time to die...
  }
  else {
    nixieSpriteTemplate.pushImage(0, 0, NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT, nixie_small);
    tft.printf("Nixie sprite template created successfully!\n");
  }
}

void setupStars() {
  tft.printf("Generating stars...");
  randomSeed(micros()); // Randomize the seed
  stars = new Star[starConfig.STAR_COUNT];
  for (int i = 0; i < starConfig.STAR_COUNT; ++i) {
    resetStar(i, true);
  }
}

void renderStars(TFT_eSprite* buffer, const float elapsedMillis) {
  for (int i = 0; i < starConfig.STAR_COUNT; ++i) {
    stars[i].z -= (starConfig.SPEED * elapsedMillis);

    // Reset star when it passes the camera
    if (stars[i].z <= starConfig.Z_MIN) {
      resetStar(i, false);
      continue; // Skip rendering this star on the pass. (terminate this loop iteration and move to the next one)
    }

    // Perspective calculations
    float scale = starConfig.SCALE / stars[i].z;
    int32_t sx = (displayConfig.WIDTH >> 1)  + (stars[i].x * scale);
    int32_t sy = (displayConfig.HEIGHT >> 1) + (stars[i].y * scale);

    // Only draw stars that are within screen boundaries
    if (sx >= 0 && sx <= displayConfig.WIDTH && sy >= 0 && sy <= displayConfig.HEIGHT) {
      // Closer star (lower Z) = brighter star
      uint8_t brightness = (uint8_t)((starConfig.Z_MAX - stars[i].z) * 255 / starConfig.Z_MAX);
      uint16_t colour = tft.color565(brightness, brightness, brightness);

      // Motion trail - a side effect is that we don't draw stars on the first pass after reset (as they haven't yet moved). I am OK with this.
      if (stars[i].prev_sx != 0 || stars[i].prev_sy != 0) {
        buffer->drawLine(sx, sy, stars[i].prev_sx, stars[i].prev_sy, colour);
      }

      // Capture the scaled x and y values.
      stars[i].prev_sx = sx;
      stars[i].prev_sy = sy;
    }
  }
}

void renderNixie(TFT_eSprite* buffer, uint16_t x, uint16_t y, const char* value) {
  const uint16_t font_number = 8; // Font 8 is a built in 16x32 pixel font that fits nicely within the nixie sprite.
  const uint16_t text_x_offset = NIXIE_SMALL_WIDTH >> 1;
  const uint16_t text_y_offset = 84;

  // -- Draw the digit to the sprite --
  nixieSprite.fillSprite(TFT_BLACK); // Clear the sprite before rendering the new digit
  nixieSprite.drawString(value,  text_x_offset, text_y_offset, font_number);

  // -- Draw the nixie image to the sprite --
  nixieSpriteTemplate.pushToSprite(&nixieSprite, 0, 0, TFT_BLACK); // Draw the nixie outline, using black as the transparent colour key

  // -- Push the nixie sprite to the buffer, using black as the transparent colour key --
  nixieSprite.pushToSprite(buffer, x, y, TFT_BLACK);  
}

// TODO: Refactor renderNixies to take a time struct and render the appropriate numbers.
void renderNixies(TFT_eSprite* buffer) {
  const uint16_t y_offset = 20;
  renderNixie(buffer, 0, y_offset, "1");
  renderNixie(buffer, 75, y_offset, "2");
  renderNixie(buffer, 170, y_offset, "3");
  renderNixie(buffer, 245, y_offset, "4");
}

void renderFPS(TFT_eSprite* buffer, uint32_t const fps) {
  buffer->setTextColor(TFT_WHITE);
  buffer->setTextSize(1);
  buffer->drawRightString(std::format("FPS: {}", fps).c_str(), 318, 0, 1);
}

void setup() {
  tft.init();
  tft.setRotation(1); // Landscape mode
  tft.fillScreen(TFT_BLACK);
  
  tft.setCursor(0, 10);
  tft.printf("Nixie Drift v%s by stevious is booting.\n\n", VERSION);
  
  setupBuffer();
  setupNixieSprite();
  setupStars();
}

void loop() {  
  frameCount++;
  uint32_t now = millis();
  float elapsedMillis = now - prevMillis;

  buffer.fillSprite(TFT_BLACK);
  renderStars(&buffer, elapsedMillis);  
  renderNixies(&buffer);

  // Update FPS every second
  if (now - lastFpsTime >= 1000) { // Update every 1 second
    currentFps = frameCount;
    frameCount = 0;
    lastFpsTime = now;
  }
  renderFPS(&buffer, currentFps);

  buffer.pushSprite(0, 0);
  prevMillis = now;
}
