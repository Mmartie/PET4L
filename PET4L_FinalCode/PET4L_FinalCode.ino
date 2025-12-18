/**
 * PET4L - Productivity Emotional Tool for Learning
 * * Description:
 * This software controls a robotic flower designed to assist with the Pomodoro technique.
 * It uses an Arduino Microcontroller to manage:
 * 1. Ultrasonic sensing with noise filtering.
 * 2. Servo motor actuation with smooth movement and anti-jitter.
 * 3. RGB LED feedback for status and animations.
 * 4. Context-aware behavior (Happy/Angry dances, Withering mode).
 */

#include <VarSpeedServo.h>     // Library for controlling servo speed
#include <Adafruit_NeoPixel.h> // Library for controlling the LED Ring

// ================= HARDWARE CONFIGURATION =================
// Pin definitions mapping the physical connections to the Arduino board
#define TRIG_PIN       9   // Ultrasonic Sensor Trigger
#define ECHO_PIN       10  // Ultrasonic Sensor Echo
#define SERVO_PIN      3   // PWM Pin for Servo Motor
#define LED_PIN        6   // Data Pin for NeoPixel Ring

#define NUM_LEDS       20  // Number of LEDs in the ring
#define MAX_BRIGHTNESS 190 // brightness limit (0-255) to manage power consumption

// ================= LOGIC PARAMETERS =================
// Distance thresholds for user detection
const int THRESHOLD_CM     = 60; // Base distance to detect user (cm)
const int HYSTERESIS_CM    = 10; // Buffer to prevent flickering at the edge of the range

// Counters for signal stability (Debouncing)
const int REQUIRED_PRESENT = 3; // Consecutive readings needed to confirm PRESENCE
const int REQUIRED_ABSENT  = 5; // Consecutive readings needed to confirm ABSENCE

// Servo Direction Configuration
// Adjust these based on the mechanical assembly of the petals
const int OPEN_POS         = 90; // Angle for Open state (Right)
const int CLOSED_POS       = 0;  // Angle for Closed state (Left)

// --- TIMING CONFIGURATION (POMODORO) ---
const unsigned long ABSENT_DELAY      = 5000; // Time (ms) to wait before triggering "Withering"
const unsigned long WORK_DURATION     = 25UL * 60UL * 1000UL; // 25 Minutes Work Cycle
const unsigned long PAUSE_DURATION    = 5UL * 60UL * 1000UL;  // 5 Minutes Break Cycle
// ---------------------------------------

const unsigned long PATTERN_SPEED     = 80;  // Speed of the LED animation (ms per step)
const unsigned long SENSOR_INTERVAL   = 100; // How often to poll the sensor (ms)

// ================= GLOBAL OBJECTS =================
VarSpeedServo myServo; // Servo object
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800); // LED Strip object

// ================= SYSTEM STATE VARIABLES =================
int lastState = 1;          // 0 = User Present, 1 = User Absent (starts as Absent)
int targetPos = CLOSED_POS; // The target angle the servo should reach

// Sensor Logic Variables
int presentCounter = 0;     // Counts consecutive "Present" readings
int absentCounter  = 0;     // Counts consecutive "Absent" readings
unsigned long lastSensorRead = 0; // Timestamp of the last sensor poll

// Timer Logic Variables
unsigned long pomodoroStart   = 0;     // Timestamp when the current session started
unsigned long pomodoroElapsed = 0;     // Time accumulated in the current session
bool inPause                  = false; // Flag: are we in the break period?
unsigned long pauseStart      = 0;     // Timestamp when the break started
int pauseLedLevel             = MAX_BRIGHTNESS; // For fading effect during pause
bool pauseAngryDanceDone      = false; // Flag to run the "Late" animation only once

// Level/Gamification Logic
int currentLevel = 0;
const int NUM_LEVELS = 6;
// Array of colors for each level (R, G, B)
const uint8_t LEVEL_COLORS[NUM_LEVELS][3] = {
  {255, 255, 255}, // Level 0: White
  {255, 255, 0},   // Level 1: Yellow
  {0, 255, 0},     // Level 2: Green
  {0, 255, 255},   // Level 3: Cyan
  {0, 0, 255},     // Level 4: Blue
  {255, 0, 255}    // Level 5: Purple
};

// "Withering" Animation State
bool dramaticClosingActive    = false; // Is the robot currently "dying"?
bool waitingForDramatic       = false; // Are we in the 5-second limbo before dying?
unsigned long absentDetectedTime = 0;  // When did the user leave?
int dramPos                   = CLOSED_POS; // Current position during dramatic close

