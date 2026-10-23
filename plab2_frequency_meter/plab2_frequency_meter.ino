/* Lab 2: Timers on the ATmega 2560 microcontroller
 *
 * Description: Application for lab 2 (frequency meter)
 *
 *  - Counter from 0 to 999 on the display, with push buttons and keypad.
 *  - The display and keypad are refreshed by a Timer 3 interrupt every 5 ms
 *    (the external interrupt INT3 was used before).
 *  - Terminal menu: 1, 2, 3 = ways to view the counter; 4 = frequency meter.
 *  - EP1: 5 = Timer 4 (OC4A 50%, OC4B 10%, OC4C 25%, pulse on pin 13; PLEFT/PRIGHT move OC4B)
 *         6 = OC4C control with PUP/PDOWN and motor direction (IO10) with PENTER
 *  - Frequency meter: measures the signal entering through ICP3 (pin 9 of the micro).
 *
 * File: 	26-27_plab2_drg
 * Created		18 October 2026
 * Author:		David Ramos García
*/


// Push button definitions
// PORTC: PC[7:0] --> PINS 30-31-32-33-34-35-36-37
#define PRIGHT  30    //	PC[7] right push button
#define PDOWN   31    //	PC[6]		"" 		down
#define PLEFT   32    //	PC[5]		"" 		left
#define PENTER 	33    //	PC[4]		"" 		enter
#define PUP     34    //	PC[3]		"" 		up
#define SPEAKER 37    // 	PC[0]					speaker


// Definitions for keypad / display cathodes (to drive the 4 display digits in an interleaved way)
// PORTL: PL[7:0] --> PINES 42-43-44-45-46-47-48-49
//														 R1-R2-R3-R4-D1-D2-D3-D4 --> KEYPAD_ROWS (R) | DISPLAY_CATHODES (D)
#define D4    0xFE    // 1111 1110 shows units
#define D3    0xFD    // 1111 1101 		"				tens
#define D2    0xFB    // 1111 1011 		"				hundreds
#define D1    0xF7    // 1111 0111 		"				thousands
#define DOFF  0xFF    // 1111 1111 all off: all common cathodes set to "1"
#define DON   0xF0    // 1111 0000 all on: all common cathodes set to "0"
// display
int tabla_7seg[] = {63,06,91,79,102,109,125,39,127,103,0x77,0x7C, 0x39, 0x5E, 0x79,0x71,0xFF}; // 0,1,2... E,F, all on
// tabla_7seg[5] is the byte to output on PORTA to see a "5"
#define SEG_GUION 0x40		// only the middle segment: shows "-"

	//KEYPAD
	// Matrix mapping for the keypad keys
	char teclado_map[][3] = { {'1','2','3'},
                        {'4','5','6'},
                        {'7','8','9'},
                        {'*','0','#'}};


// ---------------------------------------------------------------------------
// APPLICATION VARIABLES
// ---------------------------------------------------------------------------
int contador = 0;						// number being shown (0..999)
int contador_anterior = 0;		// to know if it changed and make the beep sound
int paso = 1;							// how much the push buttons add/subtract (1 or 2)
int modo = 1;							// menu mode (1-6)
bool escribiendo = false;			// true while a number is being typed on the keypad
int num_teclado = 0;				// the number being typed

// Variables used in both the interrupts and loop() are declared "volatile"
// volatile lets the micro know that they can change at any time without warning
//the variable is used in an ISR or in loop()
volatile int digit = 0;				// which digit is next to light up (0=units ... 3=thousands)
volatile char tecla_vista = 0;		// key detected in this scan
volatile char tecla_pulsada = 0;	// new key ready for loop() to read
volatile bool tecla_libre = true;	// true if the key was released (so it is not repeated while held down)
volatile int frec_display = 0;		// frequency shown in mode 4 (-1 = out of range)

// Frequency meter variables
volatile unsigned long N = 0;			// counts the laps of Timer 3 (1 lap = 5 ms)
volatile unsigned long N_inicial = 0;	// value of N at the first edge
volatile unsigned int icr_a = 0;		// counter value (ICR3) at the first edge
volatile long ticks = 0;				// time between the two edges (1 tick = 0.5 us)
volatile byte estado_medida = 0;		// 0 = stopped, 1 = waiting for the 1st edge,
										// 2 = waiting for the 2nd edge, 3 = measurement ready

