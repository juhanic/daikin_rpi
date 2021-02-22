#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <stdint.h>
#include <pthread.h>

#define BUTTON_PIN 1
#define TEXT_BUFFER_LEN 1024

volatile uint32_t lastHigh = 0;
volatile uint32_t lastLow = 0;

volatile char bitBuffer[8];
volatile int bufferPos;

volatile int frameNo = 0;
volatile int framePos = 0;

volatile char inSeq = 0;

char txtBuf[TEXT_BUFFER_LEN];
volatile int txtBufPos = 0;

pthread_mutex_t lock, bitlock;

void reset() {
  //  fprintf(stderr, "reset\n");
  inSeq = 0;
  frameNo = 0;
  framePos = 0;
  bufferPos = 0;
}

void writeSharedBuf(char* out) {
  int delta;

  pthread_mutex_lock(&lock);
  if (strlen(out) + txtBufPos > TEXT_BUFFER_LEN) {
    printf(txtBuf);
    txtBufPos = 0;
    if(strlen(out) > TEXT_BUFFER_LEN) {
      fprintf(stderr, "can't write to buffer as would overflow\n");
      exit(0);
    }
  }
  // pointer math beware!
  delta = sprintf(txtBuf + txtBufPos, out);
  txtBufPos += delta;
  pthread_mutex_unlock(&lock);
}

void flushBuf() {
  pthread_mutex_lock(&lock);
  if (txtBufPos > 0) {
    printf(txtBuf);
    txtBufPos = 0;
  }
  pthread_mutex_unlock(&lock);
}

void processNewDaikin() {
  writeSharedBuf("startup step 1\n");
  inSeq = 1;
}

void processDaikinConfirmation() {
  writeSharedBuf("Confirmation\n");
  reset();
  // should be a two stage message so verify the first step happenned)
  if (inSeq == 1) {
    writeSharedBuf("Starting new message\n");
    inSeq = 2;
  }
}

void processNewFrame() {
  char msgBuf[256];
  // do some basic verification of previous frame
  /*  if (bufferPos != 0) {
    fprintf(stderr, "attempt to go to next frame mid-char, likely missed data\n");
    reset();
    return;
  }*/
  frameNo++;
  framePos = 0;
  bufferPos = 0;
  sprintf(msgBuf, "Starting frame %d\n", frameNo);
  writeSharedBuf(msgBuf);
}

void printChar() {
  writeSharedBuf("  ");
  bufferPos = 0;
}

void processBit(char bit) {
  pthread_mutex_lock(&bitlock);
  writeSharedBuf(bit > 0 ? "1":"0");
  //  printf("%d", bit);
  bitBuffer[bufferPos] = bit;
  bufferPos++;
  if (bufferPos == 8) {
    printChar();
  }
  pthread_mutex_unlock(&bitlock);
}

void badData(uint32_t duration) {
  if (inSeq) {
    fprintf(stderr, "unexpected high received in ir length %d\n", duration);
  }
  reset();
}

void processHighTime(uint32_t duration) {
  if (duration > 24500 && duration < 26000) {
    processNewDaikin();
  } else if(duration > 1500 && duration < 1700) {
    processDaikinConfirmation();
  } else if(duration > 34000 && duration < 36000) {
    processNewFrame();
  } else if(duration > 300 && duration < 450) {
    processBit(0);
  } else if(duration > 1150 && duration < 1300) {
    processBit(1);
  } else {
    badData(duration);
  }
}

int diff(uint32_t first, uint32_t second) {
  //  printf("diff %d, %d\n", first, second);
  if (first > second) {
    return ~first + 1 + second;
  } else {
    return (second - first);
  }
}



volatile
void fallingInt(uint32_t curTime) {
  lastLow = curTime;
  //  printf("Fall %d\n", diff(curTime, lastHigh));
  processHighTime(diff(lastHigh, lastLow));
}

volatile
void risingInt(uint32_t curTime) {
  
  //  printf("rise %d", diff(curTime, lastLow));
  lastHigh = curTime;
}

volatile
void changeInt(void) {
  uint32_t curTime;
  int res;

  curTime = micros();

  //  printf("edge detected\n");
  if (digitalRead(BUTTON_PIN)) {
    risingInt(curTime);
  } else {
    fallingInt(curTime);
  }
}

int main(void) {
  printf("Starting up ir sensor\n");
  if (wiringPiSetup() < 0) {
    fprintf(stderr, "Unable to setup wiringPi: %s\n", strerror(errno));
    return 1;
  }

  if (pthread_mutex_init(&lock, NULL) != 0) {
    printf("\n mutex init failed\n");
    return 1;
  }

  if (pthread_mutex_init(&bitlock, NULL) != 0) {
    printf("\n bit buffer mutex init failed\n");
    return 1;
  }
  
  pinMode(BUTTON_PIN, INPUT);
  pullUpDnControl(BUTTON_PIN, PUD_UP);
  if (wiringPiISR(BUTTON_PIN, INT_EDGE_BOTH, &changeInt) < 0) {
    fprintf(stderr, "Unable to setup ISR: %s\n", strerror(errno));
    return 1;
  }

  while (1) {
    delay(1000);
    //    flushBuf();
  }
  return 0;
}
