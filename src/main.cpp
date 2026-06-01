#include <vector>
#include <iostream>
#include <tuple>
#include <string>
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "nixie_small.h"

// Version
const char* VERSION = "0.1.0";

// Touchscreen pins
#define TS_CS 33
#define TS_IRQ 36
#define TS_CLK 25
#define TS_DIN 32
#define TS_DOUT 39

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

struct ColonState {
  bool ascending = true;
  const float speed = 512.0f; // Speed of the animation
  float value = 0;
} colonState;

struct ScrollingMarquee {
  String text= "Nixie Drift - a retro-futuristic time keeping device by Stevious. Enjoy the cosmic journey through time and space!   ";
  float x;
  const float speed = 60.0f;
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

struct NixieState {
  char digits[4];
} nixieState;

Star* stars = nullptr;

uint32_t prevMillis = 0;
bool b0rked = false; // A flag to indicate if something went very wrong during setup (like failing to allocate the sprite buffer). If this is true, the loop will skip all rendering to avoid unexpected behavior.

XPT2046_Touchscreen ts( TS_CS, TS_IRQ );

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);
TFT_eSprite nixieSpriteTemplate = TFT_eSprite(&tft);
TFT_eSprite nixieSprites[4] = { TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft) };

// -- HELPER FUNCTIONS --

void drawGlowingString(TFT_eSprite* buffer, const String& text, int16_t x, int16_t y, uint16_t textColor, uint16_t glowColor, uint16_t glowStrength) {  
  // Be sure to set the appropriate font and text size on the buffer before calling this function.
  // Draw glow layers
  buffer->setTextColor(glowColor);
  for (uint16_t i = glowStrength; i > 0; --i) {
    buffer->drawString(text, x - i, y - i);
    buffer->drawString(text, x + i, y - i);
    buffer->drawString(text, x - i, y + i);
    buffer->drawString(text, x + i, y + i);
  }
  
  // Draw main text
  buffer->setTextColor(textColor);
  buffer->drawString(text, x, y);
}