// LED Pattern State
bool patternActive            = false;
unsigned long lastPatternStep = 0;
int patternIndex              = 0;
unsigned long startPatternAfter = 0; // Delay before starting lights after opening

// Function Prototypes (Declarations)
void startupGreeting();
void happyDance();
void angryDance();
long readDistanceStable();
void fadeLedsIn();
bool checkInterrupt();

// ================= SETUP ROUTINE =================
// Runs once when the Arduino is powered on
void setup() {
  Serial.begin(9600);         // Initialize serial communication for debugging
  randomSeed(analogRead(0));  // Seed random number generator (for happy dance colors)

  // Configure Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize Servo
  myServo.attach(SERVO_PIN);
  myServo.write(OPEN_POS);    // Start in Open position

  // Initialize LEDs
  strip.begin();
  strip.setBrightness(MAX_BRIGHTNESS);
  strip.show();

  Serial.println(F("--- FLOWER BOT INITIALIZING ---"));
  
  // Run the specific startup sequence (Rainbow -> Breath -> Wave)
  startupGreeting(); 

  Serial.println(F("Interaction confirmed. POMODORO STARTED."));
  
  // Set initial state to "Working"
  lastState = 0; 
  targetPos = OPEN_POS;
  pomodoroStart = millis(); // Start the timer clock
  startPatternAfter = millis() + 1000;
}

// ================= MAIN LOOP =================
// Runs continuously. Acts as the central dispatcher.
void loop() {
  
  // 1. PRIORITY CHECK: Are we in Pause Mode?
  // If yes, handle pause logic and skip the rest of the loop.
  if (inPause) {
    handlePauseMode();
    return; 
  }

  // 2. SENSOR POLLING
  // Non-blocking timer: Check sensor every SENSOR_INTERVAL (100ms)
  if (millis() - lastSensorRead > SENSOR_INTERVAL) {
    lastSensorRead = millis();
    int currentState = checkPresence(); // Read filtered distance
    handleStateChange(currentState);    // Update state machine logic
  }

  // 3. BEHAVIORAL CHECKS
  // Check if we need to start the "Withering" sequence
  checkDramaticClosingStart();
  // Execute the "Withering" animation step (if active)
  updateDramaticClose(); 
  
  // 4. TIMER & FEEDBACK UPDATES
  updatePomodoroTimer(); // Check if work time is over
  updateLightPatterns(); // Update LED "Knight Rider" effect

  // 5. MOTOR ACTUATION
  // Move servo smoothly to target (unless we are in dramatic closing mode)
  if (!dramaticClosingActive) {
    smoothServoMove();
  }
}

// ================= HELPER FUNCTIONS =================

// Helper to get the correct color based on current gamification level
uint32_t getLevelColor(int brightness) {
  if (brightness < 0) brightness = 0;
  if (brightness > 255) brightness = 255; 

  // Calculate RGB values based on brightness percentage
  uint8_t r = (LEVEL_COLORS[currentLevel][0] * brightness) / 255;
  uint8_t g = (LEVEL_COLORS[currentLevel][1] * brightness) / 255;
  uint8_t b = (LEVEL_COLORS[currentLevel][2] * brightness) / 255;
  
  return strip.Color(r, g, b);
}

// ================= LOGIC IMPLEMENTATION =================

// --- STARTUP SEQUENCE ---
// A blocking routine that runs only at boot to greet the user
void startupGreeting() {
  // 1. Rainbow Cycle
  for(int j=0; j<256; j+=5) {
    for(int i=0; i<NUM_LEDS; i++) strip.setPixelColor(i, strip.ColorHSV(j*256, 255, MAX_BRIGHTNESS));
    strip.show();
    delay(10);
  }
  strip.clear();
  strip.show();

  Serial.println(F("Waiting for user greeting..."));

  // 2. Breathing Light Loop (Waiting for user)
  int breathVal = 0;
  int breathDir = 1;
  while (true) {
    // Pulse logic
    breathVal += breathDir;
    if (breathVal >= MAX_BRIGHTNESS) breathDir = -1;
    if (breathVal <= 5) breathDir = 1;
    
    for(int i=0; i<NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(breathVal, breathVal, breathVal));
    strip.show();
    
    // Check if user is close (< 50cm) to break the loop
    long d = readDistanceStable();
    if (d > 0 && d < 50) { 
      Serial.println(F("User Seen! Hello!"));
      break; 
    }
    delay(40); 
  }

  // 3. Mechanical Wave (Wiggle)
  if (!myServo.attached()) myServo.attach(SERVO_PIN);
  for (int i = 0; i < 2; i++) {
    myServo.write(OPEN_POS); // Open
    uint32_t c = strip.Color(0, MAX_BRIGHTNESS, 0); // Green Flash
    for(int j=0; j<NUM_LEDS; j++) strip.setPixelColor(j, c);
    strip.show();
    delay(300);
    myServo.write(OPEN_POS - 25); // Slight Close
    strip.clear();
    strip.show();
    delay(300);
  }
  myServo.write(OPEN_POS);
  fadeLedsIn(); 
}

