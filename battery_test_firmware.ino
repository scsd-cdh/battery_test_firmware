#define CMD_STRT 0xB3

#define CMD_PING 0x00
#define CMD_DATA 0x02
#define CMD_STND 0x04
#define CMD_DISC 0x05
#define CMD_CHAR 0x06
#define CMD_COMP 0x07

#define RX_CMD_LENGTH 3
#define TX_CMD_MAX_LENGTH 12

uint8_t rxBuffer[RX_CMD_LENGTH];
uint8_t txBuffer[TX_CMD_MAX_LENGTH];

enum ExperimentStatus{
  Discharge = 0x00,
  Charge = 0x01,
  InProgress = 0x05,
  Failed = 0x06,
  Success = 0x07,
  Idle = 0x03
} status;

enum Command{
  Ping,
  RequestData,
  SetStandBy,
  SetDischarge,
  SetCharge,
  BadCommand
};


void setup() {

  Serial.begin(119200);
  status = ExperimentStatus::Idle;

  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);

}

void loop() {

  //has to be changed *********************************
  while(Serial.available() != 3);

  Command Incomming = ReadCommand();

  if(Incomming != Command::BadCommand && Incomming != Command::RequestData){
    ChangeStatus(Incomming);
    SendStatus();
  }
  else if(Incomming == Command::RequestData)
    SendData();
  else
    SendBadRequest();

}

Command ReadCommand(){

  int byte_num = 0;

  rxBuffer[byte_num++] = Serial.read();
  rxBuffer[byte_num++] = Serial.read();
  rxBuffer[byte_num++] = Serial.read();


  //Check DILIMITER
  if(rxBuffer[0] != CMD_STRT)
    return Command::BadCommand; //bad DILIMITER



  /*
  has to be changed ***********************************
  //Check CheckSum
  uint8_t checksum = CMD_STRT;
  for (int i = 1; i < RX_CMD_LENGTH; i++)
    checksum ^= rxBuffer[i];

  if(checksum != rxBuffer[RX_CMD_LENGTH - 1])
    return Command::BadCommand; //bad Checksum
  */


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

void ChangeStatus(Command command){
  switch (command){
      case Command::SetStandBy:
        status = ExperimentStatus::Idle;
        return;
      case Command::SetDischarge:
        status = ExperimentStatus::Discharge;
        return;

      case Command::SetCharge:
        status = ExperimentStatus::Charge;
        return;

      default:
        return;
    }
}

void SendStatus(){
  
  byte buf[3]={ CMD_STRT , status  , CMD_STRT ^ status };

  Serial.write(buf, 3);

}

void SendData(){

  //has to be changed **************************************************
  Serial.write(-1);
  Serial.write(-2);
  Serial.write(-3);
}

void SendBadRequest(){

  //has to be changed ***************************************************
  byte buf[3]={CMD_STRT,status,CMD_STRT ^ status};
  Serial.write(rxBuffer, 3);
}
