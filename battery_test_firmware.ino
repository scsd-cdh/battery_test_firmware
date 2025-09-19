//Must add the following manager, for the pico api 
//https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
//It save alot of time dealing with the registers
#include "hardware/irq.h"
#include <Arduino.h>
#include <SPI.h> 
#include <Wire.h>
#include "pico/stdlib.h"         
#include "hardware/rtc.h" 

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

//spi stuff
constexpr uint8_t PIN_MOSI = 16;   // GP16
constexpr uint8_t PIN_MISO = 19;   // GP19
constexpr uint8_t PIN_SCK  = 18;   // GP18
constexpr uint8_t PIN_CS   = 20;   // GP20
SPIClass &spiCustom = SPI1;  //There is spi0 and spi1 must chose
constexpr uint32_t SPI_CLOCK_HZ = 8'000'000;   // 8 MHz
SPISettings spiSettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0);
uint16_t BenchIVT(uint16_t Att_Chan);



//Variable used to read
uint8_t CurrentBufferSize = 0;
uint8_t rxBuffer[RX_CMD_LENGTH];

// List of possible states
enum BatteryBenchState{
  Standby,
  Charge,
  Discharge
} bench_state;

enum CompletionStatus{
  Success,
  Fail,
  InProgress
} completion_status;

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
constexpr int  MaxBench = 4; //Set the number of bench of the program
uint8_t batteryBenches[MaxBench]; // array that hold the ids of the bench


//Flag to see if the GUI has responded to the ping
volatile bool BencheFlag[MaxBench];

int BufSize (int8_t  CMD);
void SendData();
void Ack();
void Nack();
Command ReadCommand();
int findFirstZeroId();
int isValidId( uint8_t id); //If Id is valid, return the index and -1 otherwise
//Iram means is must be in the ram, and It is the interrupt handler for the 1 second timer

int CurrentId; //Hold the index of the id that we are working with

//The flag for the alarm
volatile bool TimerFlag = false;

//----------------------------------------------------------------------------------------
void setup() {
  //should reset the  TCA6408A
  SPI1.end();


  for (int i = 0; i< MaxBench; i++){
  BencheFlag[i] = false;      //set flase when first change     
 
}
for (int i = 0; i< MaxBench; i++){ //Setting the Array of ID
  batteryBenches[i] = 0xff;          
 
}
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);//must turn off before transmetting 
  SPI1.setRX(PIN_MISO);   // MISO
  SPI1.setTX(PIN_MOSI);   // MOSI
  SPI1.setSCK(PIN_SCK);   // SCK
  SPI1.begin();
  Serial.begin(19200);
  while (!Serial) { ; } 
 
int64_t TimerHandler(alarm_id_t alarm_id, void *user_data);
add_alarm_in_us(
    1'000'000ULL,   // delay = 2second 
    TimerHandler,   
    nullptr,        
    true            // set to true so the callback can request repeats
);
}


void loop() {
  //Timer Flag stuff here 
  if (TimerFlag) {
    TimerFlag = false; //Reset the timer flag
    
    for (int i = 0; i< MaxBench; i++){
      if(BencheFlag[i]==false){
          batteryBenches[i] = 0xff; //reset flag

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
      case Command::Ping:{
            int temp = isValidId( rxBuffer[2]); //check if id exist 
            if (temp >= 0){
              BencheFlag[temp] = true; 
            }else { 
              int temp2 =findFirstZeroId();
              if (temp2 != -1 ){
                BencheFlag[temp2]= true;
                batteryBenches[temp2] = rxBuffer[2];
              }else Nack();
              
            }
            
                 
        break;}
         

      case Command::RequestData:{
        SendData();
        break;}

      case Command::SetStandBy:{
        bench_state = BatteryBenchState::Standby;
        //must confirm ? 
        break;}

      case Command::SetDischarge:{
        bench_state = BatteryBenchState::Discharge;
        break;}

      case Command::SetCharge:{
        bench_state = BatteryBenchState::Charge;
       
        break;}

      case Command::BadCommand:{
        Nack();
        break;}
       }
  }
}

uint16_t BenchIVT(uint16_t Att_Chan){
  Att_Chan = (0b0001000000000000 | Att_Chan <<7); //must check D|06 depeding on the Ref
  digitalWrite(PIN_CS, LOW);
  uint16_t r = SPI1.transfer16(Att_Chan);
  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_CS, LOW);
  r = SPI1.transfer16(0x0000); //send a dummy
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
  rxBuffer[byte_num++] = Serial.read(); //Check what command it is

  CurrentBufferSize = BufSize (rxBuffer[byte_num++] );
  for (byte_num; byte_num < CurrentBufferSize ;byte_num++){
      rxBuffer[byte_num] = Serial.read();

  }

  if(ChecksumCheck () ==0)
    return Command::BadCommand;
  //Check DILIMITER
  if(rxBuffer[0] != CMD_STRT)
    return Command::BadCommand; //bad DILIMITER

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
void SendBenchId(uint8_t IDeas){
  byte buf[4] = {CMD_STRT , CMD_PING, IDeas,  CMD_STRT ^ CMD_PING ^ IDeas};
  Serial.write(buf,4);
 
}


void Ack(){
  byte buf[3]= {CMD_STRT, CMD_ACK, 1};
  Serial.write(buf, 3);

}
void Nack(){
  byte buf[3]= {CMD_STRT, CMD_ACK, 0};
  Serial.write(buf, 3);

}
void SendData(){
/*Ptcl = 0xB3 
  Cmd = 0x02
  BenchId = Currentid;
  Battery Temp MSB  
  Battery Temp LSB  
  Bench Temp MSB  GP26 Internal Adc ? 
  Bench Temp LSB  
  Load MSB = 0x00  //not how it is connected to the board beside p2
  Load LSB = 0x00 
  Battery Voltage MSB 
  Battery Voltage LSB 
  Bench Current MSB 
  Bench Current LSB 
  Checksum
  //has to be changed **************************************************
  Serial.write(-1);
  Serial.write(-2);
  Serial.write(-3); */
}
bool ChecksumCheck (){
  uint8_t checksum =0xB3;
  for (int i =1; i < CurrentBufferSize-1; i++){
    checksum = checksum ^ rxBuffer[i];
  }
  if (checksum == rxBuffer[CurrentBufferSize-1]) return 1;
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