// --- MEDIAN FILTER ALGORITHM ---
// Reads the sensor 5 times and returns the median value to remove noise/glitches
long readDistanceStable() {
  int readings[5]; 
  int validCount = 0;
  
  // Take 5 raw readings
  for (int i = 0; i < 5; i++) {
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 6000); // Timeout 6ms (~1m range)
    long dist = (duration == 0) ? -1 : (duration * 0.034 / 2);
    
    // Accept only reasonable values (2cm to 200cm)
    if (dist > 2 && dist < 200) { 
      readings[validCount] = dist; 
      validCount++; 
    }
    delayMicroseconds(200); 
  }
  
  if (validCount == 0) return 999; // Return high value if no valid reading
  
  // Sort the array (Bubble Sort)
  for (int i = 0; i < validCount - 1; i++) {
    for (int j = 0; j < validCount - i - 1; j++) {
      if (readings[j] > readings[j + 1]) {
        int temp = readings[j]; readings[j] = readings[j + 1]; readings[j + 1] = temp;
      }
    }
  }
  // Return the middle element (Median)
  return readings[validCount / 2];
}

// --- PAUSE MODE LOGIC ---
void handlePauseMode() {
  patternActive = false;
  pauseClosingAnimation(); // Slowly dim lights

  // Check if pause time is over
  if (millis() - pauseStart >= PAUSE_DURATION) {
    
    // If user is LATE (10 seconds past pause end), trigger Angry Dance
    if (!pauseAngryDanceDone && (millis() - pauseStart >= PAUSE_DURATION + 10000)) {
       Serial.println(F("LATE FOR WORK! ANGRY DANCE!"));
       angryDance();
       pauseAngryDanceDone = true; 
    }
    
    // Check if user has returned
    if (millis() - lastSensorRead > SENSOR_INTERVAL) {
      lastSensorRead = millis();
      long d = readDistanceStable(); 
      if (d < (THRESHOLD_CM - HYSTERESIS_CM)) {
        // RESUME WORK
        Serial.println(F("Pause over -> User detected -> RESUMING!"));
        inPause = false;
        targetPos = OPEN_POS;
        currentLevel = (currentLevel + 1) % NUM_LEVELS; // Level Up!
        Serial.print("Level Up! Current Level: "); Serial.println(currentLevel);
        fadeLedsIn(); 
        pomodoroStart = millis(); // Reset timer for next cycle
        pomodoroElapsed = 0;
        startPatternAfter = millis() + 1000;
      }
    }
  }
}

// --- PRESENCE DETECTION (HYSTERESIS) ---
// Prevents flickering by requiring multiple consecutive readings to change state
int checkPresence() {
  long distance = readDistanceStable(); 
  
  if (distance < THRESHOLD_CM - HYSTERESIS_CM) {
    presentCounter++;
    absentCounter = 0;
  } else if (distance > THRESHOLD_CM + HYSTERESIS_CM) {
    absentCounter++;
    presentCounter = 0;
  }
  
  // Only change state if threshold counts are met
  if (presentCounter >= REQUIRED_PRESENT) return 0; // 0 = Present
  if (absentCounter >= REQUIRED_ABSENT) return 1;   // 1 = Absent
  return lastState; // Otherwise keep last state
}

// --- STATE CHANGE HANDLER ---
// Manages logic when switching between Present (0) and Absent (1)
void handleStateChange(int currentState) {
  if (currentState == lastState) return; // No change

  if (currentState == 0) { // User Returned
    Serial.println(F("PRESENT -> OPENING"));
    targetPos = OPEN_POS;
    
    // Reset dramatic flags
    dramaticClosingActive = false;
    waitingForDramatic = false; 

    // Re-attach servo if it was detached during withering
    if (!myServo.attached()) myServo.attach(SERVO_PIN);

    if (!waitingForDramatic) {
      fadeLedsIn();
    }
    // FREEZE MEMORY: Calculate new start time so timer resumes correctly
    pomodoroStart = millis() - pomodoroElapsed;
    
  } else { // User Left
    Serial.println(F("ABSENT -> Waiting 5s..."));
    absentDetectedTime = millis();
    waitingForDramatic = true; // Start 5s countdown
  }

  presentCounter = 0;
  absentCounter = 0;
  lastState = currentState;
}

