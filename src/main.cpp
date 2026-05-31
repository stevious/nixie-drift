#include <Arduino.h>
#include <TFT_eSPI.h>
#include <format>
#include <vector>

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

struct ScrollingMarquee {
  String text= "Nixie Drift - a retro-futuristic time keeping device by Stevious. Enjoy the cosmic journey through time and space!   ";
  float x;
  const float speed = 60.0f;
  const uint16_t font = 2;
  const uint16_t bottomMargin = 6;
} scrollingMarquee;

// Terminal-style marquee configuration
struct TerminalMarqueeState {
  String fullText;              // Full text to display
  String currentLine;           // Current line being displayed
  uint32_t lineIndex = 0;       // Current line index (for word-wrapped lines)
  uint32_t charIndex = 0;       // Current character in current line
  uint32_t lastCharTime = 0;    // Last time a character was added
  uint32_t lastBlinkTime = 0;   // Last time cursor blink was toggled
  bool cursorVisible = true;    // Whether cursor is visible
  uint32_t charDelay = 50;      // Milliseconds per character
  uint32_t lineDelay = 1500;    // Milliseconds to pause before next line
  uint32_t blinkPeriod = 500;   // Cursor blink period in ms
  bool waitingForLineDelay = false; // Currently waiting before showing next line
} terminalMarquee;

Star* stars = nullptr;

uint32_t prevMillis = 0;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);
TFT_eSprite nixieSprite = TFT_eSprite(&tft);

// -- HELPER FUNCTIONS --

void drawGlowingString(TFT_eSprite* buffer, const String& text, int16_t x, int16_t y, uint16_t font, uint16_t textColor, uint16_t glowColor, uint16_t glowStrength) {  
  buffer->setTextColor(glowColor);
  
  // Draw glow layers
  for (int i = glowStrength; i > 0; --i) {
    buffer->drawString(text, x - i, y - i, font);
    buffer->drawString(text, x + i, y - i, font);
    buffer->drawString(text, x - i, y + i, font);
    buffer->drawString(text, x + i, y + i, font);
  }
  
  // Draw main text
  buffer->setTextColor(textColor);
  buffer->drawString(text, x, y, font);
}

// Split text into lines that respect word boundaries and fit within maxWidth
std::vector<String> wrapTextToLines(const String& text, TFT_eSprite* buffer, uint16_t maxWidth, uint16_t font) {
  std::vector<String> lines;
  String remaining = text;
  
  while (remaining.length() > 0) {
    // Try to fit as many characters as possible without breaking words
    String line;
    int spacePos = -1;
    
    for (int i = 0; i < remaining.length(); ++i) {
      String testLine = remaining.substring(0, i + 1);
      int width = buffer->textWidth(testLine.c_str(), font);
      
      if (width > maxWidth) {
        // Exceeded width, use previous valid line
        if (spacePos != -1) {
          // We have a space, break there
          line = remaining.substring(0, spacePos);
          remaining = remaining.substring(spacePos + 1); // Skip the space
        } else {
          // No space found, break at current position
          line = remaining.substring(0, i);
          remaining = remaining.substring(i);
        }
        break;
      }
      
      if (remaining[i] == ' ') {
        spacePos = i;
      }
      
      if (i == remaining.length() - 1) {
        // Reached end of text
        line = remaining;
        remaining = "";
      }
    }
    
    if (line.length() > 0) {
      lines.push_back(line);
    } else if (remaining.length() > 0) {
      // Fallback for very long words
      lines.push_back(remaining.substring(0, 1));
      remaining = remaining.substring(1);
    }
  }
  
  return lines;
}