// Forward declarations for the functions defined further down
byte cifra(int pos);
void teclado(int col);
void fin_barrido();
void leer_teclado();
void leer_pulsadores();
void leer_menu();
void imprimir_menu();
void frecuencimetro();
void captura_on();
void captura_off();
void timer_4_on();
void timer_4_off();

/*
 * MICRO PORTS (each letter is a group of 8 pins)
 *   A -> display (the segments of the digits, the "screen")
 *   C -> push buttons and speaker
 *   L -> keypad (rows) and cathodes (the ones that choose which digit lights up)
 *
 * REGISTERS OF EACH PORT (x is the port letter: A, C, L...)
 *   DDRx  -> direction of each pin: 1 = output, 0 = input
 *   PORTx -> writes the output value (on an input pin it enables the pull-up)
 *   PINx  -> reads the value currently on the pins
 */


void setup() {
	Serial.begin(9600);		// serial channel initialization to communicate with:
												// PC --> "Tools/Serial Monitor" in the Arduino platform
												// Proteus --> console in Proteus (VIRTUAL TERMINAL)

	//Set PORTA as output. Connected to the segments a-b-c-d-e-f-g-dp of the 7-segment display)
	DDRA = 0xFF;    					// It can also be written in binary with B11111111 (DDRA = B1111111;)

	// Write to PORTA --> All segments to "1": on
	PORTA = 0xFF;  					// or also B11111111

	// PORT L setup: keypad_rows (input) | cathodes (output) --> R1-R2-R3-R4-D1-D2-D3-D4
	DDRL = 0x0F;		// PL[7:4] inputs (keypad rows: R1-R2-R3-R4)
									// PL[3:0] outputs (common display cathodes / keypad columns: D1-D2/C3-D3/C2-D4/C1
	PORTL = 0xFF; 	// enable pull-up resistors on the keypad row inputs PL[7:4]; display off


	// PORTC setup
	DDRC = B00000001; // 0x01:all bits are inputs except PC0, which is an output (speaker)
	PORTC = B11111110;	// 0xFE: enable all pull-up resistors except the one on PC0


	// ----- TIMER 3 SETUP -----
	// Mode 4 (CTC, TOP = OCR3A), prescaler N = 8, interrupt every 5 ms (vector TIMER3_COMPA_vect)
	// The timer counts at 16 MHz / 8 = 2 MHz (1 tick = 0.5 us) -> 5 ms is 10000 steps (0 to 9999)
	// The OC3A output (toggle) has frequency f = 16 MHz / (2*N*(1+OCR3A)) = 100 Hz

	cli();						// cli() disable interrupts

	// initialization
	TCCR3A = 0;
	TCCR3B = 0;
	TCCR3C = 0;
	TCNT3 = 0;					// counter to zero
	OCR3A = 9999;				// TOP = 9999 -> one lap every 5 ms
	pinMode(5, 1); 				// the OC3A output is on pin 5: set it as output
	
	//Timer 3 outputs
	//OCRA -> pin 5
	//OCRB -> pin 2
	//OCRC -> pin 3

	// TCCR3A: COM3A0 = 1 -> the OC3A output changes level (toggle) on every lap
	TCCR3A = B01000000;

	// TCCR3B: ICES3 = 1 -> the capture (frequency meter) is done on the RISING edge
	//         WGM32 = 1 -> mode 4 (CTC: counts up to OCR3A and goes back to 0)
	//         CS31  = 1 -> prescaler N = 8
	TCCR3B = B01001010;

	// TIMSK3: OCIE3A = 1 -> interrupt every time it reaches OCR3A (every 5 ms)
	// (the ICIE3 capture interrupt is only enabled in mode 4)
	TIMSK3 = (1 << OCIE3A);

	//---------TIMER 4 SETUP --------------------
	//REMOVED FOR THE ON/OFF MODE

	sei();						// enable interrupts

	imprimir_menu();
	}


