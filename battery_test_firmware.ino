//Must add the following manager, for the pico api 
//https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
//It save alot of time dealing with the registers
#include "hardware/irq.h"
#include <Arduino.h>
#include <SPI.h> 
#include <Wire.h>
#include "pico/stdlib.h"         
#include "hardware/rtc.h" 
#include <cstdint>
#include <cstddef>
///address for the Charge and discharge stuff is 0100000 32 (decimal), 20 (hexadecimal)
//Second address for the adc is 1101110 https://octopart.com/datasheet/mcp3428-e%2Fsl-microchip-13127436 9.3.1 how to get back the orginal value 4.9.2 5.2  is important for the register manupulation 
//Double check how the first adc work ? There is no way 1 byte is enought for the voltage reading 
//Renaming The ENcoding
#define CMD_STRT 0xB3
#define CMD_PING 0x00
#define CMD_ID 0x01
#define CMD_DATA 0x02
#define CMD_STND 0x04
#define CMD_DISC 0x05
#define CMD_CHAR 0x06
#define CMD_ACK 0x08
#define RX_CMD_LENGTH 14// 0xB3 |Frame ID | ping | Checksum - what about data request ?
#define TX_CMD_MAX_LENGTH 14
#define ADC_ADDR 0x6E // Which adc and is it correct ? 

//spi stuff
constexpr uint8_t PIN_MOSI = 16;   // GP16
constexpr uint8_t PIN_MISO = 19;   // GP19
constexpr uint8_t PIN_SCK  = 18;   // GP18
constexpr uint8_t PIN_CS   = 20;   // GP20
SPIClass &spiCustom = SPI;  //There is SPI and SPI must chose
constexpr uint32_t SPI_CLOCK_HZ = 8'000'000;   // 8 MHz
SPISettings spiSettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0);
uint16_t BenchIVT(uint16_t Att_Chan);



//Variable used to read
uint8_t CurrentBufferSize = 0;
uint8_t rxBuffer[RX_CMD_LENGTH];
uint8_t txBuffer[TX_CMD_MAX_LENGTH];
uint8_t RegisterMemory = 0;

// List of possible states
enum Command{
  Ping,
  ID,
  RequestData,
  SetStandBy,
  SetDischarge,
  SetCharge,
  BadCommand
};

// Make sure to change the protocol to excule 0xff as a valid value
constexpr int  MaxBench =4; //Set the number of bench of the program
uint8_t batteryBenches[MaxBench]; // array that hold the ids of the bench


//Flag to see if the GUI has responded to the ping
volatile bool BencheFlag[MaxBench];

int BufSize (int8_t  CMD);
uint8_t calculate_crc8(const uint8_t* data, size_t length); // Checksumthing 
void SendData();
void Ack();
void Nack();
Command ReadCommand();
int findFirstZeroId();
int isValidId( uint8_t id); //If Id is valid, return the index and -1 otherwise
//Iram means is must be in the ram, and It is the interrupt handler for the 1 second timer
bool ping();

int CurrentId; //Hold the index of the id that we are working with

//The flag for the alarm
volatile bool TimerFlag = false;
uint8_t MagicReg = 0;
//----------------------------------------------------------------------------------------

void setup() {
  //should reset the  TCA6408A
  SPI.end();
  Wire.end(); 

  for (int i = 0; i< MaxBench; i++){
  BencheFlag[i] = false;      //set flase when first change     
}
for (int i = 0; i< MaxBench; i++){ //Setting the Array of ID
  batteryBenches[i] = 0xff;          
 
}
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);//must turn off before transmetting 
  SPI.setRX(PIN_MISO);   // MISO
  SPI.setTX(PIN_MOSI);   // MOSI
  SPI.setSCK(PIN_SCK);   // SCK
  SPI.begin();
  Serial.begin(19200); /// is it the same rate as the computer ??? 
  while (!Serial) { } 
  
  //I2C BS
  Wire.setSDA(2);
  Wire.setSCL(3);
  Wire.begin();
  Wire.beginTransmission(0x20 ); //Setting all pins to output
  Wire.write(0x03);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.beginTransmission(0x20); //setting all the output to 0
  Wire.write(0x01);
  Wire.write(0x00);
  Wire.endTransmission();

