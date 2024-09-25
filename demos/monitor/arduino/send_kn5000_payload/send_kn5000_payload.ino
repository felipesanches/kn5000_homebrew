const byte pA0 = 2;
const byte pA1 = 3;
const byte pA2 = 4;
const byte pA3 = 5;
const byte pA4 = 6;
const byte pA5 = 7;
const byte pA6 = 8;
const byte pA7 = 9;
const byte pC0 = A0;
const byte pC1 = A1;
const byte pC2 = A2;
const byte pB3 = A3;
const byte pB4 = A4;
const byte pB5 = A5;

byte pA = 0;
byte pBC = 0;

void setup() {
//  Serial.begin(230400);
  Serial.begin(115200);
  //Serial.write("HD-AE5000 READ:\n");
  
  pinMode(pA0, INPUT);
  pinMode(pA1, INPUT);
  pinMode(pA2, INPUT);
  pinMode(pA3, INPUT);
  pinMode(pA4, INPUT);
  pinMode(pA5, INPUT);
  pinMode(pA6, INPUT);
  pinMode(pA7, INPUT);
  pinMode(pC0, INPUT);
  pinMode(pC1, OUTPUT);
  pinMode(pC2, INPUT);
  pinMode(pB3, INPUT);
  pinMode(pB4, INPUT);
  pinMode(pB5, INPUT);
}

void print_hex(byte v){
  byte d = (v >> 4) & 0xF;
  Serial.write(d < 10 ? d + '0': d - 10 + 'A');
  d = v & 0xF;
  Serial.write(d < 10 ? d + '0': d - 10 + 'A');
}

void loop() {
  byte value_A = 0;
  byte value_BC = 0;
  value_A = value_A << 1 | (digitalRead(pA7) == HIGH ? 1: 0);
  value_A = value_A << 1 | (digitalRead(pA6) == HIGH ? 1: 0);
  value_A = value_A << 1 | (digitalRead(pA5) == HIGH ? 1: 0);
  value_A = value_A << 1 | (digitalRead(pA4) == HIGH ? 1: 0);
  value_A = value_A << 1 | (digitalRead(pA3) == HIGH ? 1: 0);
  value_A = value_A << 1 | (digitalRead(pA2) == HIGH ? 1: 0);
  value_A = value_A << 1 | (digitalRead(pA1) == HIGH ? 1: 0);
  value_A = value_A << 1 | (digitalRead(pA0) == HIGH ? 1: 0);

//  value_BC = value_BC << 1 | (digitalRead(pB5) == HIGH ? 1: 0);
//  value_BC = value_BC << 1 | (digitalRead(pB4) == HIGH ? 1: 0);
//  value_BC = value_BC << 1 | (digitalRead(pB3) == HIGH ? 1: 0);
//  value_BC = value_BC << 1 | (digitalRead(pC2) == HIGH ? 1: 0);
//  value_BC = value_BC << 1 | (digitalRead(pC1) == HIGH ? 1: 0);
//  value_BC = value_BC << 1 | (digitalRead(pC0) == HIGH ? 1: 0);

  value_BC = (digitalRead(pC0) == HIGH ? 1: 0);
  if ((value_BC % 2) != (pBC % 2)){
    Serial.write(value_A);
    // print_hex(value_A);
    // Serial.write(' ');
    // print_hex(value_BC);
    // if (value_A >= 'a' && value_A <='z' || value_A >= 'A' && value_A <='Z') {
    //  Serial.write(" \"");
    //  Serial.write(value_A);
    //  Serial.write('"');
    //}
    //Serial.write('\n');
  }
  pA = value_A;
  pBC = value_BC;
}