// --- SOFTWARE INTERRUPT ---
// Called inside blocking loops to check if user returned immediately
bool checkInterrupt() {
  long dist = readDistanceStable(); 
  // If user is detected within range
  if (dist < (THRESHOLD_CM - HYSTERESIS_CM)) {
    Serial.println(F("INTERRUPT! User returned!"));
    
    // Stop all adverse animations
    dramaticClosingActive = false;
    waitingForDramatic = false;
    targetPos = OPEN_POS;
    lastState = 0; 
    
    // Wake up servo
    if (!myServo.attached()) myServo.attach(SERVO_PIN);
    
    fadeLedsIn(); 
    // Restore timer
    pomodoroStart = millis() - pomodoroElapsed;
    return true; // Signal that interrupt occurred
  }
  return false;
}

// --- ANGRY DANCE ANIMATION ---
// Rapid shaking and red lights
void angryDance() {
  Serial.println(F("ANGRY DANCE START!"));
  if (!myServo.attached()) myServo.attach(SERVO_PIN);

  for (int i = 0; i < 25; i++) {
    if (checkInterrupt()) return; // Exit if user returns
    
    myServo.write(OPEN_POS); 
    // Red Flash
    for(int j=0; j<NUM_LEDS; j++) strip.setPixelColor(j, strip.Color(MAX_BRIGHTNESS, 0, 0)); 
    strip.show();
    delay(100);
    
    if (checkInterrupt()) return; // Exit if user returns
    
    myServo.write(OPEN_POS - 25); // Shake
    strip.clear();
    strip.show();
    delay(100);
  }
  // Reset
  myServo.write(OPEN_POS);
  strip.clear();
  strip.show();
  delay(100);
}

// --- WITHERING TRIGGER LOGIC ---
void checkDramaticClosingStart() {
  // If user is absent, NOT yet withering, but waiting time (5s) has passed
  if (lastState == 1 && !dramaticClosingActive && waitingForDramatic && (millis() - absentDetectedTime >= ABSENT_DELAY)) {
    
    // 1. Warning Shake
    angryDance();
    
    // Check if user came back during shake
    if (lastState == 0) { waitingForDramatic = false; return; }

    // 2. Start Withering
    Serial.println(F("Starting WITHERING..."));
    dramaticClosingActive = true; 
    waitingForDramatic = false; 
    patternActive = false;
    
    if (!myServo.attached()) myServo.attach(SERVO_PIN);
    dramPos = myServo.read(); // Start from current angle
  }
}

// --- TIMER UPDATE ---
void updatePomodoroTimer() {
  if (lastState == 0 && !dramaticClosingActive) {
    pomodoroElapsed = millis() - pomodoroStart;

    // Flash LEDs when 4 seconds remain
    if (pomodoroElapsed >= WORK_DURATION - 4000 && pomodoroElapsed < WORK_DURATION) {
      flashLeds(); 
    }
    
    // Cycle Complete
    if (pomodoroElapsed >= WORK_DURATION) {
      Serial.println(F("Cycle Complete! Happy Dance!"));
      happyDance(); 
      
      // Enter Pause Mode
      inPause = true;
      pauseStart = millis();
      targetPos = CLOSED_POS;
      patternActive = false;
      pauseLedLevel = MAX_BRIGHTNESS; 
      pauseAngryDanceDone = false; 
    }
  }
}

// --- LED PATTERN GENERATOR ---
// "Knight Rider" circular chase effect
void updateLightPatterns() {
  if (!dramaticClosingActive && lastState == 0 && myServo.read() == OPEN_POS && millis() > startPatternAfter) {
    patternActive = true;
  }
  
  if (patternActive && !dramaticClosingActive) {
    if (millis() - lastPatternStep >= PATTERN_SPEED) {
      lastPatternStep = millis();
      strip.clear();
      
      // Create a trail of 5 pixels
      for (int k = 0; k < 5; k++) {
        int fadeLvl = MAX_BRIGHTNESS - (k * (MAX_BRIGHTNESS / 5));
        uint32_t finalColor = getLevelColor(fadeLvl); // Use current level color
        
        // Circular math to wrap around the ring
        int led1 = (patternIndex - k + NUM_LEDS) % NUM_LEDS;
        strip.setPixelColor(led1, finalColor);
        // Mirror effect (opposite side)
        int led2 = (led1 + (NUM_LEDS / 2)) % NUM_LEDS; 
        strip.setPixelColor(led2, finalColor);
      }
      strip.show();
      patternIndex = (patternIndex + 1) % NUM_LEDS; // Advance index
    }
  }
}