int64_t TimerHandler(alarm_id_t alarm_id, void *user_data);
add_alarm_in_us(
    1'000'000ULL,   // delay = 1second 
    TimerHandler,   
    nullptr,        
    true            // set to true so the callback can request repeats
);
}


void loop() {
  //Timer Flag stuff here 
  if (TimerFlag) {//
    TimerFlag = false; //Reset the timer flag
    
    for (int i = 0; i< MaxBench; i++){
      if(BencheFlag[i]==false){
          batteryBenches[i] = 0xff; //reset flag
//============================================== Reset the State of the bench too
      } 
    BencheFlag[i] = false;//must reset old bencheFlag  
    //SendBenchId(batteryBenches[i]);//Sending the pings every time the flag is flipflopped   
      }                
  }
  

if (Serial.available() > 0) {//check that there is something to read
  //Receive
  //must read everything for a set command
  Command Incomming = ReadCommand();

  switch (Incomming){
      case Command::Ping:{    //this is the Ping Command that was already called ? Maybe we dont need that Why we double the trouble
            int temp = isValidId( rxBuffer[2]); //check if it exist 
            if (temp >= 0){ // If the found in the array  do 
              BencheFlag[temp] = true; //Set the flag to true
              Ack();
            }else { 
              int temp2 =findFirstZeroId(); // find the firstempty space
              if (temp2 != -1 ){ // if there is space
                BencheFlag[temp2]= true; // Change the flag
                batteryBenches[temp2] = rxBuffer[2]; // and take the sapce
                Ack();
              }else Nack(); //otherwise Nack that shit 
              
            }
            
                 
        break;}
         

      case Command::RequestData:{
        SendData();
        break;}
//-------------------------------------------------  Should there a possibility to charge it and discharge it at the same time ? 
      case Command::SetStandBy:{
     
        switch (CurrentId){
          case  0:
            UnsetState (2); 
            UnsetState (3);
            Ack();
            break;
          case 1:
            UnsetState  (0);
            UnsetState (1);
            Ack();
            break;

          case 2:
            UnsetState (6); 
            UnsetState (7);
            Ack();
            break;

          case 3:
            UnsetState (4); 
            UnsetState (5);
            Ack();
            break;
          default : 
          Nack();

          break;
        }
        break;
        }

      case Command::SetDischarge:{
      
        switch (CurrentId){
          case  0:
            SetState (2); 
            UnsetState (3);
            Ack();
            break;
          case 1:
            SetState (0);
            UnsetState (1);
            Ack();
            break;

          case 2:
            SetState (6); 
            UnsetState (7);
            Ack();
            break;

          case 3:
            SetState (4); 
            UnsetState (5);
            Ack();
            break;
          default : 
          Nack();
          break;
        }
        break;
        }
        
      /*
      0   B
      1   A
      2   D
      3   C
      id  Bench
      */
      case Command::SetCharge:{
       
        switch (CurrentId){
          case  0:
            SetState (1); 
            UnsetState (0);
            Ack();
            break;
          case 1:
            SetState (3); 
            UnsetState (2);
            Ack();
            break;

          case 2:
            SetState (5); 
            UnsetState (4);
            Ack();
            break;

          case 3:
            SetState (7);
            UnsetState (6);
            Ack();
            break;
          default : 
          Nack();
          break;
        }
        break;
        }
        
      case Command::BadCommand:{
        Nack();
        break;}
       }
  }
}

uint16_t BenchIVT(uint16_t Att_Chan){// obtain the information from the adc on the main board 
  Att_Chan = (0b0001000000000000 | Att_Chan <<7); //must check D|06 depeding on the Ref page 31
  digitalWrite(PIN_CS, LOW);
  uint16_t r = SPI.transfer16(Att_Chan);
  digitalWrite(PIN_CS, HIGH);// Better to write by changing the registers directly 
  digitalWrite(PIN_CS, LOW);//DO I need to add a delay here ? Is it too fast for the adc ??????
  r = SPI.transfer16(0x0000); //send a dummy
  digitalWrite(PIN_CS, HIGH);
  return r;
}
int64_t TimerHandler(alarm_id_t alarm_id, void *user_data)
{
    (void)alarm_id;   
    (void)user_data; 

    TimerFlag = true;        
    return 1'000'000;               
}