// ---------------------------------------------------------------------------
// LOOP: only the user commands are handled here.
// The display is refreshed by the Timer 3 interrupt on its own.
// ---------------------------------------------------------------------------
void loop() {

	leer_menu();			// did the user choose an option in the terminal?
	leer_teclado();			// was a keypad key pressed?
	leer_pulsadores();		// was a push button pressed?

	// Beep every time the counter changes
	if(contador != contador_anterior){
		tone(SPEAKER, 1000, 50);		// 1000 Hz for 50 ms
		contador_anterior = contador;
	}
/*
 * MENU MODES (chosen by typing the number in the terminal)
 *
 * Mode | What it does                               | Display (D1-D2-D3-D4)
 * -----+----------------------------------------+---------------------------------------
 *  1   | Normal counter, 3 digits             | off - hundreds - tens - units  | 
 *  2   | Reduced counter (lower), 2 digits  | off - off - tens - units	| 
 *  3   | Reduced counter (upper), 2 digits  | tens - units - off - off | 
 *  4   | Frequency meter                         | 4 digits with the frequency in Hz ; if 1234Hz -> display 1234
																														if 50Hz -> display 0050
 *
 * Modes 1 to 3: the counter (0 to 999) is shown and push buttons and keypad work.
 * Mode 4: the ICP3 signal is measured, the keypad does nothing. Above
 *         9999 Hz the display shows "----" and the value goes out through the terminal.
 */


	if(modo == 4){
		frecuencimetro();			//computes the frequency and shows it
	}

  }

ISR(TIMER4_COMPA_vect){
	//set digital output PB7 (pin 13) to 1 as the first instruction, then to 0
	digitalWrite(13,1);
	digitalWrite(13,0);
	
	
}


// ---------------------------------------------------------------------------
// TIMER 3 INTERRUPT: runs by itself every 5 ms
// Each time it lights ONE display digit and checks ONE keypad column.
// Since it happens so fast, the eye sees all 4 digits lit at once.
// ---------------------------------------------------------------------------
ISR(TIMER3_COMPA_vect){
	PORTL = DOFF;			// turn everything off before switching digit
	PORTA = 0x00;

	N++;					// one more lap (used by the frequency meter)

	switch(digit){
		case 0:				// units (keypad column 1)
			PORTA = cifra(0);
			PORTL = D4;			//chooses which one lights up on the display, in this case units
			teclado(0);
			break;
		case 1:				// tens (keypad column 2)
			PORTA = cifra(1);		
			PORTL = D3;
			teclado(1);
			break;
		case 2:				// hundreds (keypad column 3)
			PORTA = cifra(2);
			PORTL = D2;
			teclado(2);
			break;
		case 3:				// thousands (no keypad column)
			PORTA = cifra(3);
			PORTL = D1;
			fin_barrido();	// a full scan is finished
			break;
	}	
	digit++;
	if(digit > 3) digit = 0;	// digit = positions ; if digit is 3 (thousands) it goes back to 0 and moves to units
}


// Returns the segments to light up at position 'pos'
// (0 = units, 1 = tens, 2 = hundreds, 3 = thousands) depending on the mode
byte cifra(int pos){
	int valor;			//number to draw on the display

	if(modo == 4){														//if modo = 4 (frequency)
		if(frec_display < 0) return SEG_GUION;			// out of range: "----"			; if negative, dash (error)
		valor = frec_display;										//copy the content of frec_display
	} else if(escribiendo){											//if typing 
		valor = num_teclado;					// while typing, the value is the typed number
	} else {
		valor = contador;							//otherwise the value does not change and is whatever the counter holds
	}

	// Should this digit be on in the current mode?
	//choose whether the digit should be on or off
	//pos -> (0 = units, 1 = tens, 2 = hundreds, 3 = thousands)
	//return 0 turns it off (digit set to 0)
	if(modo == 1 && pos == 3) return 0;			// mode 1: OFF-hundreds-tens-units
	if(modo == 2 && pos >= 2) return 0;			// mode 2: OFF-OFF-tens-units
	if(modo == 3){								// mode 3: tens-units-OFF-OFF
		if(pos < 2) return 0;
		pos = pos - 2;							// D2 = units, D1 = tens ; subtract from pos because it goes from D1 to D4, so when you do that
														// it goes from OFF-OFF-TENS-UNITS to TENS-UNITS-OFF-OFF
	}

	// Keep the digit that corresponds: divide by 10 'pos' times
	//extracts and returns the values in order from units to thousands ; if you have 347 it gives 7,4,3,0 in this order       
	for(int i = 0; i < pos; i++){
		valor = valor / 10;
	}
	return tabla_7seg[valor % 10];
}