// -- SETUP FUNCTIONS --
void initializeTerminalMarquee(const String& text) {
  terminalMarquee.fullText = text;
  terminalMarquee.charIndex = 0;
  terminalMarquee.lineIndex = 0;
  terminalMarquee.lastCharTime = 0;
  terminalMarquee.lastBlinkTime = 0;
  terminalMarquee.cursorVisible = true;
  terminalMarquee.waitingForLineDelay = false;
}

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
  } else {
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

// -- LOOP FUNCTIONS --

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

// Nothing fancy, just a simple FPS counter in the top right corner. It will update once per second.
void renderFPS(TFT_eSprite* buffer, uint32_t const fps) {
  buffer->setTextColor(TFT_WHITE);
  buffer->setTextSize(1);
  buffer->drawRightString(std::format("FPS: {}", fps).c_str(), 318, 0, 1);
}

// I didn't end up using this function, but I will keep it here for the LOLs.
void renderScrollingMarquee(TFT_eSprite* buffer, const float elapsedMillis) {
  buffer->setTextSize(3);
  buffer->setTextDatum(TL_DATUM);

  int16_t textWidth = buffer->textWidth(scrollingMarquee.text.c_str(), scrollingMarquee.font);
  scrollingMarquee.x -= scrollingMarquee.speed * (elapsedMillis / 1000.0f);

  if (scrollingMarquee.x + textWidth < 0) {
    scrollingMarquee.x = displayConfig.WIDTH;
  }

  int16_t y = displayConfig.HEIGHT - tft.fontHeight(scrollingMarquee.font) * 3 - scrollingMarquee.bottomMargin;
  
  buffer->setTextColor(tft.color565(255, 100, 0));
  drawGlowingString(buffer, scrollingMarquee.text, static_cast<int16_t>(scrollingMarquee.x), y, scrollingMarquee.font, tft.color565(255, 100, 0), tft.color565(255, 50, 0), 3);
}

void renderTerminalMarquee(TFT_eSprite* buffer, const uint32_t now) {
  // Static cache for wrapped lines (recalculate only when text changes)
  static std::vector<String> wrappedLines;
  static String lastText = "";
  
  const uint16_t terminalFont = 1;    // Is this the font I want?
  const uint16_t terminalFontSize = 2;
  const uint16_t terminalTextColor = tft.color565(0, 200, 0);
  const uint16_t terminalTextGlowColor = tft.color565(0, 100, 0);
  const uint16_t displayWidth = 310;  // Leave some margin (need to park the cursor somewhere!)

  // Let's set some default text properties for the terminal marquee
  buffer->setTextDatum(TL_DATUM);
  buffer->setTextSize(terminalFontSize);

  const uint16_t terminalY = displayConfig.HEIGHT - tft.fontHeight(terminalFont) - 8;
  const uint16_t charWidth = buffer->textWidth("M", terminalFont); // Mmmmmm, m-dash! <.<

    // Dynamically calculate cursor size based on font metrics.
  const uint16_t blockCursorWidth = charWidth;
  const uint16_t blockCursorHeight = tft.fontHeight(terminalFont) * terminalFontSize;

  // Recalculate line wrapping if text changed
  if (terminalMarquee.fullText != lastText) {
    wrappedLines = wrapTextToLines(terminalMarquee.fullText, buffer, displayWidth, terminalFont);
    lastText = terminalMarquee.fullText;
    terminalMarquee.charIndex = 0;
    terminalMarquee.lineIndex = 0;
    terminalMarquee.lastCharTime = now;
    terminalMarquee.waitingForLineDelay = false;
  }
  
  // Check if we're done with all lines
  if (terminalMarquee.lineIndex >= wrappedLines.size()) {
    return;
  }
  
  terminalMarquee.currentLine = wrappedLines[terminalMarquee.lineIndex];
  
  // Update cursor blink
  if (now - terminalMarquee.lastBlinkTime >= terminalMarquee.blinkPeriod) {
    terminalMarquee.cursorVisible = !terminalMarquee.cursorVisible;
    terminalMarquee.lastBlinkTime = now;
  }
  
  // -- Handle line delay (pause between lines) --
  if (terminalMarquee.waitingForLineDelay) {
    if (now - terminalMarquee.lastCharTime >= terminalMarquee.lineDelay) {
      // Move to next line
      ++terminalMarquee.lineIndex;
      terminalMarquee.charIndex = 0;
      terminalMarquee.lastCharTime = now;
      terminalMarquee.waitingForLineDelay = false;
      if (terminalMarquee.lineIndex >= wrappedLines.size()) {
        return;
      }
      terminalMarquee.currentLine = wrappedLines[terminalMarquee.lineIndex];
    } else {
      // Still waiting, draw complete line      
      drawGlowingString(buffer, terminalMarquee.currentLine, 5, terminalY, terminalFont, terminalTextColor, terminalTextGlowColor, 3);      

      // -- Draw blinking cursor (note the duplication of this below) --
      if (terminalMarquee.cursorVisible) {
        uint16_t cursorX = 5 + buffer->textWidth(terminalMarquee.currentLine.c_str(), terminalFont);
        buffer->fillRect(cursorX, terminalY, blockCursorWidth, blockCursorHeight, tft.color565(0, 200, 0));
      }
      return;
    }
  }
  
  // --- Add characters one at a time ---
  // Let's check to see if it is time to draw the next character (based on charDelay)
  if (now - terminalMarquee.lastCharTime >= terminalMarquee.charDelay) {  
    // Yes, we want to draw the next character. Increment charIndex to include the next character in the visible text.
    if (terminalMarquee.charIndex < terminalMarquee.currentLine.length()) {
      ++terminalMarquee.charIndex;
      terminalMarquee.lastCharTime = now;
    } else {
      // Line complete, start line delay
      terminalMarquee.waitingForLineDelay = true;
      terminalMarquee.lastCharTime = now;
    }
  }
  
  // -- Draw the visible portion of the current line --
  String visibleText = terminalMarquee.currentLine.substring(0, terminalMarquee.charIndex);
  drawGlowingString(buffer, visibleText, 5, terminalY, terminalFont, terminalTextColor, terminalTextGlowColor, 3);
  
  // -- Draw blinking cursor --
  /* 
    Did we just duplicate the text and cursor drawing code in the line delay section? Yes, we did. But having it here again allows us to do something different 
    at the end of a line (e.g. make the cursor solid instead of blinking, or add a little "line complete" animation).
  */
  if (terminalMarquee.cursorVisible && terminalMarquee.charIndex <= terminalMarquee.currentLine.length()) {
    uint16_t cursorX = 5 + buffer->textWidth(visibleText.c_str(), terminalFont);
    buffer->fillRect(cursorX, terminalY, blockCursorWidth, blockCursorHeight, tft.color565(0, 200, 0));
  }
}

// -- Arduino setup and loop --
void setup() {
  tft.init();
  tft.setRotation(1); // Landscape mode
  tft.dmaWait();     // Ensure any DMA operations complete before filling screen
  tft.fillScreen(TFT_BLACK);
  
  tft.setCursor(0, 10);
  tft.printf("Nixie Drift v%s by Stevious is booting.\n\n", VERSION);
  
  setupBuffer();
  setupNixieSprite();
  setupStars();

  scrollingMarquee.x = displayConfig.WIDTH; // Start marquee off-screen to the right
  
  // Initialize terminal marquee with example text
  initializeTerminalMarquee(" Welcome to Nixie Drift - a retro-futuristic time keeping device by Stevious. Enjoy the cosmic journey through time and space!");
}

void loop() {  
  ++fpsTracking.frameCount;
  uint32_t now = millis();
  float elapsedMillis = now - prevMillis;

  buffer.fillSprite(TFT_BLACK);
  renderStars(&buffer, elapsedMillis);  
  renderNixies(&buffer);
  renderTerminalMarquee(&buffer, now);
  // renderMarquee(&buffer, elapsedMillis);

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
  tft.dmaWait(); // Wait for DMA to complete before next frame
  prevMillis = now;
}