Command ReadCommand(){

  int byte_num = 0;
  //Is there cases where we need more than 4 (we need to check the checksum and send bad command
  rxBuffer[byte_num++] = Serial.read(); //Check the CMD_STRT
  rxBuffer[byte_num++] = Serial.read(); //Check wSetState (uint8_t p)=+hat command it is
  rxBuffer[byte_num++] = Serial.read(); //read the id

  // should check if the commad is valid first //WHERE THE FUCK IS THE FUCKING LOOP FOR THAT ?
  // if bad delim happens should, should cycle until next valid command
  //I dont neeed a timeout right ? no because the checksum will catch it and not probable to lose information
  
  CurrentBufferSize = BufSize (rxBuffer[1] ); // check the FrameID to determine the size of the buffer //Dont we need to check if rxBuffer1 is valid first ?
  for (byte_num; byte_num < CurrentBufferSize ;byte_num++){ //Load the whole frame from the buffer 
      rxBuffer[byte_num] = Serial.read(); 

  }//add the id timer reset here

  if(ChecksumCheck () ==0)      // if checksum failed  
    return Command::BadCommand;
  //Check DILIMITER
  if(rxBuffer[0] != CMD_STRT)
    return Command::BadCommand; //bad DILIMITER
  if(ping() == 0 ) //Check if the id is valid    
    return Command::BadCommand;
  CurrentId = isValidId(rxBuffer[1]); //Set the id used for the next command  //Not sure that is correct 

  if(rxBuffer[1] == CMD_PING)
    return Command::Ping;
   
  if(rxBuffer[1] == CMD_DATA)
    return Command::RequestData;
 
  if(rxBuffer[1] == CMD_STND)
    return Command::SetStandBy;

  if(rxBuffer[1] == CMD_DISC)
    return Command::SetDischarge;

  if(rxBuffer[1] == CMD_CHAR)
    return Command::SetCharge;

  return Command::BadCommand; //invalid Command id
}
// void SendBenchId(uint8_t IDeas){ //Not needed anymore ? 
//   byte buf[4] = {CMD_STRT , CMD_PING, IDeas,  CMD_STRT ^ CMD_PING ^ IDeas};
//   Serial.write(buf,4);
 
// }
void SetState (uint8_t p){ // I2C Connection 
/*Valide option are Just double check with documentations 
0 B DIR
1 B Charge 
2 A DIR
3 A Charge 
4 D Dir 
5 D Charge 
6 C DIR 
7 C Charge 
*/
  
  RegisterMemory  | (1 << p); //So shift 1 to number of p // same problem as unsetstate Also B
  Wire.beginTransmission(0x20 );
  Wire.write(0x01);
  Wire.write(RegisterMemory);
  Wire.endTransmission();
}
void UnsetState (uint8_t p){ // I2C Connection 
/*Valide option are Just double check with documentations 
0 B DIR
1 B Charge 
2 A DIR
3 A Charge 
4 D Dir 
5 D Charge 
6 C DIR 
7 C Charge 
*/uint8_t temp = 0b11111111;

  temp & (0 << p); //So shift 1 to number of p  /// Does it work the Way I meant it tooo ? DO I need to read the Register and then changing the bit and then writinng ?
  RegisterMemory && temp; 
  Wire.beginTransmission(0x20 );
  Wire.write(0x01); // the output register 
  Wire.write(RegisterMemory);
  Wire.endTransmission();
}

void Ack(){
  byte buf[4]= {CMD_STRT, CMD_ACK, 1 ,0};
  buf[3]= calculate_crc8(buf, std::size(buf)-1);
  Serial.write(buf, std::size(buf));
}

void Nack(){
  byte buf[4]= {CMD_STRT, CMD_ACK, 0, 0};
  buf[3]= calculate_crc8(buf, std::size(buf)-1);
  Serial.write(buf, std::size(buf));
}
uint8_t SplitBit(uint16_t in, bool a){ //byte to split, and true for lsb and false for MSB        

  uint8_t low = in & 0x00FF;        // keep only bits 0‑7
  uint8_t high= (in >> 8) & 0x00FF; 
 return a ? low : high;
}