// ---------------------------------------------------------------------------
// KEYPAD
// The keypad is scanned column by column. 'col' is the column to check
// (0, 1 or 2). That column is at 0 because its display digit is on.
// If a key is pressed in that column, one of the 4 rows also goes down to 0.
// ---------------------------------------------------------------------------
//finds out which key the user presses
void teclado(int col){
	int filas = PINL >> 4;		// PINL = R1 R2 R3 R4 | D1 D2 D3 D4. I only keep the rows
										// (I set the columns myself, so I already know their values)

	// filas is 15 (1111) if NO key is pressed.
	// If there is one, the pressed row is 0, so we take the 4 numbers with only one 0 in them, in order
	if(filas == 7)  tecla_vista = teclado_map[0][col];	// 0111 -> row 1 pressed	
	if(filas == 11) tecla_vista = teclado_map[1][col];	// 1011 -> row 2 pressed
	if(filas == 13) tecla_vista = teclado_map[2][col];	// 1101 -> row 3 pressed
	if(filas == 14) tecla_vista = teclado_map[3][col];	// 1110 -> row 4 pressed
}

// Called after the 4 digits are done (one full scan).
// It prevents the same key from being repeated while it is held down.
void fin_barrido(){
	if(tecla_vista == 0){				// if no key was seen in this scan
		tecla_libre = true;				// no key: it was already released and the key is free
	} else if(tecla_libre){					
		tecla_pulsada = tecla_vista;	// new key: notify loop()
		tecla_libre = false;
	}
	tecla_vista = 0;				//to start the next scan
}		

// What loop() does with the pressed key
void leer_teclado(){
	if(tecla_pulsada == 0) return;		// if there is no key, do not read and leave the function 

	char tecla = tecla_pulsada;			//copy the variable to a local one to work with it 
	tecla_pulsada = 0;						//and set the previous one to 0 to reset it and not process it twice 

	if(modo == 4) return;				// only modes 1-3 accept typing, mode 4 is the frequency meter that measures frequency

	if(tecla == '#'){					// '#' = accept the typed number, it works like Enter: it finishes and saves it to the display
		if(escribiendo){
			contador = num_teclado;
			if(contador > 999) contador = 999;		//so you do not go over the maximum 
			escribiendo = false;								// no longer typing: the display shows the counter again
		}
		num_teclado = 0;
	} else if(tecla == '*'){			// '*' = cancel
		num_teclado = 0;
		escribiendo = false;
	} else if(tecla >= '0' && tecla <= '9'){			// if the key is a digit (0-9)
		if(num_teclado < 100){							// maximum 3 digits
			num_teclado = (num_teclado * 10) + (tecla - '0');			
			//num_teclado is the number built so far, one digit at a time.
			//for 347 [it would be (0*10) + 3] + [(3*10) + 4] + [(34*10) + 7] = 347
			//(tecla - '0') -> '7' - '0'  =  55 - 48  =  7 ; to subtract a char
		}
		escribiendo = true;			//escribiendo = true -> shows num_teclado
												//escribiendo = false -> shows contador
	}
}


