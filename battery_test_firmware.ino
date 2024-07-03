#define CMD_STRT 0xB3

#define CMD_PING 0x00
#define CMD_ASSI 0x01
#define CMD_DATA 0x02
#define CMD_STND 0x04
#define CMD_DISC 0x05
#define CMD_CHAR 0x06
#define CMD_COMP 0x07
#define CMD_END  0x08

#define CMD_MAX_PAYLOAD_LENGTH 12

bool startDelimiterFound = false;
bool commandIdFound = false;

uint8_t benchID = 0xFF;
uint8_t frameLengths[] = { 4, 4, 13, 0, 3, 3, 3, 4 };
uint8_t currentFrameLength = 0;
uint8_t frameIdx = 0;
uint8_t rxBuffer[CMD_MAX_PAYLOAD_LENGTH];
uint8_t txBuffer[CMD_MAX_PAYLOAD_LENGTH];

void setup() {
    Serial.begin(115200);
}

void loop() {
    while (Serial.available() == 0);

    uint8_t incomingByte = Serial.read();

    if (startDelimiterFound && !commandIdFound) {
      if (incomingByte == 0x03 || incomingByte >= CMD_END) {
        startDelimiterFound = false;
      } else {
        commandIdFound = true;
        currentFrameLength = frameLengths[incomingByte];
        frameIdx = 1;
        rxBuffer[0] = incomingByte;
      }
    } else if (startDelimiterFound && commandIdFound) {
      rxBuffer[frameIdx++] = incomingByte;
      if (frameIdx == currentFrameLength) {
        parseCommand();
      }

      if (frameIdx > CMD_MAX_PAYLOAD_LENGTH) {
        startDelimiterFound = false;
        commandIdFound = false;
      }
    } else {
      if (incomingByte == CMD_STRT)
        startDelimiterFound = true;
    }
}

void parseCommand() {
  uint8_t checksum = CMD_STRT;
  for (int i = 0; i < currentFrameLength - 1; i++)
    checksum ^= rxBuffer[i];

  if (checksum != rxBuffer[currentFrameLength - 1])
    return;

  switch (rxBuffer[0]) {
  case CMD_PING:
    Serial.write((uint8_t)CMD_STRT);
    Serial.write((uint8_t)CMD_PING);
    Serial.write(benchID);
    Serial.write(checksum);
    break;
  case CMD_ASSI:
    benchID = rxBuffer[1];
    break;
  case CMD_DATA:
    break;
  case CMD_STND:
    break;
  case CMD_DISC:
    break;
  case CMD_CHAR:
    break;
  case CMD_COMP:
    break;
  default:
    break;
  }
}
