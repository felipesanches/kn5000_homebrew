const byte dataPin = 13;
const byte interruptPin = 2;
volatile byte value = 0;
volatile byte count = 0;
volatile bool send_data = false;
byte column = 0;

void setup() {
  Serial.begin(9600);
  pinMode(dataPin, INPUT);
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), receive_bit, CHANGE);
}

void loop() {
  byte h, l;
  if (send_data){
    column = (column+1)%16;
    l = value & 0x0f;
    h = (value & 0xf0) >> 4;
    Serial.write((h > 9 ? '0':'A') + h);
    Serial.write((l > 9 ? '0':'A') + l);
    Serial.write(' ');
    if (column == 8) Serial.write(' ');
    if (column == 0) Serial.write('\n');
    value = 0;
    send_data = false;  
  }
}

void receive_bit() {
  value = (value << 1) | (digitalRead(dataPin)==HIGH ? 1 : 0);
  count = (count + 1) % 8;
  if (count==0){
    send_data = true;
  }
}