// ---------------------------------------------------------------------------
// PUSH BUTTONS
// How it works (explained simply):
//  - Buttons read 1 when RELEASED and 0 when PRESSED.
//  - Each button has an "ant_..." variable where we store how it was last time.
//  - If it was released (1) before and is NOW pressed (0), it was just pressed:
//    there we do the action ONLY ONCE (even if you keep the button held down).
//  - At the end of each button we store "ahora" in "ant_..." for the next pass of loop().
//  - "static" makes the ant_... variables NOT be erased when leaving the function,
//    so they remember their value the next time it is called.
// ---------------------------------------------------------------------------
void leer_pulsadores(){
	static int ant_up = 1, ant_down = 1, ant_left = 1, ant_right = 1, ant_enter = 1;
	int ahora;		// here we store the state of the button right now

	// RIGHT button: mode 5 -> decreases the OC4B width | other modes -> paso = 2
	ahora = digitalRead(PRIGHT);							// read the button (1 = released, 0 = pressed)
	if(ant_right == 1 && ahora == 0){						// was released before and is pressed now
		if(modo == 5){
			if(OCR4B > 0) OCR4B -= 200;						// -10% of TOP (TOP = 2000), limit at 0
		}
		else{
			paso = 2;
		}
	}
	ant_right = ahora;										// store the state for next time

	// LEFT button: mode 5 -> increases the OC4B width | other modes -> paso = 1
	ahora = digitalRead(PLEFT);
	if(ant_left == 1 && ahora == 0){
		if(modo == 5){
			if(OCR4B < 2000) OCR4B += 200;					// +10% of TOP, limit at TOP (100%)
		}
		else{
			paso = 1;
		}
	}
	ant_left = ahora;

	// UP button: mode 6 -> increases the OC4C width | other modes -> adds to the counter
	ahora = digitalRead(PUP);
	if(ant_up == 1 && ahora == 0){
		if(modo == 6){
			if(OCR4C < 2000) OCR4C += 200;					// +10% of TOP, limit 100%
		}
		else{
			contador = contador + paso;						// add 1 or 2 depending on the step
			if(contador > 999) contador = 0;				// goes from 999 to 0
		}
	}
	ant_up = ahora;

	// DOWN button: mode 6 -> decreases the OC4C width | other modes -> subtracts from the counter
	ahora = digitalRead(PDOWN);
	if(ant_down == 1 && ahora == 0){
		if(modo == 6){
			if(OCR4C > 0) OCR4C -= 200;						// -10% of TOP, limit 0%
		}
		else{
			contador = contador - paso;						// subtract 1 or 2 depending on the step
			if(contador < 0) contador = 999;				// goes from 0 to 999
		}
	}
	ant_down = ahora;

	// ENTER button: mode 6 -> toggles the motor direction (IO10) | other modes -> counter to 0
	ahora = digitalRead(PENTER);
	if(ant_enter == 1 && ahora == 0){
		if(modo == 6) digitalWrite(10, !digitalRead(10));	// 0 becomes 1 and 1 becomes 0
		else contador = 0;
	}
	ant_enter = ahora;
}


// ---------------------------------------------------------------------------
// TERMINAL MENU
// ---------------------------------------------------------------------------
void leer_menu(){
	if(Serial.available() > 0){				//if there are characters
		char op = Serial.read();				//option = what you read
		if(op >= '1' && op <= '6'){			//if the mode or menu option is valid (1-6)
			modo = op - '0';					//converts the character ('3') into a number (3), for example
			escribiendo = false;				// cancel whatever was being typed when changing mode
			num_teclado = 0;					//clear whatever was being typed

			// Modes 5 and 6: Timer 4 on | other modes: Timer 4 stopped
			if(modo == 5 || modo == 6) timer_4_on();
			else timer_4_off();

			// Mode 6: OC4C starts at 20% so the steps of 200 (10%) line up
			if(modo == 6) OCR4C = 400;

			// Changing mode stops any measurement in progress
			captura_off();					// turns off the capture interrupt
			estado_medida = 0;

			Serial.print("\nModo ");
			Serial.println(op);
		}
	}
}

void imprimir_menu(){
	Serial.println("\n--- MENU DE VISUALIZACION ---");
	Serial.println("1.- Modo normal de visualizacion (tres digitos)");
	Serial.println("2.- Modo reducido-inferior (dos digitos)");
	Serial.println("3.- Modo reducido-superior (dos digitos)");
	Serial.println("4.- Modo frecuencimetro");
	Serial.println("5.- Modo EP1");
	Serial.println("6.- Modo OC4C (PUP/PDOWN ancho, PENTER direccion motor)");
	Serial.print("Entrar opcion: ");
}


//------FUNCTIONS TO TURN TIMER 4 ON AND OFF (MODES 5 AND 6)-------------

void timer_4_on(){
	TCCR4A = 0;
	TCCR4B = 0;
	TCCR4C = 0;
	TCNT4 = 0;						// counter to zero
	ICR4 = 2000;					// TOP = 16 MHz / (2*8*500) = 2000

	OCR4A = 1000;					// OC4A at 50 % of TOP
	OCR4B = 200;					// OC4B at 10 %
	OCR4C = 500;					// OC4C at 25 %

	pinMode(6, 1);					// OC4A
	pinMode(7, 1);					// OC4B
	pinMode(8, 1);					// OC4C
	pinMode(13, 1);					// PB7, for the ISR pulse
	pinMode(10, 1);					// IO10: motor direction

	TCCR4A = B10101000;				// OC4A, OC4B, OC4C as normal PWM
	TCCR4B = B00010010;				// mode 8 and N = 8
	TIMSK4 = (1 << OCIE4A);			// interrupt TIMER4_COMPA_vect
}