// --- ANTI-JITTER SERVO MOVEMENT ---
// Moves servo incrementally and cuts power when destination is reached
void smoothServoMove() {
  static unsigned long lastMove = 0;
  // Non-blocking delay (20ms per degree)
  if (millis() - lastMove >= 20) {
    lastMove = millis();
    int currentPos = myServo.read();
    
    if (currentPos != targetPos) {
      // Re-attach if previously detached
      if (!myServo.attached()) {
        myServo.attach(SERVO_PIN);
        currentPos = myServo.read();
      }
      // Move 1 degree closer to target
      if (currentPos < targetPos) myServo.write(currentPos + 1);
      else if (currentPos > targetPos) myServo.write(currentPos - 1);
    } 
    else {
      // DETACH: Cuts power to stop jittering noise and save energy
      if (myServo.attached()) {
        myServo.detach();
      }
    }
  }
}

void pauseClosingAnimation() {
  smoothServoMove();
  // Slowly fade out LEDs
  if (pauseLedLevel > 0) {
    pauseLedLevel--;
    uint32_t color = getLevelColor(pauseLedLevel);
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
    strip.show();
    delay(30);
  } else {
    strip.clear();
    strip.show();
  }
}

// --- WITHERING ANIMATION (DRAMATIC CLOSING) ---
// Simulates the flower dying.
void updateDramaticClose() {
  if (!dramaticClosingActive) return; 

  if (!myServo.attached()) myServo.attach(SERVO_PIN);

  // 1. SLOW DESCENT (Loop)
  int steps = 20;
  for (int i = 0; i < steps; i++) {
    if (checkInterrupt()) return; // Check for user return
    
    dramPos--; // Decrease angle (Closing towards 0)
    if (dramPos < CLOSED_POS) dramPos = CLOSED_POS;
    myServo.write(dramPos);
    
    // Dim LEDs red
    int brightness = map(i, 0, steps, 0, MAX_BRIGHTNESS);
    for (int j = 0; j < NUM_LEDS; j++) strip.setPixelColor(j, strip.Color(brightness, 0, 0));
    strip.show();
    
    delay(30); 
    if (dramPos <= CLOSED_POS) break;
  }

  // 2. END OF ANIMATION
  if (dramPos <= CLOSED_POS) {
    // A final twitch (bounce) to make it look organic
    dramPos += 10; 
    if (dramPos > OPEN_POS) dramPos = OPEN_POS;
    myServo.write(dramPos);
    strip.clear();
    strip.show();
    delay(200); 

    // Final Close
    dramPos = CLOSED_POS; 
    myServo.write(CLOSED_POS);
    delay(500);

    // 3. SHUTDOWN
    dramaticClosingActive = false; 
    waitingForDramatic = false;
    
    // IMPORTANT: Detach servo. The robot is now "dead"/sleeping.
    myServo.detach(); 
    return;
  }
}

// Utility: Fade LEDs from 0 to Max
void fadeLedsIn() {
  for (int b = 0; b <= MAX_BRIGHTNESS; b++) {
    uint32_t color = getLevelColor(b);
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
    strip.show();
    delay(50);
  }
}

// Utility: Flash LEDs (Alert)
void flashLeds() {
  for (int cycle = 0; cycle < 2; cycle++) {
    for (int b = 0; b <= MAX_BRIGHTNESS; b++) {
      uint32_t color = getLevelColor(b);
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
      strip.show();
      delay(25); 
    }
    for (int b = MAX_BRIGHTNESS; b >= 0; b--) {
      uint32_t color = getLevelColor(b);
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, color);
      strip.show();
      delay(25); 
    }
  }
}

// Happy Dance: Random colors + Slow wave
void happyDance() {
  if (!myServo.attached()) myServo.attach(SERVO_PIN);
  for (int i = 0; i < 6; i++) {
    myServo.write(OPEN_POS); 
    uint32_t rndColor = strip.Color(random(0, 50), random(0, 50), random(0, 50)); 
    for(int j=0; j<NUM_LEDS; j++) strip.setPixelColor(j, rndColor);
    strip.show();
    delay(500); 
    myServo.write(OPEN_POS - 45); 
    rndColor = strip.Color(random(0, 50), random(0, 50), random(0, 50)); 
    for(int j=0; j<NUM_LEDS; j++) strip.setPixelColor(j, rndColor);
    strip.show();
    delay(500); 
  }
  myServo.write(OPEN_POS);
  strip.clear();
  strip.show();
}