uint16_t readAdcChannel(uint8_t ch) 
/* c1 mostemp
   C2 ResTemp
   c3 VoltSense
   c4 CurrentSense*/          // ch = 1‑4
{
    
   if (ch < 1 || ch > 4) return 0xFFFF;
    
    uint8_t cfg = ((ch - 1) << 5) | 0x08;   //setting the register  Not Contiunous

    Wire.beginTransmission(0x6E);
    Wire.write(cfg);   // applying the setting
    Wire.endTransmission();

    // wait for conversion (RDY bit cleared)
    uint32_t t0 = millis(); //current system time 
    while (millis() - t0 < 100){ //is 100 long enoguth ?
        Wire.requestFrom(ADC_ADDR, (uint8_t)3);
        if (Wire.available() == 3) {
            uint8_t msb = Wire.read();
            uint8_t lsb = Wire.read();
            uint8_t status = Wire.read();
            if ((status & 0x80) == 0) {       // WHEn rdy is 0 we can take the value, but not before
                return (uint16_t(msb) << 8) | uint16_t(lsb);
            }
        }
        delay(5);
    }
    return 0xFFFF;                           // timeout / error
}


void SendData(){

/*
0 : C Tm
1 : C IM 
2 : C VM 
3 : D TM
4 : D IM
5 : D VM
10: A TM 
11: A IM 
!2: A VM
13: B TM
14: B IM
15: B VM

      0   B
      1   A
      2   D
      3   C
      id  Bench
  
  Battery Temp MSB  3
  Battery Temp LSB  4
  Bench Temp MSB  GP26 Internal Adc ? 5
  Bench Temp LSB  6
  Load MSB = 0x00  7//not how it is connected to the board beside p2
  Load LSB = 0x00 8
  Battery Voltage MSB 9
  Battery Voltage LSB 10
  Bench Current MSB 11
  Bench Current LSB 12
  Checksum13
*/

  txBuffer[0] = 0xB3;
  txBuffer[1] = 0x02;
  txBuffer[2] =  batteryBenches[CurrentId];

          switch (CurrentId){
          case  0:
          // R = V/I 
          if (readAdcChannel(3) == 0xFFFF|readAdcChannel(4)== 0xFFFF){
          txBuffer[7] = 0xFF;
          txBuffer[8] = 0xFF;
          }
          else {

          txBuffer[7] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),false);
          txBuffer[8] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),true);
          }
          //add the other temp
          txBuffer[3] = SplitBit((readAdcChannel(1)),false);
          txBuffer[4] = SplitBit((readAdcChannel(1)),true);
          txBuffer[5] = SplitBit(BenchIVT(13),false);
          txBuffer[6] = SplitBit(BenchIVT(13),true);
          txBuffer[9]  = SplitBit(BenchIVT(15),false);
          txBuffer[10] = SplitBit(BenchIVT(15),true);
          txBuffer[11] = SplitBit(BenchIVT(14),false);
          txBuffer[12] = SplitBit(BenchIVT(14),true);
          //checksum
          
          txBuffer[13]= calculate_crc8(txBuffer, std::size(txBuffer)-1);
          
          Serial.write(txBuffer , std::size(txBuffer));
            break;
          case 1:

          // R = V/I 
          if (readAdcChannel(3) == 0xFFFF|readAdcChannel(4)== 0xFFFF){
          txBuffer[7] = 0xFF;
          txBuffer[8] = 0xFF;
          }else {

          txBuffer[7] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),false);
          txBuffer[8] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),true);
          }
          //add the other temp
          txBuffer[3] = SplitBit((readAdcChannel(1)),false);
          txBuffer[4] = SplitBit((readAdcChannel(1)),true);
          txBuffer[5] = SplitBit(BenchIVT(10),false);
          txBuffer[6] = SplitBit(BenchIVT(10),true);
         
          txBuffer[9]  = SplitBit(BenchIVT(12),false);
          txBuffer[10] = SplitBit(BenchIVT(12),true);
          txBuffer[11] = SplitBit(BenchIVT(11),false);
          txBuffer[12] = SplitBit(BenchIVT(11),true);
          //checksum
          
          txBuffer[13]= calculate_crc8(txBuffer, std::size(txBuffer)-1);
        
           Serial.write(txBuffer , std::size(txBuffer));
            break;
          case 2:
         
          // R = V/I 
          if (readAdcChannel(3) == 0xFFFF|readAdcChannel(4)== 0xFFFF){
          txBuffer[7] = 0xFF;
          txBuffer[8] = 0xFF;
          }else {

          txBuffer[7] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),false);
          txBuffer[8] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),true);
          }
          //add the other temp
          txBuffer[3] = SplitBit((readAdcChannel(1)),false);
          txBuffer[4] = SplitBit((readAdcChannel(1)),true);
          txBuffer[5] = SplitBit(BenchIVT(3),false);
          txBuffer[6] = SplitBit(BenchIVT(3),true);
         
          txBuffer[9]  = SplitBit(BenchIVT(5),false);
          txBuffer[10] = SplitBit(BenchIVT(5),true);
          txBuffer[11] = SplitBit(BenchIVT(4),false);
          txBuffer[12] = SplitBit(BenchIVT(4),true);
          //checksum
          
          txBuffer[13]= calculate_crc8(txBuffer, std::size(txBuffer)-1);
           
           Serial.write(txBuffer , std::size(txBuffer));
            break;
          case 3:
           
          // R = V/I  also add the divde by 0 check
          if (readAdcChannel(3) == 0xFFFF|readAdcChannel(4)== 0xFFFF){
          txBuffer[7] = 0xFF;
          txBuffer[8] = 0xFF;
          }else {

          txBuffer[7] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),false);
          txBuffer[8] = SplitBit((readAdcChannel(3)/readAdcChannel(4)),true);
          }
          //add the other temp
          txBuffer[3] = SplitBit((readAdcChannel(1)),false);
          txBuffer[4] = SplitBit((readAdcChannel(1)),true);
          txBuffer[5] = SplitBit(BenchIVT(0),false);
          txBuffer[6] = SplitBit(BenchIVT(0),true);
         
          txBuffer[9]  = SplitBit(BenchIVT(2),false);
          txBuffer[10] = SplitBit(BenchIVT(2),true);
          txBuffer[11] = SplitBit(BenchIVT(1),false);
          txBuffer[12] = SplitBit(BenchIVT(1),true);
          //checksum
          
          txBuffer[13]= calculate_crc8(txBuffer, std::size(txBuffer)-1);
           
           Serial.write(txBuffer , std::size(txBuffer));
            break;
          default : 
          Nack();
          break;
        }

}
bool ChecksumCheck (){
  
  if (calculate_crc8(rxBuffer,CurrentBufferSize-1) == rxBuffer[CurrentBufferSize-1]) return 1;
  return 0;
}
int BufSize (int8_t  CMD){
  if(CMD == CMD_DATA)
    return 14;
  return 4;
}
int isValidId( uint8_t id) {
  for (int i = 0; i < MaxBench; i++) {
    if (batteryBenches[i] == id) {
      return i; // return the index 
    }
  }
  return -1; // return -1 if not found
}
int findFirstZeroId() {//remove the pointer and just put the array in the body
  for (int i = 0; i < MaxBench; i++) {
    if (batteryBenches[i] == 0xff) {
      return i;
    }
  }
  return -1; // return -1 if not found
}

bool ping () {
  int temp = isValidId( rxBuffer[2]); //check if it exist 
            if (temp >= 0){ // If the found in the array  do 
              BencheFlag[temp] = true; //Set the flag to true
              return 1; 
            }else { 
              int temp2 =findFirstZeroId(); // find the firstempty space
              if (temp2 != -1 ){ // if there is space
                BencheFlag[temp2]= true; // Change the flag
                batteryBenches[temp2] = rxBuffer[2]; // and take the sapce
                return 1; 
              }else return 0; //otherwise Nack that shit 
              
            }
}

/**
 * Calculates the CRC8 checksum for a given array of bytes.
 * 

 * 
 * Note: Uses the standard CRC-8 polynomial 0x07 (x^8 + x^2 + x + 1)
 *       Initial value is 0x00. Adjust POLYNOMIAL or INITIAL_VALUE if 
 *       you need a different variant (e.g., Dallas/Maxim uses 0x31).
 */
uint8_t calculate_crc8(const uint8_t* data, size_t length) {
    constexpr uint8_t POLYNOMIAL = 0x07;
    constexpr uint8_t INITIAL_VALUE = 0x00;
    
    uint8_t crc = INITIAL_VALUE;

    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}