// Stops Timer 4: no clock, no outputs and no interrupt
void timer_4_off(){
	TCCR4B = 0;						// no clock: the counter stops
	TCCR4A = 0;						// OC4A, OC4B y OC4C disconnected
	TIMSK4 = 0;						// no interrupt
}

// ---------------------------------------------------------------------------
// FREQUENCY METER
// Idea: measure the time (period T) between two rising edges of the signal
// entering through ICP3 (pin 9). The frequency is f = 1 / T.
// The ICP3 pin takes a "snapshot" of the counter (TCNT3) into the ICR3 register right
// when an edge arrives. With two snapshots we have the time between them.
//
// To avoid overloading the micro, we do NOT measure all the time: every 250 ms we enable the
// capture, take 2 edges and turn it off (only 2 interrupts per measurement).
//
// Range: with only (ICR3b - ICR3a) the minimum frequency would be 200 Hz (1 lap = 5 ms).
// Since we count laps (N), it can go down to ~1 Hz (2 s timeout).
// The real maximum frequency is limited by the interrupt time (tens of kHz).
// ---------------------------------------------------------------------------

// Runs by itself when a rising edge arrives at ICP3
ISR(TIMER3_CAPT_vect){
	unsigned int cap = ICR3;		// counter value at the edge
	unsigned long vueltas = N;		// complete laps so far

	// Rare case: the counter already wrapped around but the 5 ms interrupt
	// has not run yet. Then the lap has to be added by hand.
	if((TIFR3 & (1 << OCF3A)) && cap < 5000){
		vueltas++;
	}

	if(estado_medida == 1){
		// First edge: save where we are
		icr_a = cap;
		N_inicial = vueltas;
		estado_medida = 2;
	} else if(estado_medida == 2){
		// Second edge: time = laps * 10000 + (ICR3b - ICR3a)  (each lap = 10000 ticks of 0.5 us)
		ticks = (long)(vueltas - N_inicial) * 10000L + (long)cap - (long)icr_a;
		estado_medida = 3;
		captura_off();				// we have the measurement: turn off the capture
	}
}

// Called from loop() when we are in mode 4
void frecuencimetro(){
	static unsigned long t_inicio = 0;	// when the last measurement started
	static unsigned long t_print = 0;	// when it was last printed
	static long frecuencia = 0;			// last computed frequency (Hz)

	if(estado_medida == 0){
		// Stopped: if 250 ms have passed, start a new measurement
		if(millis() - t_inicio >= 250){
			noInterrupts();
			TIFR3 = (1 << ICF3);		// clear the old capture flag
			estado_medida = 1;
			captura_on();					// turn on the capture interrupt
			interrupts();
			t_inicio = millis();
		}
	} else if(estado_medida == 3){
		// Measurement ready: f = 1 / (ticks * 0.5 us) = 2,000,000 / ticks
		if(ticks > 0){
			frecuencia = 2000000L / ticks;
		}
		estado_medida = 0;
	} else {
		// Measuring: if the 2 edges have not arrived within 2 seconds, there is no signal
		if(millis() - t_inicio >= 2000){
			captura_off();
			estado_medida = 0;
			frecuencia = 0;
		}
	}

	// Display: up to 9999 Hz. More than that shows "----" (seen in the terminal)
	if(frecuencia <= 9999) frec_display = frecuencia;
	else                   frec_display = -1;

	// Write to the terminal every 500 ms
	if(millis() - t_print >= 500){
		t_print = millis();
		Serial.print("Frecuencia: ");
		Serial.print(frecuencia);
		Serial.println(" Hz");
	}
}

// ---------------------------------------------------------------------------
// TURN THE CAPTURE INTERRUPT ON / OFF
// TIMSK3 is a register with one "switch" (bit) for each Timer 3 interrupt:
//   bit 1 = OCIE3A -> 5 ms interrupt (display and keypad): ALWAYS on
//   bit 5 = ICIE3  -> capture interrupt (frequency meter): only while measuring
// (1 << OCIE3A) = puts a 1 in the OCIE3A bit  |  the | symbol joins two switches
// ---------------------------------------------------------------------------
void captura_on(){
	TIMSK3 = (1 << OCIE3A) | (1 << ICIE3);		// 5 ms: YES | capture: YES
}

void captura_off(){
	TIMSK3 = (1 << OCIE3A);						// 5 ms: YES | capture: NO (same as in setup)
}