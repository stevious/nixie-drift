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
  bool showFPS = true;
} config;

struct FPSTracking {
  uint32_t frameCount = 0;
  uint32_t lastFpsTime = 0;
  uint32_t currentFPS = 0;
} fpsTracking;

Star* stars = nullptr;

uint32_t prevMillis = 0;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);
TFT_eSprite nixieSprite = TFT_eSprite(&tft);

String marqueeText = "Nixie Drift - a retro-futuristic time keeping device by stevious. Enjoy the cosmic journey through time and space!   ";
float marqueeX = 0.0f;
const float marqueeSpeed = 60.0f; // pixels per second
const uint16_t marqueeFont = 2;
const uint16_t marqueeBottomMargin = 6;

void resetStar(int index, bool init) {
  stars[index].x  = random(-displayConfig.WIDTH, displayConfig.WIDTH);
  stars[index].y  = random(-displayConfig.HEIGHT, displayConfig.HEIGHT);
  stars[index].z  = init ? random(50, starConfig.Z_MAX) : starConfig.Z_MAX;
  stars[index].prev_sx = 0;
  stars[index].prev_sy = 0;
}

void setupBuffer() {
  tft.printf("Provisioning 8-bit display buffer... ");
  buffer.setColorDepth(8);
  if(buffer.createSprite(displayConfig.WIDTH, displayConfig.HEIGHT) == nullptr) {
    tft.printf("failed!\n");
    exit(-1); // Epic fail! Time to die...
  }
  else {
    tft.printf("success!\n");   
  }
}

void setupNixieSprite() {
  nixieSprite.setColorDepth(16);
  if(nixieSprite.createSprite(NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT) == nullptr) {
    tft.printf("Failed to create nixie sprite template!\n");
    exit(-1); // Epic fail! Time to die...
  }
  else {
    nixieSprite.pushImage(0, 0, NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT, nixie_small);
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

  buffer->setTextColor(tft.color565(255, 120, 0)); // Nixie orange
  buffer->setTextSize(1);
  buffer->setTextDatum(MC_DATUM); // Middle center
  
  buffer->drawString(value, x + text_x_offset, y+ text_y_offset, font_number);

  // -- Draw the nixie image to the sprite --
  nixieSprite.pushToSprite(buffer, x, y, TFT_BLACK); // Draw the nixie outline, using black as the transparent colour key
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

void renderMarquee(TFT_eSprite* buffer, const float elapsedMillis) {
  buffer->setTextSize(3);
  buffer->setTextDatum(TL_DATUM);

  int16_t textWidth = buffer->textWidth(marqueeText.c_str(), marqueeFont);
  marqueeX -= marqueeSpeed * (elapsedMillis / 1000.0f);

  if (marqueeX + textWidth < 0) {
    marqueeX = displayConfig.WIDTH;
  }

  int16_t y = displayConfig.HEIGHT - tft.fontHeight(marqueeFont) * 3 - marqueeBottomMargin;
  
  buffer->setTextColor(tft.color565(255, 100, 0));
  buffer->drawString(marqueeText, static_cast<int16_t>(marqueeX - 3), y, marqueeFont);
  buffer->drawString(marqueeText, static_cast<int16_t>(marqueeX + 3), y, marqueeFont);
  buffer->drawString(marqueeText, static_cast<int16_t>(marqueeX), y - 3, marqueeFont);
  buffer->drawString(marqueeText, static_cast<int16_t>(marqueeX), y + 3, marqueeFont);

  buffer->setTextColor(TFT_WHITE);
  buffer->drawString(marqueeText, static_cast<int16_t>(marqueeX), y, marqueeFont);
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

  marqueeX = displayConfig.WIDTH; // Start marquee off-screen to the right
}

void loop() {  
  fpsTracking.frameCount++;
  uint32_t now = millis();
  float elapsedMillis = now - prevMillis;

  buffer.fillSprite(TFT_BLACK);
  renderStars(&buffer, elapsedMillis);  
  renderNixies(&buffer);
  renderMarquee(&buffer, elapsedMillis);

  // Update FPS every second
  if (config.showFPS && (now - fpsTracking.lastFpsTime >= 1000)) { // Update every 1 second
    fpsTracking.currentFPS = fpsTracking.frameCount;
    fpsTracking.frameCount = 0;
    fpsTracking.lastFpsTime = now;
  }
  if (config.showFPS) {
    renderFPS(&buffer, fpsTracking.currentFPS);
  }

  buffer.pushSprite(0, 0);
  prevMillis = now;
}
