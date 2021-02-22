#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <stdint.h>

#define BUTTON_PIN 1
#define TEXT_BUFFER_LEN 1024
#define TEXT_LOCK 0
#define BIT_BUF_LOCK 1

#define STATE_NONE 0
#define STATE_WAITING_CONF 1
#define STATE_WAITING_FRAME 2

volatile uint32_t lastHigh = 0;
volatile uint32_t lastLow = 0;

volatile char bitBuffer[8];
volatile int bufferPos;

volatile int frameNo = 0;
volatile int framePos = 0;

// some basic state vars
volatile char inSeq = 0;

char txtBuf[TEXT_BUFFER_LEN];
volatile int txtBufPos = 0;

void reset() {
  //  fprintf(stderr, "reset\n");
  inSeq = 0;
  frameNo = 0;
  framePos = 0;
  bufferPos = 0;
}

void writeSharedBuf(char* out) {
  int delta;

  piLock(TEXT_LOCK);
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
  piUnlock(TEXT_LOCK);
}

void flushBuf() {
  piLock(TEXT_LOCK);
  if (txtBufPos > 0) {
    printf(txtBuf);
    txtBufPos = 0;
  }
  piUnlock(TEXT_LOCK);
}

void processNewDaikin() {
  writeSharedBuf("\n--msg--\n");
  reset();
  inSeq = 1;
}

void processDaikinConfirmation() {
  //  writeSharedBuf("Confirmation\n");

  // this is just verifying the start of a new frame which should already have come
  // eventually we should verify this is sent after a new header
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
  sprintf(msgBuf, "\n--frame %d--\n", frameNo);
  writeSharedBuf(msgBuf);
}

// writes the low byte of the int to the buffer
// expects a buffer with at least 2 bytes
void toHex(int val, char* buf) {
  static int upperMask = 0xf0;
  static int lowerMask = 0x0f;
  int hi, lo;
  hi = (val & upperMask) >> 4;
  lo = (val & lowerMask);

  // the comparison to 9 is to make get a-f instead of ; to ?
  buf[0] = hi + '0';
  if (hi > 9) {
    buf[0] += 7;
  }
  buf[1] = lo + '0';
  if (lo > 9) {
    buf[1] += 7;
  }
}

void advanceByte() {
  framePos++;
  if (frameNo < 2 && framePos >= 8) {
    writeSharedBuf(" frame complete\n");
    return;
  }
  if (frameNo == 2 && framePos >= 19) {
    writeSharedBuf("\n\t--daikin--\n");
    reset();
    flushBuf();
    return;
  }
}

void printChar() {

  char buf[4];
  int delta = 0;
  int i;

  if (bufferPos != 8) {
    fprintf(stderr, "incomplete byte\n\n");
  }

  buf[2] = ' ';
  buf[3] = 0;
  for (i=7; i >= 0; i--) {
    delta = delta << 1;
    delta += bitBuffer[i];
  }

  toHex(delta, buf);
  writeSharedBuf(buf);
  advanceByte();  
  bufferPos = 0;
}

void processBit(char bit) {
  piLock(BIT_BUF_LOCK);
  //  writeSharedBuf(bit > 0 ? "1":"0");
  bitBuffer[bufferPos] = bit;
  bufferPos++;
  if (bufferPos == 8) {
    printChar();
  }
  piUnlock(BIT_BUF_LOCK);
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
  } else if(duration > 200 && duration < 450) {
    processBit(0);
  } else if(duration > 1100 && duration < 1300) {
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
