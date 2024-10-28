const byte pA0 = 2;
const byte pA1 = 3;
const byte pA2 = 4;
const byte pA3 = 5;
const byte pA4 = 6;
const byte pA5 = 7;
const byte pA6 = 8;
const byte pA7 = 9;
const byte pC0_KN_Strobe = A0;
const byte pC1_KN_Speaking = A1;
const byte pC2_KN_OK = A2;
const byte pB3_PC_Strobe = A3;
const byte pB4_PC_Speaking = A4;
const byte pB5_PC_OK = A5;
byte pA = 0;
byte pB = 0;
byte pC = 0;
byte state = 0;

void set_port_A_INPUT(){
  pinMode(pA0, INPUT);
  pinMode(pA1, INPUT);
  pinMode(pA2, INPUT);
  pinMode(pA3, INPUT);
  pinMode(pA4, INPUT);
  pinMode(pA5, INPUT);
  pinMode(pA6, INPUT);
  pinMode(pA7, INPUT);
}

void set_port_A_OUTPUT(){
  pinMode(pA0, OUTPUT);
  pinMode(pA1, OUTPUT);
  pinMode(pA2, OUTPUT);
  pinMode(pA3, OUTPUT);
  pinMode(pA4, OUTPUT);
  pinMode(pA5, OUTPUT);
  pinMode(pA6, OUTPUT);
  pinMode(pA7, OUTPUT);
}

byte read_A(){
	byte v=0;
	v = v << 1 | (digitalRead(pA7) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pA6) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pA5) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pA4) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pA3) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pA2) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pA1) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pA0) == HIGH ? 1: 0);
	return v;
}

void write_A(byte v){
	digitalWrite(pA7, v & 0x80 ? HIGH : LOW);
	digitalWrite(pA6, v & 0x40 ? HIGH : LOW);
	digitalWrite(pA5, v & 0x20 ? HIGH : LOW);
	digitalWrite(pA4, v & 0x10 ? HIGH : LOW);
	digitalWrite(pA3, v & 0x08 ? HIGH : LOW);
	digitalWrite(pA2, v & 0x04 ? HIGH : LOW);
	digitalWrite(pA1, v & 0x02 ? HIGH : LOW);
	digitalWrite(pA0, v & 0x01 ? HIGH : LOW);
}

void write_B(byte v){
	digitalWrite(pB5_PC_OK, v & 4 ? HIGH : LOW);
	digitalWrite(pB4_PC_Speaking, v & 2 ? HIGH : LOW);
	digitalWrite(pB3_PC_Strobe, v & 1 ? HIGH : LOW);
}

byte read_C(){
	byte v=0;
	v = v << 1 | (digitalRead(pC2_KN_OK) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pC1_KN_Speaking) == HIGH ? 1: 0);
	v = v << 1 | (digitalRead(pC0_KN_Strobe) == HIGH ? 1: 0);
	return v;
}

enum {
	WAITING_KN_ID = 0,
	
}

void setup() {
	Serial.begin(115200);
	//Serial.write("HD-AE5000 READ:\n");
	set_port_A_INPUT()
	pinMode(pC0_KN_Strobe, INPUT);
	pinMode(pC1_KN_Speaking, INPUT);
	pinMode(pC2_KN_OK, INPUT);
	pinMode(pB3_PC_Strobe, OUTPUT);
	pinMode(pB4_PC_Speaking, OUTPUT);
	pinMode(pB5_PC_OK, OUTPUT);

	state = WAITING_KN_ID;
}

void send_byte(byte v){
	while (KN_is_speaking()) {
		// wait	
	}
	digitalWrite(pB4_PC_Speaking, HIGH);
	digitalWrite(pB3_PC_Strobe, LOW);
	write_A(v);
	delay(1);
	digitalWrite(pB3_PC_Strobe, HIGH);	
}

void loop() {
	byte value_A = read_A();
	byte value_C = read_C();

	if ((value_C % 2) != (pC % 2)){
	    Serial.write(value_A);
	}
	pA = value_A;
	pC = value_C;
}