// Split text into lines that respect word boundaries and fit within maxWidth pixels
std::vector<String> wrapTextToLines(const String& text, TFT_eSprite* buffer, uint16_t maxWidth) {
  std::vector<String> lines;
  String remaining = text;
  
  while (remaining.length() > 0) {
    // Try to fit as many characters as possible without breaking words
    String line;
    int spacePos = -1;
    
    for (int i = 0; i < remaining.length(); ++i) {
      String testLine = remaining.substring(0, i + 1);
      int width = buffer->textWidth(testLine.c_str());
      
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

void resetStar(uint16_t index, bool init) {
  stars[index].x  = random(-displayConfig.WIDTH, displayConfig.WIDTH);
  stars[index].y  = random(-displayConfig.HEIGHT, displayConfig.HEIGHT);
  stars[index].z  = init ? random(50, starConfig.Z_MAX) : starConfig.Z_MAX;
  stars[index].prev_sx = 0;
  stars[index].prev_sy = 0;
}

std::tuple<bool, String> setupBuffer() {
  bool result = true;
  String message = "";

  buffer.setColorDepth(8);
  if(buffer.createSprite(displayConfig.WIDTH, displayConfig.HEIGHT) == nullptr) {        
    result = false;
    b0rked = true;
  } 
  return {result, message};
}

std::tuple<bool, String> setupNixieSpriteTemplate() {
  bool result = true;
  String message = "";
  
  nixieSpriteTemplate.setColorDepth(16);
  if(nixieSpriteTemplate.createSprite(NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT) == nullptr) {    
    result = false;
    b0rked = true;    
  }
  else {
    nixieSpriteTemplate.pushImage(0, 0, NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT, nixie_small);    
  }
  return {result, message};
}

std::tuple<bool, String> setupNixieSprites() {
  bool result = true;
  String message = "";

  for(int i = 0; i < 4; ++i) {
    nixieSprites[i].setColorDepth(16);    
    if(nixieSprites[i].createSprite(NIXIE_SMALL_WIDTH, NIXIE_SMALL_HEIGHT) == nullptr) {
      result = false;
      b0rked = true;
      message = "(nixie: " + String(i) + ")"; // Indicate which sprite failed to create in the message      
      break; // No need to attempt to create further sprites if one has already failed, as we are in a b0rked state at this point.
    }
  }
  return {result, message};
}

std::tuple<bool, String> setupStars() {
  bool result = true;
  String message = "";
  
  randomSeed(micros()); // Randomize the seed
  stars = new Star[starConfig.STAR_COUNT];
  if (stars != nullptr) {
    for (int i = 0; i < starConfig.STAR_COUNT; ++i) {
      resetStar(i, true);
    }
  } else {
    result = false;
    b0rked = true;
  }
  return {result, message};
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
      uint8_t brightness = static_cast<uint8_t>((starConfig.Z_MAX - stars[i].z) * 255 / starConfig.Z_MAX);
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

void renderNixie(TFT_eSprite* buffer, uint16_t nixie_number, uint16_t x, uint16_t y) {
  // Drawing a sprite with transparency is expensive, so we will cheat and draw each nixie with a black background. Hopefully the lack of transparency won't be too noticeable.
  // This gives us quite a significant boost in framerate, so I can use those free clock cycles for other things :)
  nixieSprites[nixie_number].pushToSprite(buffer, x, y);
}

void updateNixie(TFT_eSprite* buffer, const char digit) {
  buffer->fillSprite(TFT_BLACK);

  buffer->setTextFont(8); // Font 8 is a built in 16x32 pixel font that fits nicely within the nixie sprite.
  buffer->setTextSize(1);

  String digitStr(digit);

  const uint16_t text_x_offset = (NIXIE_SMALL_WIDTH - buffer->textWidth(digitStr)) / 2; // Center the text within the sprite
  const uint16_t text_y_offset = 44; // Manually measured this from nixie_small.png

  // Write the text into the sprite
  drawGlowingString(buffer, digitStr, text_x_offset, text_y_offset, tft.color565(255, 120, 0), tft.color565(128, 0, 0), 5);
  
  // -- Draw the nixie image to the sprite --
  nixieSpriteTemplate.pushToSprite(buffer, 0, 0, TFT_BLACK); // Draw the nixie outline, using black as the transparent colour key  
}

void updateNixies(const char digits[4], bool force_update = false) {
  // Only render nixies that have changed digit values.
  for(uint16_t i = 0; i < 4; ++i) {
    if (force_update || (digits[i] != nixieState.digits[i])) {
      updateNixie(&nixieSprites[i], digits[i]);
      nixieState.digits[i] = digits[i];      
    }    
  }         
}

void renderNixies(TFT_eSprite* buffer) {
  const uint16_t y_offset = 20;
  renderNixie(buffer, 0, 0, y_offset);
  renderNixie(buffer, 1, 75, y_offset);
  renderNixie(buffer, 2, 170, y_offset);
  renderNixie(buffer, 3, 245, y_offset);
}

// Nothing fancy, just a simple FPS counter in the top right corner. It will update once per second.
void renderFPS(TFT_eSprite* buffer, uint32_t const fps) {
  buffer->setTextColor(TFT_WHITE);
  buffer->setTextFont(1);
  buffer->setTextSize(1);

  char sbuffer[16];
  snprintf(sbuffer, sizeof(sbuffer), "FPS: %lu", fps);

  buffer->drawRightString(sbuffer, 318, 0, 1);
}

// I didn't end up using this function, but I will keep it here for the LOLs.
void renderScrollingMarquee(TFT_eSprite* buffer, const float elapsedMillis) {
  buffer->setTextSize(1);
  buffer->setTextFont(1);

  int16_t textWidth = buffer->textWidth(scrollingMarquee.text.c_str());
  scrollingMarquee.x -= scrollingMarquee.speed * (elapsedMillis / 1000.0f);

  if (scrollingMarquee.x + textWidth < 0) {
    scrollingMarquee.x = displayConfig.WIDTH;
  }

  int16_t y = displayConfig.HEIGHT - buffer->fontHeight() - scrollingMarquee.bottomMargin;
  
  drawGlowingString(buffer, scrollingMarquee.text, static_cast<int16_t>(scrollingMarquee.x), y, tft.color565(255, 100, 0), tft.color565(255, 50, 0), 0);
}

void renderTerminalMarquee(TFT_eSprite* buffer, const uint32_t now) {
  // Set the text defaults
  buffer->setFreeFont(&FreeMono9pt7b); // Adafruit GFXFF font. Change if RAM gets tight.
  buffer->setTextSize(1);
  
  // Static cache for wrapped lines (recalculate only when text changes)
  static std::vector<String> wrappedLines;
  static String lastText = "";
  
  const uint16_t terminalTextColor = tft.color565(0, 200, 0);
  const uint16_t terminalTextGlowColor = tft.color565(0, 100, 0);
  const uint16_t displayWidth = 310;  // Leave some margin (need to park the cursor somewhere!)

  const uint16_t terminalY = displayConfig.HEIGHT - tft.fontHeight() - 24;
  
    // Dynamically calculate cursor size based on font metrics.
  const uint16_t blockCursorWidth = buffer->textWidth("M"); // Mmmmmm, mmmmm m-dash! <.<
  const uint16_t blockCursorHeight = buffer->fontHeight();

  // Recalculate line wrapping if text changed
  if (terminalMarquee.fullText != lastText) {
    wrappedLines = wrapTextToLines(terminalMarquee.fullText, buffer, displayWidth);
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
      drawGlowingString(buffer, terminalMarquee.currentLine, 5, terminalY, terminalTextColor, terminalTextGlowColor, 1);      

      // -- Draw blinking cursor (note to self: duplication of this below for end-of-line situation) --
      if (terminalMarquee.cursorVisible) {
        uint16_t cursorX = 5 + buffer->textWidth(terminalMarquee.currentLine.c_str());
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
  drawGlowingString(buffer, visibleText, 5, terminalY, terminalTextColor, terminalTextGlowColor, 1);
  
  // -- Draw blinking cursor --
  /* 
    Did we just duplicate the text and cursor drawing code in the line delay section? Yes, we did. But having it here again allows us to do something different 
    at the end of a line (e.g. make the cursor solid instead of blinking, or add a little "line complete" animation).
  */
  if (terminalMarquee.cursorVisible && terminalMarquee.charIndex <= terminalMarquee.currentLine.length()) {
    uint16_t cursorX = 5 + buffer->textWidth(visibleText.c_str());
    buffer->fillRect(cursorX, terminalY, blockCursorWidth, blockCursorHeight, tft.color565(0, 200, 0));
  }
}

void renderColon(TFT_eSprite* buffer, float elapsedMillis) {
  // Animate the colon by making it pulse up and down in size
  if (colonState.ascending) {
    colonState.value += colonState.speed * (elapsedMillis / 1000.0f);
    if (colonState.value >= 255.0f) {
      colonState.value = 255.0f;
      colonState.ascending = false;
    }
  } else {
    colonState.value -= colonState.speed * (elapsedMillis / 1000.0f);
    if (colonState.value <= 0.0f) {
      colonState.value = 0.0f;
      colonState.ascending = true;
    }
  }
  
  // The y values for the colon dots etc.
  const uint16_t y1 = 90;
  const uint16_t y2 = 110;

  // Draw a couple of horizontal lines between the middle nixies to hold the colon dots. They just can't be left floating there without some kind of support, can they?
  buffer->drawFastHLine(145, y1 - 1, 30, tft.color565(210, 136, 3));
  buffer->drawFastHLine(145, y1, 30, tft.color565(210, 136, 3));
  buffer->drawFastHLine(145, y1 + 1, 30, tft.color565(210, 136, 3));
  
  buffer->drawFastHLine(145, y2 - 1, 30, tft.color565(210, 136, 3));
  buffer->drawFastHLine(145, y2, 30, tft.color565(210, 136, 3));
  buffer->drawFastHLine(145, y2 + 1, 30, tft.color565(210, 136, 3));
  
  uint8_t glow = static_cast<uint8_t>(colonState.value);

  // Draw the two dots of the colon
  buffer->drawEllipse(160, y1, 6, 6, tft.color565(210, 136, 3));
  buffer->drawEllipse(160, y1, 7, 7, tft.color565(210, 136, 3));
  buffer->drawEllipse(160, y1, 8, 8, tft.color565(210, 136, 3));

  buffer->drawEllipse(160, y2, 6, 6, tft.color565(210, 136, 3));
  buffer->drawEllipse(160, y2, 7, 7, tft.color565(210, 136, 3));
  buffer->drawEllipse(160, y2, 8, 8, tft.color565(210, 136, 3));

  buffer->fillEllipse(160, y1, 5, 5, tft.color565(glow, glow, glow));
  buffer->fillEllipse(160, y2, 5, 5, tft.color565(glow, glow, glow));
}

void log(String message, uint16_t textFont = 1, uint16_t textSize = 1, uint16_t textColor = TFT_WHITE, uint16_t margin = 10) {
  tft.setTextFont(textFont);
  tft.setTextColor(textColor);
  tft.setTextSize(textSize);
  tft.setCursor(margin, tft.getCursorY());

  tft.println(message); 
}

void status(bool status, String message, uint16_t margin = 10) {
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setCursor(margin, tft.getCursorY());

  tft.setTextColor(TFT_WHITE);
  tft.print("[");
  tft.setTextColor(status ? TFT_GREEN : TFT_RED);
  tft.print(status ? "OK" : "FAIL");
  tft.setTextColor(TFT_WHITE);
  tft.println("] - " + message);  
}

// -- Arduino setup and loop --
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1); // Landscape mode  
  tft.fillScreen(TFT_BLACK);
 
  SPI.begin(TS_CLK, TS_DOUT, TS_DIN);
  ts.begin();
  ts.setRotation( 1 );

  log("Nixie Drift v" + String(VERSION), 2, 2, TFT_GREEN, 10);
  log("by Stevious (www.localgoat.xyz)", 2, 1, TFT_GREEN, 10);
  log("");
  log("ESP-IDF Version: " + String(esp_get_idf_version()), 1, 1, TFT_WHITE, 10);
  
  log("");

  if(!b0rked) {
    auto [result, message] = setupBuffer();
    status(result, "Provision display buffer. " + message);
  }

  if(!b0rked) {
    auto [result, message] = setupNixieSpriteTemplate();
    status(result, "Provision nixie sprite template. " + message);
  }

  if(!b0rked) {
    auto [result, message] = setupNixieSprites();
    status(result, "Provision nixie sprites. " + message);
  }
 
  if(!b0rked) {
    auto [result, message] = setupStars();
    status(result, "Provision and reset stars. " + message);
  }  

  if(!b0rked) {
    updateNixies((const char[]){'0', '0', '0', '0'}, true); // Force update all nixies to the initial '0' state
    status(true, "Render initial nixie digits.");
  }
  scrollingMarquee.x = displayConfig.WIDTH; // Start marquee off-screen to the right
  
  // Initialize terminal marquee with example text
  initializeTerminalMarquee(" Welcome to Nixie Drift - a retro-futuristic time keeping device by Stevious. Enjoy the cosmic journey through time and space!");

  prevMillis = millis(); // Initialize prevMillis after setup is complete to avoid a huge elapsed time on the first loop iteration. 
}

void loop() {  
  if(b0rked) {
    return; // If something went very wrong during setup, skip all rendering to avoid unexpected behavior.
  }

  ++fpsTracking.frameCount;
  uint32_t now = millis();
  float elapsedMillis = now - prevMillis; // Time delta in milliseconds since last frame

  // -- Render everything to the buffer sprite --
  buffer.fillSprite(TFT_BLACK);
  renderStars(&buffer, elapsedMillis);  
  renderNixies(&buffer);
  renderColon(&buffer, elapsedMillis); // Sounds rather painful :(
  renderTerminalMarquee(&buffer, now);
  renderScrollingMarquee(&buffer, elapsedMillis);

  // Update FPS every second
  if (config.showFPS && (now - fpsTracking.lastFpsTime >= 1000)) { // Update every 1 second
    fpsTracking.currentFPS = fpsTracking.frameCount;
    fpsTracking.frameCount = 0;
    fpsTracking.lastFpsTime = now;
  }
  if (config.showFPS) {
    renderFPS(&buffer, fpsTracking.currentFPS);
  }

  // -- Push the buffer to the display --
  buffer.pushSprite(0, 0);

  if(ts.tirqTouched() && ts.touched()) {
    Serial.println("Touched at: " + String(ts.getPoint().x) + ", " + String(ts.getPoint().y));
  }

  prevMillis = now;
}
